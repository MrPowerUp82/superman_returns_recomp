# Baseline e balanceamento do replay nativo

Data: 21/10/2026.
Status: especificação escrita aprovada pelo usuário em 21/10/2026; plano de implementação em revisão.

## Intenção e sucesso esperado

O usuário pediu seguir o relatório técnico de gargalos de CPU fornecido em
`C:\Users\Gusta\Downloads\deep-research-report.md`. A direção escolhida é
incremental: medir e avaliar replay, depois captura, sincronização e compilação.
Foi aprovado como primeiro subprojeto estabelecer o baseline atual e comparar
particionamentos e contagens de threads do replay, preservando imagem,
comportamento, versões de recursos e ordem de publicação.

Vulkan é a referência principal deste ciclo. D3D12 é controle de regressão do
caminho compartilhado; seu renderer direto não usa o decoder de packets deste
experimento. Ganho de replay não deve ser anunciado como ganho de D3D12.
O benchmark histórico de 720p/30 FPS permanece como referência. Esta etapa não
promete 30 ou 60 FPS e não altera o padrão de execução do jogo.

Sucesso é produzir uma comparação reproduzível que indique se intercalar trabalho
ou mudar a contagem de shards reduz a latência do replay e melhora o caminho
completo. Um resultado negativo, com dados que apontem o próximo gargalo,
também conclui o ciclo. Nenhuma promoção de configuração a padrão faz parte dele.

## Evidências e limites da hipótese

Fontes locais examinadas:

- `port/src/graphics/guest/parallel_replay.{h,cpp}`;
- integração em `Renderer::WorkerMain` e `Renderer::Execute`, em
  `port/src/native_renderer/native_renderer.cpp`;
- `tests/native/test_parallel_replay.cpp` e `replay_storage_benchmark.cpp`;
- `docs/native-vulkan-parallel-replay-results-2026-10-19.md`;
- `tools/bench/bench_api.ps1`, `bench.ps1`, `image_gate.ps1` e
  `tools/analysis/frame_timeline_report.py`.

O decoder prepara comandos sequencialmente usando o mirror PM4, depois divide
as entradas em faixas contíguas. A thread chamadora participa como shard zero.
Cada shard tem seu arena; os resultados são reunidos por índice original.
O construtor e o armazenamento das sondas têm limite quatro.

No runtime, apenas `SR_NATIVE_REPLAY_THREADS=2|4` ativa hoje o decoder paralelo,
e somente quando existe `packet_sink_`. Sem a variável, o caminho é serial
direto. Portanto suporte a quatro shards não significa paralelismo por padrão.

Os resultados de 19/10 registram ganho no benchmark isolado de lotes 256/4,
mas não ganho comprovado de FPS. No perfil daquela execução, o replay levou
aproximadamente 40 ms/quadro, incluindo cerca de 18 ms de preparação serial.
A timeline registrou Worker 42,36 ms e Record 75,02 ms, este com 5,60 ms
bloqueados; o perfil de recording atribuiu cerca de 35 ms a bindings.
São dados históricos de uma carga específica, não o baseline desta alteração.

Hipótese a testar: draws caros agrupados podem desequilibrar faixas contíguas.
Intercalar índices pode reduzir o maior tempo de shard, com possível custo
de localidade e contenção. Mais threads podem piorar o resultado. O desenho
não assume que essa hipótese explique o gargalo dominante atual.

## Escopo e limites

Incluído:

- seleção experimental de 1 a 8 shards e dois particionamentos estáticos;
- sondas por lote e por shard, agregação por quadro e relatórios de percentis;
- comparação com serial direto e preparado com uma thread;
- extensão do benchmark isolado e um executor de matriz sem substituir o bench
  histórico;
- testes de equivalência, ownership, erros e configuração;
- relatório final, configurações efetivas, hashes e dados brutos preservados.

Ficam para outros ciclos: deduplicação PM4, mudanças em hashes de texturas,
paralelização da preparação, work stealing, mudanças de ownership/queue lock,
bindings, secondary command buffers, alterações de shaders e opções de build.
As medições existentes de frontend, recording e GPU serão usadas para decidir
qual dessas frentes merece investigação posterior.

## Configuração experimental

`SR_NATIVE_REPLAY_THREADS` conta participantes totais, incluindo a thread
chamadora, e passa a aceitar os valores literais `1` a `8`, sem sinais,
espaços, sufixos ou zeros à esquerda.

| Entrada | Comportamento Vulkan |
| --- | --- |
| Ausente ou vazia | Serial direto atual |
| `1` explícito | Decoder preparado com um participante, sem thread adicional |
| `2` a `8` | Decoder preparado com chamadora e N-1 workers persistentes |
| Inválida, negativa, com sufixo ou fora do intervalo | Aviso uma vez e serial direto |

