# M1 — separar metadados de IA e shader fetch

Design antes do código. A captura real em graphics_final_gameplay_indexed trouxe
tokens/partes/bancos finitos e envelopes corretos para C209, C687 e C5E8. O probe
CPU dos bancos congelados passou os helpers de skin e partes rígidas, inclusive
a igualdade de câmera/owner em C5E8. Mesmo assim o consumidor registrou zero.

O renderer congela dynamic_vertex_fetch no frontend por GuestShaderInfo. Em
BindVertexStreams, somente draws com shader fetch zeram e preenchem os campos
descriptor/offset/stride do array224x4. Draws IA não os usam; só o campo type é
atualizado em SharedConstants para converter atributos de IA. O histórico atual
exige resolver TODOS esses descriptors, inclusive sobras de draws anteriores,
contra a lista de buffers do draw IA atual. Isso pode rejeitar correspondência
sem que exista dependência real do shader nesses campos.

Adicionar modo explícito ao helper puro CaptureTemporalVertexFetch, mantendo
shader-fetch=true como padrão conservador. No modo IA, preservar type e zerar
descriptor/offset/stride na assinatura, sem alterar os metadados de rendering.
Continuar usando todas as identidades reais VB/IB, layout, offsets, strides,
gerações, versões e ordem no hash de mesh do consumidor. O modo vem somente
do flag congelado do comando, já usado para escolher o caminho real de binding.

Aplicar a mesma distinção à normalização de capa; o restante de sua cadeia de
buffers/topologia permanece obrigatório. Draws shader-fetch continuam exigindo
resolução completa e rejeitando descriptors desconhecidos/ambíguos. Regressões
devem provar stale-descriptor IA independente, type ainda relevante, falha em
modo dinâmico e preservação dos gates de mesh/capa. Medir os contadores reais
após o build; este ajuste não autoriza misturas transparentes nem classifica
qualquer token como veículo. A hipótese só é aceita após correspondências reais.

## Verificação

Regressões falharam com a resolução antiga e passaram com a distinção IA;
699casos/0falhas no sr_native_tests, CTest6/6. No jogo, build
b900e3463a4132cf4a65d1b7d343d850d5a350e61e7d5a4bb0b8af53d680b8e2,
logs/graphics_static_ia_gameplay/game.log registrou em22:42:02:
C5E8 150/150, C209145/150, C6878/8, capa1/1 matches. Rígidos chegaram a82/82
em frames posteriores; são renderables com partes auditadas, não rótulos de ator.
O percurso idle/forward cobriu3355quadros/8.816.767draws, com zeroPSO/shadercompile/
shadermiss no intervalo. Isso confirma que o histórico passou a encontrar objetos,
mas não libera o blend RGB dos corpos; o design opaque-alpha trata esse gate
separadamente. Capturas/depth/validade e executable.exe exato ficam no diretório.
