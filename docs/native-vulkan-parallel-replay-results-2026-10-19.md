# Replay paralelo — 19/10/2026

Meta: 60 quadros reais por segundo, render nativo Vulkan, escala 1, LSFG
desligado. Continuação de [constantes compactas](native-vulkan-compact-replay-results-2026-10-19.md).

## Implementação

O caminho serial permanece padrão. `SR_NATIVE_REPLAY_THREADS=2|4` ativa um
decoder persistente com o caller e mais 1 ou 3 threads. O mirror PM4 continua
serial: cada comando recebe estado imutável compacto com 192 palavras de
fetch, 8 bools, 32 loops, seus bits de escrita, 4 registradores de polygon
offset, 5 de scissor e dois bancos de floats normalizados compartilhados.
Não copiar o mirror completo nem consultar memória guest nas threads.

Cada thread constrói um arena e decodifica uma faixa contígua do lote. Todas
terminam antes da publicação; o sink recebe os packets na ordem original.
Uploads, resolves, versões de recursos e chamadas Vulkan continuam em sua
ordem anterior. Capturas e aliases dos arenas mantêm ownership após retorno.
Falhas retornam na posição original e o runtime bloqueia publicação posterior.

`SR_NATIVE_BATCH_COMMANDS=128|256` permite comparar lotes maiores. O padrão
continua 32; o limite de pressão de bytes de 4 MiB e o flush explícito de swap
permanecem. Lotes maiores podem aumentar latência e prejudicar a sobreposição.
Nenhuma opção experimental foi habilitada como padrão por este trabalho.

O perfil distingue tempo serial de preparação e somas de CPU das threads.
O tempo de parede de todo o lote aparece em replay. Não somar essas medidas
como se fossem estágios sequenciais, nem interpretar soma de CPU como latência.

## Testes

O primeiro build de teste falhou pela API ainda inexistente (RED preservado).
195 testes nativos passaram, incluindo comparação serial/paralela com valores
literais de constantes, mudança de bancos, máscara de escrita, estado de
dispositivo e fallback, overlays, índices normalizados, geometria, ownership
entre chamadas e falhas sem publicação de draw parcial. CTest nativo 2/2 e
Vulkan 1/1; 108 testes Vulkan passaram.

GPU global e contrato portátil: validação ativa, zero erros e comparações de
pixels aprovadas. A primeira tentativa global não encontrou a layer porque
VK_LAYER_PATH não estava configurado; ela falhou antes de executar a prova.
O log foi preservado e a repetição apontou para a layer local já existente.
Não houve instalação nem alteração de configuração global do sistema.

Revisão estática independente não encontrou achados acionáveis em snapshots,
concorrência, ordem, erros, teardown ou batching. Não houve race sanitizer ou
injeção determinística de falha de alocação/criação de thread.

## Medição isolada

Release, pool desligado, 3008 draws, bancos VS/PS variando em proporções
aproximadas ao jogo, dois conjuntos de packets vivos, 10 rodadas descartando
duas de aquecimento. O teste inclui estado capturado de dispositivo, mas não
é um quadro completo do jogo. Medianas em ms, incluindo descarte anterior:

| Comandos/lote | Serial direto | Preparado, 1 thread | 2 threads | 4 threads |
| --- | ---: | ---: | ---: | ---: |
| 32 | 43,0401 | 45,1742 | 44,6916 | 42,5541 |
| 128 | 41,2868 | 42,4931 | 42,2156 | 38,2073 |
| 256 | 44,4065 | 41,6031 | 41,4900 | 36,7813 |

Todos os checksums foram 31793585826180. Na configuração 256/4, preparação
mais decode caiu de 33,8132 para 25,2326 ms; o ciclo completo caiu cerca de
17%. Lotes de 32 não demonstram benefício relevante e a variante sem estado
de dispositivo chegou a ficar mais lenta com quatro threads. Não extrapolar
essas diferenças para FPS, nem compará-las a cargas antigas distintas.

Evidências: `logs/native60_parallel_replay_*`, especialmente
`native60_parallel_replay_device_perf.log` e planos desta data.

## Jogo

Build final concluído. Executável e snapshot `parallel_replay_after.exe`:
SHA-256 `6C0C2CD6164A7B1526E77C4D3642EA2918B3C61B0E3F77AF8E667A29753EB330`.
O anterior de constantes compactas permanece preservado separadamente.

Vulkan nativo, contrato global=1, escala 1, MSAA 1, LSFG desligado, limite 60,
janela lógica 1280×720 e swapchain físico 1600×900. Sem builds concorrentes.

| Configuração neste executável | FPS parado | Avanço solicitado | Mínimo no avanço |
| --- | ---: | ---: | ---: |
| 4 threads, lote 256, perfil detalhado | 11,03 | 10,41 | 9,2 |
| Serial direto, lote 32, sem perfil/timeline | 10,59 | 11,33 | 10,7 |
| 4 threads, lote 256, sem perfil/timeline | 11,34 | 11,25 | 9,7 |

Rodadas limpas de controle concluídas, com o mesmo SHA do executável e zero
mensagens de perfil detalhado nas duas rodadas limpas. As janelas de 20 segundos registraram
10 amostras cada. O personagem, a cidade e o HUD foram observados. O comando de
avanço alcançou uma parede: esse cenário não representa movimento livre durante
toda a janela. A câmera ociosa e o trajeto não são determinísticos. Não tratar
diferenças pequenas dessas rodadas como ganho causal.

No perfil próximo ao percurso: replay de parede ~40 ms/quadro, preparação
serial ~18 ms; decode somou ~34 ms de CPU e arenas ~31 ms de CPU entre threads.
Essas somas se sobrepõem e não são latências que possam ser somadas ao replay.
A gravação Vulkan permaneceu ~71 ms, com ~35 ms em bindings. A leitura inicial
é que a paralelização do replay não removeu o gargalo dominante.

Timeline recortada no avanço da rodada instrumentada: Frontend 44,95 ms,
Worker 42,36 ms, Record 75,02 ms (inclui 5,60 ms bloqueados), GPU real 14,57 ms.
Os estágios se sobrepõem. Fonte: `native60_parallel_replay_forward_timeline.json`.

**Não houve ganho de FPS comprovado.** A rodada limpa de avanço ficou
praticamente igual ao controle serial; não habilitar o experimento por padrão.
A variação parada também não demonstra ganho causal, dadas as câmeras distintas.
O perfil indica 18 ms seriais dentro do próprio replay e cerca de 35 ms de
bindings na gravação: aumentar a quantidade de threads de decode não resolve
esses limites. A próxima investigação deve se concentrar na preparação de
recursos/bindings e no frontend, respeitando ordem, versões e aliases.

Executáveis preservados, fontes e evidências copiadas com SHA-256 em
`artifacts/native-renderer-performance/native60_2026-10-19_parallel_replay/manifest.json`.
Nenhum commit, push, PR ou publicação realizado.

A meta de 60 FPS permanece pendente. A gravação Vulkan e o frontend também
precisam reduzir custo; replay paralelo sozinho não resolve esse orçamento.