A interpretação de `1` explícito passa a distinguir o controle preparado do
serial direto. Essa diferença experimental deve constar da documentação e dos
logs. As opções existentes `2` e `4` mantêm o significado anterior.
O caminho serial direto sempre pode ser recuperado removendo a variável.

`SR_NATIVE_REPLAY_SCHEDULING=contiguous|interleaved` escolhe o particionamento
do decoder preparado. Ausência ou valor vazio usa `contiguous`; um valor
inválido gera aviso uma vez e usa `contiguous`. Essa variável sozinha não ativa
o decoder. Em D3D12, nenhuma das duas variáveis passa a ativar replay de packets.

A seleção ocorre uma vez na criação do worker, antes de criar suas threads.
O log informa API, caminho efetivo, contagem efetiva, particionamento e tamanho
efetivo dos lotes. Não registrar apenas o valor solicitado como se estivesse ativo.
O construtor do decoder recebe opções explícitas e limita sua contagem a 1–8;
ele não consulta o ambiente. Testes podem selecionar as variantes sem depender
de configuração global do processo. A assinatura existente de um argumento
permanece utilizável, com particionamento contíguo.

## Componentes e fluxo de dados

1. O runtime congela o `WorkBatch` e usa a configuração fixada na criação do worker.
2. O caminho preparado percorre comandos na ordem original, produzindo snapshots
   imutáveis com `PrepareReplayCommand`. Só essa etapa modifica o mirror PM4.
3. O decoder publica o lote preparado aos workers persistentes. Cada entrada
   pertence exatamente a um shard.
4. Cada shard conta seus draws, cria seu arena e executa
   `DecodePreparedRenderPacket` nas entradas que lhe pertencem.
5. A chamadora espera todos os workers, reúne resultados por índice original e
   transfere ownership ao consumidor.
6. O consumidor publica packets na ordem original e aplica a política atual
   de interromper publicação na primeira falha.

Para N shards e M entradas preparadas:

- contíguo: shard s processa `[M*s/N, M*(s+1)/N)`;
- intercalado: shard s processa `s, s+N, s+2N, ...`, enquanto índice < M.

A mesma regra deve servir à contagem de draws, ao decode e ao tratamento de
exceções. Não manter uma faixa contígua no caminho de erro do modo intercalado.
Não há reordenação do vetor de entradas nem do vetor retornado.

Os shards leem apenas snapshots e memória capturada imutável. Não consultam
memória guest ao vivo, não compartilham o mirror mutável e não emitem chamadas
Vulkan. Arenas e aliases mantêm o armazenamento válido após retorno, reset do
mirror, destruição do batch original e próximas chamadas do decoder.

## Falhas e ciclo de vida

- Falha de preparação preserva o contrato atual: retorna o prefixo válido e a
  entrada de falha; nenhum comando posterior é preparado ou publicado.
- Falha de decode permanece no índice original e não publica um draw parcial.
  Entradas posteriores eventualmente já decodificadas não são enviadas ao sink.
- Exceção em um shard marca suas entradas ainda não concluídas e sempre permite
  que a chamadora termine a espera. Nenhuma entrada de outro shard é alterada
  pelo tratamento dessa exceção.
- Falha ao criar workers encerra e reúne os já criados antes de propagar o erro,
  preservando a política atual de construção.
- Batch vazio não cria trabalho pendente. Shards sem entradas são válidos.
- O decoder continua com um único chamador por instância. Destruição não ocorre
  concorrentemente com `Decode`; depois de uma falha de decode retornada, a
  instância deve continuar utilizável em testes isolados.

## Contrato das medições

As sondas do decoder mantêm `wall_ns`, `prepare_ns`, `arena_ns` e os contadores
existentes. O armazenamento comporta oito shards e registra quantos estão ativos.
Cada shard escreve somente em sua sonda; a chamadora as lê após a sincronização.
Sem profiling, não há leituras adicionais de relógio, escrita de CSV ou alocação
de dados de diagnóstico por comando.

Medidas do caminho preparado:

- `wall_ns`: chamada completa do decoder, incluindo preparação, dispatch, espera,
  reunião dos resultados e limpeza interna;
- `prepare_ns`: laço sequencial de preparação;
- `parallel_ns`: intervalo da publicação do trabalho até todos os shards
  terminarem, incluindo execução da chamadora e sincronização;
- `join_wait_ns`: tempo da chamadora esperando depois de terminar seu próprio
  shard; é parte de `parallel_ns`, não um estágio adicional;
