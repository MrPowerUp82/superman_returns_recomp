# D3D12: primeira validação de layout POSITION0 em gameplay

Data: 24/10/2026. Branch: `codex/rt-dlss-menu-20261024`, HEAD base `adddef2`.
Diagnóstico opt-in; nenhuma alteração visual nem implantação de RT/DLSS.

## Evidência da cena

`logs/bench_rt_geometry_gameplay_2410.log`: o HUD foi detectado às 04:29:55 e
`guest_swap=1650` capturado às 04:30:10, com 2.071 submissões. A captura de
`logs/bench_rt_geometry_gameplay_2410_start.png` mostra Superman, trânsito,
cidade e HUD. A captura não corresponde apenas ao menu inicial.

Após adicionar o reconhecimento de `POSITION0` à instrumentação:

- `logs/bench_rt_position_layout_2410.log`: HUD detectado às **04:44:36**;
  `guest_swap=1650` capturado às **04:44:46**, portanto em gameplay.
- 2.945 submissões: **2.827** candidatas com buffers versionados, **52** com
  geometria gerada e **66** com stream não versionado.
- Nas **2.827 candidatas**, todas declararam `POSITION0` com tipo XDK
  `0x002A23B9` (**FLOAT3**), no stream existente e com largura/offset inicial
  dentro dos limites observados; **0** declarações ausentes, formatos não
  mapeados ou limites inválidos nesta captura. As 118 restantes não passaram
  pela validação de posição.
- Distribuição de superfícies: segmento 0 com 1.249 draws em 640×640, sem
  depth observado; segmento 1 com 1.643 draws em 1280×720, com depth
  observado em 1280×720; segmentos posteriores de pós-processamento e dez
  draws finais em 1280×720. Esses segmentos **ainda não possuem papel de
  cena/HUD atribuído de forma comprovada**.
- VS candidato de câmera `2C45BFFE76D77398` continuou com matrizes
  invertíveis e estáveis dentro dos quadros observados; o contrato temporal
  ainda reporta `camera_missing`. Convenção, frustum, profundidade e
  movimento não foram validados.

O classificador usa a declaração guest já interpretada pelo caminho raster,
`GetVertexEncoding`, stream real do draw e bounds iniciais do buffer. A contagem
ocorre apenas durante a captura explicitamente habilitada. Isso **não** testa
os valores dos vértices, bounds de todos os índices, strip restart,
transformações de instância, skinning, objetos fora de tela, identidade de
mesh ou elegibilidade de BLAS/TLAS. Uma classificação `float3_layout` é apenas
evidência de declaração válida para a próxima etapa de decodificação.

## Instrumentação seguinte: bounds da IA e primeira amostra host

Foi adicionada uma verificação separada da declaração: o offset do stream é
relativo ao **buffer residente completo**, enquanto `StreamPlan.size` é o
tamanho da **view IA**. A sonda verifica se a view cabe no buffer, se o último
vértice de um draw não indexado cabe na view e se a janela de índices 16/32-bit
cabe na view de índices. As contas de vértices usam divisão antes da
multiplicação para rejeitar números extremos sem overflow.

No draw **indexado**, uma janela matematicamente válida fica classificada como
`index_values_needed`: ainda é preciso observar os índices efetivos, aplicar
`base_vertex`, tratar o primitive restart e verificar todas as posições
referenciadas. Funções puras permitem testar a normalização de índice e o
decode `FLOAT3` host, mas não equivalem a evidência capturada no jogo.

No draw **não indexado** com declaração `FLOAT3` e janela válida, a sonda pode
registrar a posição do **primeiro vértice** usando exclusivamente o scratch CPU
do `ApplyBuffer` após a conversão endian de um **upload integral novo**
(`action=2`). Upload parcial ou buffer já residente resulta em
`no_full_fresh_upload`, sem reler a memória guest e sem ler o heap D3D12.
O valor decodificado representa coordenadas locais, ainda sem transformações.
Logs agora distinguem `nonindexed_window_valid`, `index_values_needed`,
falhas de limites e `decoded`/`nonfinite`/`no_full_fresh_upload`.

**Ainda não foi executada uma nova captura de gameplay com esses campos.** As
2.827 declarações anteriores não podem ser reclassificadas retroativamente.
O próximo experimento curto deverá medir as novas categorias, identificar a
distribuição de índices e verificar se houve uploads integrais amostráveis;
nenhum draw pode ser considerado BLAS-ready nesta etapa.

## Verificação e desempenho

- `cmake --build build/tests-native --target sr_native_tests --config Release`:
  sucesso; `ctest --test-dir build/tests-native --output-on-failure`:
  **2/2 suítes**, zero falhas.
- `.\build.cmd`: sucesso, registro em
  `logs/build_geometry_position_layout_2410.log`; binário D3D12 produzido.
- `git diff --check`: zero erros de whitespace.
- Segundo benchmark instrumentado: 10,0 FPS médios parado (mínimo 6,7),
  9,8 avançando (mínimo 6,8), 34–35% de uso de GPU.
  A rodada anterior teve 9,6/11,2 FPS. Poses e draws diferem; não atribuir
  as diferenças à instrumentação nem usá-las como medida de ganho causal.

## Próxima extração necessária

Validar a faixa de índices efetivos (inclusive triangle strips/restart) e
decodificar valores `FLOAT3` após a conversão endian já realizada pelo
renderer. Relacionar amostras de geometria a transformações de instância,
identificar skinned Superman e resolver convenção de câmera/depth. Só depois
produzir triângulos em world space, vida útil de BLAS/TLAS e cobertura de
casters fora da câmera. Separar cor/depth da cena do HUD antes do DLSS Quality.
