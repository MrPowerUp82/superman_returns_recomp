# Normalização da profundidade das sombras — 30/10/2026

O defeito foi investigado também no sobrevoo do benchmark, conforme o relato
do usuário. O caminho nativo usava `R16G16_SNORM` para o alvo Xenos `k_16_16`,
mas gravava a saída original sem normalizar o intervalo fixo de −32 a +32.
Os casters exportam `32 * Z`. O hardware do PC limitava esse valor a ±1
antes do resolve, destruindo a variação de profundidade. O trace do benchmark
confirma `RB_COLOR_INFO=00040000` e `RB_COPY_DEST_INFO=003B0C01` (bias −5).

O shader agora multiplica cada saída de cor pela escala do respectivo alvo
depois do alpha test e do alpha-to-coverage. O host preenche c31/bytes 496–511
em todos os draws: 1/32 para o formato 4 e 1 para os demais alvos, inclusive
slots vazios. Os clears explícitos, de tiling e de resolve usam as mesmas
unidades. O caminho Vulkan recebe a escala no pacote neutro e nos clears.
RGBA16, armazenado em float por padrão, conserva as unidades anteriores.

A tradução foi atualizada pelo patch `0014-color-output-scale.patch`.
O validador da variante de cobertura aceita as duas caudas auditadas, mantém
a escala e atualiza a identidade de cache. O preparo do tradutor Vulkan
reconhece o ABI já aplicado sem depender do contexto textual de patches
posteriores. Os artefatos locais DXIL, SRSL e Vulkan portátil/global foram
regenerados e instalados junto ao executável Release.

## Verificação

Após testar a build corrigida, o usuário confirmou que o problema relatado
foi resolvido, incluindo o caso observado no benchmark. Essa confirmação
visual complementa os testes e o readback abaixo; não equivale a uma
comparação completa com o console.

- Build Release concluído.
- CTest nativo: 5/5; Vulkan: 1/1; tradutor: 1/1.
- Testes Python das ferramentas de shader e do serviço Vulkan: 11 passaram.
- Corpus: 241/241 compilam em DXIL e SPIR-V; bibliotecas Vulkan portátil e
  global também contêm 241 shaders, sem falhas.
- Regressões cobrem ordenação de profundidades, mudanças de projeção,
  clears, slots MRT, retorno à escala 1, saídas condicionais e preservação
  do alpha test/cobertura.

As capturas D3D12 estão em `logs/shadow_scale_20261030_before_late/dump/`
e `logs/shadow_scale_20261030_after_late/dump/`. As duas rodadas terminaram
com benchmark válido. O readback posterior mostra profundidades intermediárias
na GPU: Z1 passou de 599 valores distintos na captura anterior para 40.281;
Z2, de 1 para 22.685. O clear distante continua em +1 e a camada Z3 sem
resolve continua zerada. Relatório: `logs/shadow_scale_20261030_validation.json`.

As câmeras e os instantes dessas capturas diferem. Os números confirmam a
recuperação da faixa de profundidade, mas não são uma comparação de imagem
alinhada nem comprovam isoladamente a ausência de toda instabilidade temporal. Trace,
readback e compilação concorrente interferiram nas rodadas; não há conclusão
de desempenho. Capturas extras `s001500` e `s002000` ainda mostram o início
uniforme da apresentação e não foram usadas como evidência visual.

## Limite do incremento

O resolve ainda copia o recurso SNORM, sem implementar a conversão numérica
geral para destinos unsigned nem todos os exponent biases. Valores negativos
deveriam ser limitados a zero antes da filtragem no destino unsigned observado.
Essa diferença preexistente permanece; limitar depois da amostragem não seria
equivalente. O incremento corrige a perda de faixa no armazenamento, sem
afirmar paridade completa das sombras com o console.