- por shard: quantidade de comandos/draws, início e fim no mesmo relógio monotônico,
  duração total, tempo de arena e tempo instrumentado de decode;
- atraso entre publicação e início de cada shard, permitindo distinguir
  desequilíbrio de trabalho de atraso de scheduling.

Tempos medidos com relógio monotônico são durações de parede. As somas existentes
chamadas `decode_cpu_sum`/`arena_cpu_sum` somam essas durações entre participantes;
não são contadores de CPU do sistema operacional. Manter seus nomes antigos por
compatibilidade de logs, mas explicar essa limitação nos novos relatórios.
Não somar tempos aninhados ou sobrepostos para estimar latência ou FPS.

O serial direto não passa pela mesma barreira de preparação: decode e entrega ao
sink se intercalam. Sua medição de decode exclui tempo do sink e deve receber
identificação própria. Comparações ponta a ponta usam o ciclo equivalente do
benchmark isolado e Worker/intervalo de quadros no jogo, sem fingir que esse
tempo é idêntico ao `wall_ns` de um lote preparado.

Com `-Timeline -TimelineDetail -Profile`, o runtime grava dados de replay em um
arquivo auxiliar cujo nome é o caminho de `SR_FRAME_TIMELINE` acrescido de
`.replay.csv`. Ele não altera o esquema do CSV de timeline existente. As linhas
contêm versão do esquema, API, caminho, configuração, sequência de lote,
quantidades de comandos/draws e sondas do lote e dos shards. A chamadora é a
única escritora; erros de gravação invalidam a evidência daquela execução.

Para associar lotes a quadros, usar a ordem do worker e o swap que fecha o quadro,
acumulando os lotes até ele. O primeiro swap estabelece a fronteira inicial:
descartar o prefixo até ele e o sufixo depois do último swap, sem inventar ids
para esses trechos. O número de swap
deve permitir cruzamento com a timeline. Emitir linhas por lote permite estudar
o scheduling; somar durações sequenciais dos lotes do mesmo quadro permite
comparações por quadro. Não obter p95/p99 de médias de 120 quadros.

O relatório usa percentis nearest-rank e informa unidade, população, quantidade
de amostras e descartes. Mostra p50/p95/p99 por lote, tamanho de lote e quadro,
com métricas de desequilíbrio e de atraso de dispatch separadas. Ao resumir shards
por quadro, distinguir soma dos máximos de cada lote de soma de durações por
identidade de shard. Não misturar essas duas medidas.

## Protocolo experimental

### Controle de carga

Usar Release idêntica, cache quente, mesma GPU, qualidade, escala, VSync e opções
de renderer. Preservar SHA-256 do executável e registrar revisão, diff local,
argumentos, ambiente efetivo, dimensões lógicas e swapchain físico. Não rodar
builds, benchmarks ou profiling concorrentes durante coleta de aceitação.

A referência histórica permanece New Game, janela lógica 1280x720, limite 30,
LSFG desligado, aquecimento de 25 s, janela de 20 s parado e 20 s com avanço
solicitado. Confirmar gameplay antes de contar amostras e preservar imagens.
O trajeto pode alcançar obstáculos; chamá-lo avanço solicitado, não movimento
livre determinístico. Registrar variações de câmera, cena e número de draws.

Se o limite de 30 ocultar diferenças, repetir finalistas em uma série separada
com limite 60. Não misturar os dois regimes nem mudar a resolução entre variantes.
O cenário de baixa resolução sugerido no relatório não é necessário neste ciclo.

### Triagem e finalistas

Primeiro usar o benchmark isolado com estado de dispositivo, bancos variáveis,
checksum e retenção dos packets anteriores. A matriz cobre serial direto,
preparado com 1 participante e 2/3/4/6/8 participantes nos dois particionamentos.
O modo de um participante só precisa ser medido uma vez, pois os índices são
iguais. Usar lote 32 como estrato principal; lote 256 é estrato diagnóstico
separado, mantendo constantes pressão de bytes e demais opções. Não atribuir
ganho ao scheduling quando o tamanho do lote mudou junto.

Cada variante tem ao menos dez execuções de triagem. Selecionar no máximo duas
finalistas para comparação no jogo com o serial direto e a configuração
contígua de mesma contagem. No jogo, dez execuções por configuração fazem a
triagem; vinte por configuração sustentam a decisão final. Alternar a ordem
das variantes em blocos balanceados e guardar a ordem efetiva. Distinguir
execuções de processo de rodadas internas do benchmark.

