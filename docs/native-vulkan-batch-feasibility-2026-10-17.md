# Vulkan nativo: atribuição de custo e prova de comandos em lote

Investigação aprovada após o usuário pedir uma estratégia mais rápida para
60 FPS reais. **60 FPS ainda não atingidos.** Esta etapa mede o limite de uma
substituição do XDK e testa outra organização dos recursos na GPU antes de
reescrever o renderizador.

## Resultado que muda a prioridade

Na execução `native60_original_split`, as chamadas gráficas originais do XDK
representaram **7,96%** do tempo observado. A captura nativa externa representou
**41,97%**. São tempos de parede, incluindo desagendamento, medidos em 21 janelas
que totalizam 42,014 segundos de gameplay. Não são contagens de instruções.

O contador agregado de originais considera somente a chamada gráfica mais
externa: DrawUP/BeginVertices e EndTiling/Resolve não devem ser somados duas
vezes. Capturas nativas aninhadas são descontadas separadamente; nesta amostra
o agregado reportou zero de captura aninhada. Os originais sempre são executados.

A ordem de grandeza é de aproximadamente 6 ms de chamadas gráficas originais
por quadro, usando a cadência observada apenas para normalização. Eliminá-las
inteiramente não justificaria uma previsão de 60 FPS. Elas também gravam PM4 e
limpam estados pendentes necessários à implementação atual; não foram omitidas.

Últimos 400 quadros da timeline:

| Medida | Média |
| --- | ---: |
| Intervalo entre quadros | 70,40 ms |
| Game, incluindo trabalho e esperas fora do renderer | 69,92 ms |
| Frontend nativo | 29,25 ms |
| Residual game menos frontend, sem atribuição exclusiva | 40,67 ms |
| Worker | 34,14 ms |
| Record Vulkan | 51,05 ms |
| GPU real | 9,46 ms |

As etapas se sobrepõem; **não somar os tempos**. Mesmo resolver somente o
recorder não elimina os limites do produtor/worker. O residual de game tampouco
prova 40,67 ms de simulação pura. Cada estágio serial precisa sustentar 16,67 ms
para 60 FPS.

O sampler do Windows, sem suspender threads, observou aproximadamente 0,972 core
na thread principal (18884), 0,809 no recorder (16436) e 0,497 no worker (24972).
Portanto, há trabalho efetivo de CPU, além de possíveis esperas nas medidas de
parede. Esses valores não identificam todas as funções que consomem esse tempo.

Gameplay/HUD passaram no início e depois do movimento. FPS instrumentados:
**13,5 parado / 13,4 andando**; não são uma comparação limpa de ganho. Mesmas
opções Vulkan, limite 60, escala 1, MSAA 1 e frame generation desligado; mouse
desligado nesta sonda. A captura inicial foi inspecionada, mas ainda inclui
desktop/título e recorta a janela. Não há prova de replay determinístico nem
um novo aceite global de imagem.

Evidências: `logs/bench_native60_original_split.log`,
`logs/timeline_native60_original_split.{csv,txt}`,
`logs/native60_original_split_attribution.json`,
`logs/native60_original_split_thread_cpu.csv` e imagens `bench_*_split_*`.
Análise local reproduzível: `build/analyze_native60_original_split.py`.

## Prova independente na RTX 2060

A placa confirmou `multiDrawIndirect`, `drawIndirectFirstInstance`,
`shaderDrawParameters`, arrays de descritores em runtime, indexação não uniforme
de imagens/buffers, bindings parcialmente preenchidos, update-after-bind e
buffer device address. O diagnóstico criou efetivamente um device com os
recursos requeridos, o layout, o pool e um conjunto global contendo capacidade
para **4.096 imagens, 65.536 buffers e 64 samplers**.

O teste de GPU usa shaders pequenos próprios e os `ResourceStore`/`TargetStore`
de produção para uploads/alvos. Não usa shaders do jogo. Uma tabela de registros
seleciona o buffer de vértices e a textura de cada draw. Foram usados slots
esparsos: buffers **257/65.535** e imagens **511/4.095**. O restante da tabela
permanece sem preencher, conforme os features explicitamente habilitados.

Dois triângulos se sobrepõem: o primeiro cobre o alvo em vermelho; o segundo
cobre a metade direita em verde. O teste confere **todos os pixels** do alvo
16x16 após uma fence real. Trocar a ordem ou usar o recurso do outro draw altera
o resultado esperado. Passaram quatro casos:

- 2 draws emitidos separadamente;
- os mesmos 2 draws em um comando indireto;
- 3.000 draws alternados emitidos separadamente;
- os mesmos 3.000 draws em um comando indireto.

O shader de vértices usa `DrawIndex` para consultar o registro e transmite o
índice da imagem ao pixel shader sem interpolação. Nenhum conjunto de
descritores é reconstruído ou trocado entre os draws do lote. Os recursos só
são destruídos/reescritos depois da fence desta fixture.

O registro de 16 bytes **é específico deste teste**, sem os bancos float, bool,
loops, formatos e estados do jogo. Não substitui os 12 KiB do ABI atual e não
prova desempenho ou correção dos shaders recompilados. A fixture não cobre
índices, recortes por draw, mudanças de pipeline, resolves, aliases, 3D/cube,
samplers, atualização concorrente nem a vida útil do futuro heap no jogo.
Camadas de validação não foram executadas nesta prova.

