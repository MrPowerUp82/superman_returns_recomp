# M1 — implementação atual e limites de aceitação

Designs anteriores ao código: graphics-milestone-1-object-motion-design-2026-11-05.md,
graphics-milestone-1-consumption-design-2026-11-05.md e extensão de partes rígidas
em graphics-milestone-1-rigid-object-design-2026-11-05.md.

O produtor por objeto está ligado ao NGX D3D12. `sr_native_object_motion=true`
é o padrão do renderer nativo D3D12 e exige reinício para observar todo lifecycle.
`false` mantém o baseline de câmera. Vulkan não anuncia suporte a este produtor.
A aceitação de veículos e a eliminação de todo rastro em gameplay continuam
pendentes de evidência visual por classe; não inferir isso de fixtures sintéticos.

## Produção e consumo

O VS original produz clip positions atuais e anteriores; conservar sua saída
pós-VS também conserva skinning e deformação de capa. O histórico exige objeto,
geração, parte de asset, mesh/VB/IB/fetch, versões de conteúdo, topologia e ordem
de vértices consistentes em frames consecutivos. Partes rígidas usam duas rotas
PPC auditadas; instancing ambíguo não ganha identidade por ordinal ou por shader.
Matrizes/constantes mutáveis pertencem aos inputs, não à identidade do objeto.
View/projection anteriores e mundo anterior compõem o mesmo RG16F screen-space.

Rasterização produz RG16F e validade R8 por pixel, incluindo perspectiva,
W inválido, finitude, limites FP16, viewport/scissor e depth EQUAL do contribuidor.
Invalidadores sem escrita de Z conservam a comparação real do draw para não
apagar o primeiro plano com transparência ocluída. Ownership R32_UINT usa a saída
do PS original das famílias de skin/capa; VFX com alpha relevante mantém rejeição
atômica em máscara separada. Inputs desconhecidos rejeitam história de objeto.

A composição DLSS mantém current-to-previous em pixels de resolução interna,
sem jitter nos vetores, escala NGX 1 e flag MVLowRes. O SDK recebe recurso externo
RG16F e BiasCurrentColorMask para áreas sem correspondência válida. Câmera ocupa
clear depth quando sua reprojeção é válida; objeto desconhecido não recebe
movimento artificial de câmera. A mesma saída/validade e máscara reativa alimenta
o temporal do RT, com diferença de jitter e testes de depth.

Linhagem de cor usa recurso/lifetime/content serial/ticket; clears, alias e cópias
parciais invalidam a origem. Fog, tonemap, filtros e cópias completas só propagam
as rotas de posição auditadas. O RT composto pelo port propaga a origem exata.
Três slots e todos os recursos só são reutilizados após seus fences; hot resize
preserva PSOs independentes de dimensão e recria targets em uma fronteira segura.

## Verificação executada

- SDK isolado ReXGlue v0.10.0 f5337cdc947ff6d4c4196737e2c807a48f2a1fc2;
  checkout anterior do usuário preservado. DLSS oficial 3.7.20.
- CTest nativo: seis executáveis passaram; sr_native_tests contém 703 casos,
  zero failed checks, incluindo seleção rígida, matrizes mutáveis e ambiguidade.
  O target adicional de teclado contém oito regressões de retenção de botões.
- GPU RTX 2060: temporal_motion_smoke passou com shader real de composição DLSS,
  movimentos assinados, jitter, perspectiva, reversed depth, transparência ocluída,
  três slots, resize fenced e cache de PSOs. São fixtures sintéticos.
- Capturas/execuções do jogo anteriores em logs/graphics_prewarm_rigid_smoke e
  graphics_combined_rt_smoke confirmaram NGX/composição/fences e ownership/RT
  juntos. Não substituem o percurso com objetos móveis de todas as classes.
- Matriz final de três rodadas por configuração deve usar um único hash do
  executável após concluir o menu APT e o cache Vulkan; resultados anteriores
  permanecem identificados pelos seus hashes e não são promovidos a finais.

## Diagnóstico e limitações

A correção graphics-milestone-1-index-envelope-design-2026-11-05.md removeu um
bloqueio de produção: o intervalo normalizado de índices só era congelado com
trigger diagnóstico. Agora DLSS com object motion ou RT também o capturam no
frontend, respeitando endian, restart, base vertex e bounds64bits. Sem DLSS/RT
o scanner conserva seu comportamento preguiçoso. A revisão independente confirmou
que a omissão fazia todos os indexed falharem em vertex_range_valid, inclusive
skin/capa e câmera2C45. Contadores anteriores à correção não certificam cobertura.

`--sr_native_object_motion_capture_swap=N` captura um swap. Em gameplay,
`--sr_native_motion_capture_trigger_path=CAMINHO` consome um arquivo de trigger
para uma captura sem reiniciar a cena. `--sr_native_dump_dir` escolhe a saída.
A ferramenta tools/graphics/inspect_temporal_motion.py gera máscara/velocidades;
check_dlss_capture.py verifica o output contra o apresentado em espaço linear.

A cobertura válida é parcial e varia com materiais, visibilidade e recursos.
As duas rotas rígidas ainda não provaram veículos móveis no percurso observado.
Capa/skin observadas não demonstram todos os NPCs. Nenhum fallback é evidência de
que um objeto recebeu MV correto. Capturas pontuais não provam ausência de ghosting
ao longo de uma sequência. Não há implementação nova FSR3 nesta etapa.

## Complemento de gameplay de 06/11

O design static-ia-fetch corrige a assinatura de draws com input assembler:
descritores de fetch antigos, que o shader dessa rota não lê, deixaram de
invalidar a identidade. VB/IB, layout, conteúdo, geração e conversão de tipos
continuam no contrato. O design opaque-alpha usa o alpha final do PS original
para certificar o contribuidor por pixel; misturas sem alpha1 recebem validade0.
A fixture GPU reproduziu o erro de preservar o MV do fundo em pixels mistos e
passou depois da correção. Não se promove esse certificado de tela a BLAS opaco.

Em graphics_owner_cold_gameplay_20261106, o dump s2539 atribuiu ao corpo C687
39.094 pixels válidos (39.090 com magnitude acima de0,01pixel), à capa A523
24.830 válidos/móveis e à família C209 39 válidos/móveis. Outros41 pixels C209
eram mistos. Os93 draws C209 tinham correspondência, mas isso sozinho não
atestaria seus pixels. Em s3801 houve117 pixels C209 válidos/móveis; corpo/capa
estavam fora da região visível. tools/graphics/inspect_motion_owners.py calcula
esses números cruzando ownerR32, manifesto de draws e RG16F/R8 do mesmo quadro.

O trigger agora congela frame_count+1 além do rótulo de swap dos draws. O log
liga explicitamente draw_swap2539 a present_swap2540/frame2540. A atualização
do swap ocorre depois dos draws; comparar somente os swaps perdia os dumps
finais. As capturas incluem cor NGX e apresentação, sem leitura no modo normal.

graphics_live_frame_gameplay_20261106 também executou solo/movimento e decolagem
com tráfego na tela. Seus trechos delimitados tiveram1504/649 quadros e zero
PSO em runtime, compilação ou shader ausente. O auditor aceitou as duas rotas
em logs/graphics_live_frame_routes_audit_20261106.json, com exe arquivado e
fingerprints. Isso não prova todas as fases, classes de veículo ou ausência
de ghosting em qualquer animação. Os benchmarks finais ficam no relatório
consolidado, com seu próprio hash; esses dumps não são medição de desempenho.