Executar séries instrumentadas para explicar os estágios e séries limpas para
aceitação de desempenho; as séries limpas não habilitam profile nem timeline.
Medir separadamente o custo de habilitar as sondas em controle/candidata.
Os logs periódicos de FPS do bench não fornecem percentis reais de frame time.
Para p95/p99 por quadro, usar a série de timeline e declarar seu overhead;
não transformar o mínimo de dez leituras periódicas em 1% low.

### Critérios de decisão

Uma variante só merece recomendação de uso quando:

1. Passa os contratos de correção e não introduz erros de validação ou crescimento
   de memória/falhas ao longo das repetições.
2. Reduz pelo menos 5% o replay por quadro em carga comparável, ou pelo menos 2%
   o Worker por quadro, sem piora reproduzível de p95/p99. O alvo desejável para
   mudança de scheduling é 10% no replay. Worker é uma medida do worker, não
   CPU total do frame nem soma dos estágios sobrepostos.
3. O ganho é repetido entre execuções. Apresentar diferenças pareadas por bloco
   e intervalo de confiança bootstrap de 95% sobre as execuções, com semente
   registrada. Não tratar quadros correlacionados como execuções independentes.
   Um intervalo inconclusivo não comprova ganho ou ausência de regressão.
4. A medição limpa do jogo não contradiz a conclusão sobre desempenho ponta a
   ponta. Ganho isolado sem melhora no jogo é relatado como tal.

Sem variante convincente, preservar o serial direto e encerrar a hipótese neste
ciclo. Se recording continuar dominante e os dados de bindings confirmarem seu
peso, recomendar esse próximo subprojeto; se frontend/capture dominar, priorizar
essa frente. A recomendação não autoriza implementar o próximo ciclo.

## Verificação de correção

Estender os testes existentes com ambas as políticas e 1–8 participantes:

- comparação com referência serial independente e valores literais de constantes,
  registradores, geometria e versões;
- índices não múltiplos da contagem, lote menor que a contagem, lote vazio e
  comandos de draw intercalados com pass-end, resolve e swap;
- prefixo interrompido na preparação, erro de decode em shards diferentes e
  proibição de publicação posterior à primeira falha;
- retenção de payloads após chamadas seguintes e reset/destruição dos produtores;
- conclusão das esperas e teardown, incluindo falha determinística de trabalho
  de shard por um ponto de teste interno, sem adicionar opção ao produto;
- contadores de sondas, unidades e classificação de shard vazio; não usar
  limites de tempo de performance como asserts de teste funcional;
- parsing das opções, fallback e identificação da configuração efetiva;
- relatório com amostras conhecidas, agregação lote/quadro, percentis, linhas
  incompletas e unidades. Linhas inválidas não viram amostras de tempo zero.

Rodar suítes nativa e Vulkan, testes de ferramentas afetados e fixtures GPU de
contrato global/portátil com a camada de validação realmente ativa. Preservar os
logs e não chamar uma execução sem a camada ativa de prova de validação.

O gate visual existente serve como triagem. Comparações de packets/fixtures
determinísticas sustentam equivalência; screenshots de cenas diferentes e um
gate grosseiro, sozinhos, não provam imagem bit a bit. Uma falha do gate precisa
ser resolvida ou permanecer registrada como limitação impeditiva de promoção;
não relaxar limites para fazer uma candidata passar.

## Entregáveis e conclusão

- Opções experimentais documentadas e defaults preservados.
- Decoder e integração com sondas por shard/lote, sem alteração de ordem.
- Benchmark isolado ampliado, executor de matriz e relatório de replay.
- Evidências e manifesto em diretório próprio de
  `artifacts/native-renderer-performance/`, com logs brutos em `logs/` e nomes
  únicos para evitar substituir capturas e referências anteriores.
- Documento de resultados com tabelas por carga, efeito da instrumentação,
  falhas/descartes, limitações, conclusão e recomendação do próximo gargalo.

As alterações locais preexistentes no renderer, na reciclagem de descritores,
nos testes e no bench são preservadas. A implementação deverá distinguir o
estado local de partida do patch deste ciclo no manifesto e nas comparações.
Esta especificação não inclui essas alterações no seu commit de documentação.

## Revisão e próximo passo

Autorrevisão: escopo limitado ao replay e sua medição; defaults e caminho
preparado de uma thread explícitos; somas sobrepostas separadas de latência;
erros, ownership, tamanho dos lotes e evidência visual definidos.

Após aprovação deste arquivo pelo usuário, usar a habilidade `writing-plans`
para escrever o plano de implementação. A revisão do plano e a escolha da forma
de execução antecedem alterações de implementação.