A janela de emissão de 3.000 comandos mediu 227,9 us separadamente e 1,9 us em
lote na execução esparsa. Isso mede **apenas as chamadas de emissão**, depois de
uploads, descritores, shaders e pipeline prontos; não mede o custo de preparar
um frame nem prevê a redução de 51,05 ms do recorder. Na primeira execução
anterior, a primeira emissão indireta de 2 draws custou 619,2 us, evidenciando
custo de primeiro uso. Não apresentar esses números como ganho de FPS.

Evidência: `logs/native60_batch_gpu_probe_sparse1.log` (números acima) e
`logs/native60_batch_gpu_probe_sparse2.log` (reprodução pelo helper).
Fonte descartável da prova em `build/native60_batch_capabilities.cpp`,
`build/native60_batch_gpu_probe.inc` e `build/native60_batch_probe.hlsl`.
Reprodução no checkout: `build/run_native60_batch_probe.ps1`.
O executável arquivado da primeira prova esparsa tem SHA256
`5044C79939F99EF33BFAD2E70C35FF97303F2237838656F6A97A5FBFCCDF4C3C`.
Os arquivos dessa sonda permanecem artefatos locais; não fazem parte do produto.
Uma cópia dos fontes/shaders/binário/log da prova está em
`artifacts/native-renderer-performance/batch_probe_2026-10-17/`. É um snapshot
para inspeção; para reconstruir, usar os arquivos de `build/` no checkout.
A reprodução passou nos quatro casos novamente; o binário reconstruído tem
SHA256 `1055A92E8663957A36B23B373C0C51E6DEDFE575483A3E43E51BB1C01D645098`.
Os hashes diferentes não estabelecem mudança de comportamento: as duas
execuções passaram nos mesmos readbacks; não comparar hashes como desempenho.

## Correção das medições isoladas

O cache local de `build/tests-vulkan` tinha `CMAKE_BUILD_TYPE=Release`, mas
`CMAKE_CXX_FLAGS_RELEASE` vazio. O `build.ninja` confirmou que biblioteca e
diagnósticos estavam sem `-O`. O jogo já estava corretamente em `-O3 -DNDEBUG`
com ThinLTO: isso não explica nem invalida seus FPS medidos.

`tools/build_vulkan.ps1` agora passa explicitamente `-O3 -DNDEBUG`. A nova
compilação e o `build.ninja` confirmam os flags. Os antigos tempos absolutos de
microbenchmarks desse diretório não devem ser tratados como custo de código
otimizado nem usados para prever ganhos no jogo. Os testes funcionais antigos
continuam sendo evidência dos casos que executaram. O build nativo separado
`build/tests-native` já tinha os flags corretos.

Após corrigir o build Vulkan, uma amostra de sanidade mediu bindings em
2,81 us/draw e descritores dummy repetidos em 1,13 us/draw (2.999 hits/1 miss).
São fixtures sintéticas; não substituem as cargas e a concorrência do jogo.
Não é um A/B temporal controlado com as medições históricas.
Logs: `logs/native60_optimized_{bindings,descriptors}_micro.log`.

CTest Vulkan passou e os três readbacks de `--production-bindings` passaram
com a compilação otimizada. Validation layers continuam indisponíveis nesses
testes; `validation_errors=0` não representa validação habilitada.
CTest nativo também passou, 2/2. Logs de validação:
`logs/native60_original_split_{native_tests,vulkan_tests,pixels}.log`.

## Próxima prova com impacto estrutural

A direção viável na GPU é uma tabela global de recursos e comandos preparados
em lotes, preservando a sequência original. Ainda não há base para prometer que
isso sozinho produzirá 60 FPS. A próxima prova deve adaptar uma cópia isolada do
`shader_common.h` real e o contrato de constantes/vertex fetch, incluindo:

- Draw ID disponível em VS e PS, com bancos VS/PS/shared corretos por draw;
- metadados de declaração e streams sem reconstruir 224 entradas por draw;
- preservação de bools, loops, remapeamento, gamma e tipos de fetch;
- nova identidade do ABI no cache de shaders e compatibilidade VS/PS;
- slots versionados: nunca sobrescrever descritores ainda lidos pela GPU;
- limites de lote em mudanças de pipeline, estado dinâmico, pass, resolve,
  clear e dependências/aliases, mantendo a ordem dos draws;
- orçamento separado para captura/produtor e replay; GPU batching sozinho não
  elimina os seus atuais 29–40+ ms por estágio.

Uma prova representativa com esse contrato deve preceder a integração no jogo
e outra build completa. Não repetir os caches locais já descartados nem cortar
efeitos/chamadas para contabilizar quadros incompletos como 60 FPS.

O profiler adicional é opt-in por `SR_NATIVE_GUEST_CPU_PROFILE=1`; sem a variável
os hooks continuam chamando os mesmos originais, sem ler relógios de perfil.
Build do jogo e rodada instrumentada passaram. Executável da rodada:
`A1E78317C3DD455BAB2A4C80BB98C7FF5E6CBC7D8963F8CA1D5D6B7625F8628A`.
Nenhum commit, publicação ou troca do executável de distribuição nesta etapa.

Continuação: a prova com o contrato real, readbacks, validação Khronos e os
241 shaders foi executada. O compilador agora possui um perfil global
experimental com cache separado. Resultados, limitações e a integração ainda
necessária estão em
[native-vulkan-global-contract-2026-10-17.md](native-vulkan-global-contract-2026-10-17.md).
