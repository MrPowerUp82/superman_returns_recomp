# Meta de 60 FPS reais em Vulkan — 2026-10-16

Meta solicitada: atingir 60 FPS neste computador com o renderer nativo Vulkan.
Critério adotado: quadros reais do jogo, frame generation desligado, preservando
imagem, ordem dos draws e correção do estado guest. Meta ainda não atingida.
Primeira cena de referência: New Game, 20 s parado e 20 s andando, HUD validado.
Essa cena não cobre todas as fases nem constitui replay determinístico.

## Máquina e executável

- Intel Core i7-10750H, 6 núcleos / 12 threads; aproximadamente 16 GB de RAM.
- NVIDIA RTX 2060, driver 617.14; Intel UHD também instalada.
- Log confirma seleção da RTX 2060 no Vulkan nativo.
- Plano Windows Equilibrado, CPU máxima 100%, boost agressivo em AC/DC.
  Nenhuma configuração de energia alterada. Uma leitura de frequência nominal
  não estabelece frequência efetiva nem limitação térmica.
- Release, `-O3 -DNDEBUG`, ThinLTO ligado no CMakeCache local.
- Executável: `port/out/build/win-amd64-release/superman_returns.exe`.
- SHA-256: `FC4A158A5CCE7B9E5576AC449BB2BCC59448C46D21ADF274269183C626A6E1EC`.

## Baseline atual

`native60_baseline`: limite 60 (log confirma vblank 120 Hz), vsync ligado,
1280x720 solicitado, escala interna 1, MSAA 1, `sr_lsfg=false`.
A swapchain iniciou em 1600x900; distinguir dimensões solicitadas e efetivas.
Somente timeline ligada; profiling detalhado desligado. Não é uma rodada
completamente sem instrumentação. Não houve build/testes pesados concorrentes.

| Cenário | Média FPS | Mínimo das amostras FPS | CPU cores | GPU 3D Windows |
| --- | ---: | ---: | ---: | ---: |
| Parado | 13,2 | 10,2 | 6,8 | 46% |
| Andando | 12,4 | 10,0 | 6,9 | 45% |

Últimos 400 quadros da timeline: intervalo 75,5 ms (13,2 FPS), 1% low 8,5 FPS.
Estágios médios: game 74,82 ms, captura 10,93 ms, worker 41,53 ms,
record 60,05 ms, GPU real 9,26 ms, GPU idle 66,26 ms.
Estágios se sobrepõem; não somar os tempos. `game` inclui tempo fora do renderer
e seu predomínio não prova sozinho trabalho exclusivo de CPU.

A GPU real ficou abaixo do orçamento de 16,67 ms nessa amostra (p99 11,52 ms).
Isso sustenta priorizar CPU, mas não garante 60 FPS em outras cenas nem inclui
todo o trabalho da composição/apresentação. O contador `presented_fps` inclui
repetições; não serve como critério de cumprimento da meta.

## Perfil detalhado

`native60_profile`: mesmas opções, acrescentando `SR_VULKAN_PROFILE=1`,
`SR_NATIVE_GUEST_CPU_PROFILE=1` e `SR_FRAME_TIMELINE_DETAIL=1`.
Parado 11,2 FPS, andando 12,7 FPS. Excluir da aceitação de ganho de desempenho.

Últimas duas janelas de 120 quadros: 2814/2534 draws, bindings 18,4/15,7 ms,
descritores 15,3/12,7 ms, gravação total 69,0/57,2 ms. Decode do replay
29,92/24,86 ms e inicialização da arena 13,33/10,75 ms.
Cache de descritores: 2629/2378 hits e 184/156 misses por quadro; uploads de
texturas quase zero. Não há evidência de compilação de shaders dominando gameplay.

Últimos 400 quadros: frontend 37,01 ms, incluindo fim de captura 17,66 ms,
streams 7,15 ms e device 4,87 ms. `game_guest` residual 48,81 ms inclui
trabalho não instrumentado, esperas e desagendamento; não é CPU exclusiva PPC.
Worker 45,50 ms, record 63,39 ms e GPU real 10,43 ms.

Os custos estão distribuídos. Repetir os caches descartados em 09–14/10 sem
nova hipótese não é um caminho demonstrado até 60 FPS.

## Evidências e continuidade

- Logs `logs/bench_native60_{baseline,profile}.log`.
- Timelines `logs/timeline_native60_{baseline,profile}.csv`.
- FPS e argumentos completos em `logs/bench_results.csv`.
- Imagens `logs/bench_native60_{baseline,profile}_{start,idle,forward}.png`.
- Gameplay da baseline inspecionado: Superman, cenário, HUD e minimapa presentes.
- Relatório: `python tools/analysis/frame_timeline_report.py <csv> --last 400 --budget-ms 16.6667`.

Teste `native60_yield`: somente `SR_NATIVE_IDLE_YIELD=1`, opção existente e
desligada por padrão, com a mesma timeline simples da baseline. Mesmo executável
e demais opções. Parado 10,6 FPS (mínimo 9,1), andando 11,7 FPS (mínimo 8,7),
4,3 cores nos dois cenários, GPU 3D 44%/41%. A menor ocupação de CPU não se
traduziu em ganho de FPS nessa rodada. Não selecionar o flag como otimização
nem afirmar regressão causal sem repetições. Não foi alterado o padrão.
O ambiente foi aplicado somente ao processo PowerShell do teste.

Gate da imagem inicial contra baseline: PASS, PSNR 35,69 dB, histograma
0,00662; critérios originais. Evidência `logs/native60_yield_image_gate.json`,
log `logs/bench_native60_yield.log`, timeline `logs/timeline_native60_yield.csv`
e screenshots correspondentes. HUD também validado ao fim do movimento.

Próximo passo: investigar redução estrutural do volume
de estado por draw e de sua reconstrução, medindo separadamente arena/decode,
bindings/descritores e captura. Os ensaios antigos de bancos por versão só
reduziram upload; não eliminaram o pacote completo nem demonstraram ganho de FPS.

## Investigação seguinte: reutilização da alocação de pacotes

Revalidado o estado do checkout: nenhum renderer alterado desde a baseline;
nenhum jogo/build em execução. O turno anterior gerou progresso por estabelecer
a medição a 60 FPS e rejeitar a hipótese de espera ativa como ganho demonstrado.

O construtor de `DrawPacketArena` reserva um `vector<DrawPacket>` novo por lote.
Os aliases `shared_ptr` mantêm todo o bloco vivo até o último consumidor.
`GameFrame::RecordFrame`/`RecorderMain` liberam os pacotes depois da gravação;
os recursos GPU têm retenção própria até a fence. O pool existente de WorkBatch
não recicla a alocação de DrawPacket e não resolve esse custo.

Build atual do diagnóstico reconferido com CMake: `ninja: no work to do`.
Três processos Release consecutivos, 3.008 draws, oito rodadas medidas após
aquecimento, sem jogo/build concorrentes:

| Processo | Arena ms | Decode ms | Total ms |
| --- | ---: | ---: | ---: |
| 1 | 21,5608 | 10,8867 | 32,6406 |
| 2 | 20,8308 | 11,0020 | 31,9777 |
| 3 | 21,3926 | 10,7459 | 32,4377 |

Checksum igual: 45255360. Logs `logs/native60_arena_before_micro{1,2,3}.log`.
Esse diagnóstico não reproduz os streams/device do gameplay; não traduzir os
tempos em ganho de FPS. O contador arena agrega reserva e inicialização, portanto
esses dados ainda não separam o custo da alocação do custo de escrever os campos.

Mudança delimitada proposta: pool apenas da memória de backing dos pacotes,
reconstruindo os objetos com os construtores atuais em cada aquisição. Devolver
o bloco somente no último release, destruindo os objetos e liberando suas
capturas antes de reter a capacidade. Limite de 128 MiB de capacidade ociosa e
oito blocos; excedentes seguem a liberação normal. O pool precisa sobreviver a
aliases liberados depois do worker, como o padrão de CaptureBatchPool.
Não dispensar inicialização de campos nem mudar snapshots, shaders ou fences.

Verificação proposta: reutilização após último alias, não reutilização durante
consumo, liberação tardia depois do dono, reset de dados/recursos, teto do pool,
falhas de decode e equivalência de pacotes. Medir criação/release juntos e
separadamente para não esconder trabalho transferido ao recorder. Benchmark
com dois lotes simultaneamente vivos, seguido de gameplay alternando pool
ligado/desligado no mesmo executável, perfis e gates de imagem. Só selecionar
como otimização se houver evidência útil; isso é uma etapa rumo à meta de 60 FPS,
não uma promessa de que esse ajuste isolado será suficiente.

Desenho aprovado pelo usuário. Implementação experimental em validação:
`SR_NATIVE_REPLAY_POOL=1` liga o pool; sem a variável ele permanece desligado.
O log de inicialização informa a escolha. A meta de 60 FPS continua pendente.

### Implementação e diagnóstico inicial

O deleter do último alias limpa os pacotes antes de bloquear o pool. O pool
retém vetores vazios, procura a menor capacidade suficiente e expulsa capacidades
ociosas antigas quando necessário para manter os limites. A primeira versão
usou 128 MiB/oito blocos; a revisão descrita abaixo aumenta somente a quantidade
de blocos para 128, mantendo 128 MiB. Objetos são
reconstruídos com os construtores anteriores; campos parcialmente preenchidos
mantêm sua inicialização. Aliases capturam o pool e podem terminar depois do
worker. O orçamento é de capacidade ociosa; não limita os pacotes em uso.

RED: o teste de vida útil do pool falhou e o orçamento detectou 699.983 bytes
alocados para 32 draws aquecidos no caminho sem reutilização. GREEN: 183 testes
nativos, orçamento e CTest 2/2 passaram. Reutilização aquecida aloca 40 bytes de
controle para esses 32 draws; nenhuma nova alocação do tamanho dos payloads.
O orçamento também verifica blocos grandes demais, limite zero de blocos e
limites independentes de bytes/blocos, com dois lotes concorrentes.

Revisão independente não encontrou defeitos de produção; apontou que o teste
de reset podia selecionar um bloco limpo. Corrigido para adquirir e conferir
ambos os blocos. O teste de equivalência do replay agora recicla armazenamento
previamente sujo e verifica os bancos/metadados, shaders e inline data, além de
falhas precoces e tardias sem substituir o pacote publicado.

91 testes Vulkan, fixture de frame assíncrono e três variantes de pixels de
bindings de produção passaram. Validation layers indisponíveis: GPU validation
não executada; contador de erros zero não implica validação com layers.

Diagnóstico atualizado mantém dois lotes vivos e mede também a liberação final
do anterior. Mesma build Release, ordem OFF/ON/ON/OFF/OFF/ON, sem build ou jogo
concorrente. 3.008 draws, duas rodadas de aquecimento e oito medidas por processo:

| Pool | Arena ms | Decode ms | Liberação ms | Ciclo ms |
| --- | ---: | ---: | ---: | ---: |
| OFF 1 | 25,2154 | 10,9698 | 7,7963 | 44,3012 |
| ON 1 | 5,0328 | 8,1837 | 0,9424 | 14,5653 |
| ON 2 | 5,7738 | 8,4748 | 0,9948 | 15,4063 |
| OFF 2 | 26,3854 | 11,7956 | 7,9676 | 46,4723 |
| OFF 3 | 26,2450 | 12,1729 | 8,7006 | 49,2895 |
| ON 3 | 4,5296 | 7,8340 | 0,9123 | 13,4916 |

Checksum 45255360 em todos os processos. Medianas de fases não precisam somar
à mediana do ciclo, que também inclui a leitura do checksum. Boot e destruição
final do pool ficam fora do ciclo aquecido. Essa amostra sem streams/device
não reproduz toda a carga do jogo nem comprova aumento de FPS.
Logs `logs/native60_pool_{off,on}_micro{1,2,3}.log`.

A timeline atual do recorder termina antes de limpar os pacotes, portanto a
liberação precisa ser considerada pelo diagnóstico completo e pelo FPS real;
não concluir ganho apenas pela redução do contador de arena.

### Gameplay da primeira versão e correção do modelo de lotes

Quatro rodadas limpas no mesmo executável experimental de oito blocos, sem
timeline nem profiling detalhado. Vulkan nativo, RTX 2060, limite60, FG desligado,
demais opções iguais à baseline. Sem builds/testes pesados concorrentes:

| Rodada | Parado FPS | Andando FPS | CPU cores parado/andando |
| --- | ---: | ---: | ---: |
| `native60_pool_off_clean1` | 15,6 | 15,0 | 6,9 / 6,9 |
| `native60_pool_on_clean1` | 12,7 | 14,6 | 6,8 / 6,7 |
| `native60_pool_on_clean2` | 14,1 | 14,0 | 6,8 / 6,9 |
| `native60_pool_off_clean2` | 13,4 | 13,0 | 6,8 / 6,7 |

Isso não demonstra ganho estável de FPS. As cenas variam, inclusive entre as
referências. Três gates de imagem contra OFF1 passaram: ON1 37,61 dB/0,00845;
ON2 35,73/0,01149; OFF2 37,99/0,00637. HUD também validado no fim do movimento.
Evidência `logs/native60_pool8_image_gates.json`.
Binário preservado em `artifacts/native-renderer-performance/pool8_candidate.exe`,
SHA256 `CB3D6AD269624954084F6BB32464656F5077CB1584B45E4735968A06BD67DECC`.

Identificada uma hipótese concreta para a divergência do teste isolado:
`Renderer::EndCmd` chama `FlushBatch` a cada 32 comandos ou 4 MiB. Portanto,
3.008 draws são distribuídos em muitos lotes, todos vivos até o recorder consumir
o quadro. O diagnóstico inicial com uma arena grande por quadro não reproduzia
esse fluxo. Oito blocos pequenos retêm apenas uma fração do quadro.

Diagnóstico agora aceita `--batched` (32 draws por lote) e `--pool-blocks=N`,
mantendo dois quadros vivos. Mesma build, sem build/jogo concorrente:

| Configuração | Arena ms | Decode ms | Liberação ms | Ciclo ms |
| --- | ---: | ---: | ---: | ---: |
| OFF | 23,2126 | 9,9701 | 9,0704 | 42,4201 |
| ON, oito blocos | 21,8670 | 10,0547 | 8,1540 | 40,4339 |
| ON, 128 blocos | 5,5404 | 7,1521 | 1,1385 | 14,1528 |

Checksum45255360 nos três casos. Logs
`logs/native60_pool_batched{_off,8,128}_micro1.log`. Mesmo esse diagnóstico não
inclui device/streams e precisa de validação real de FPS.

Regressão de alocações para 64 lotes de 32 draws falhou primeiro com o limite oito:
39.200.712 bytes alocados no quadro aquecido. Com 128 blocos passou; CTest nativo
2/2 novamente aprovado. O limite de bytes e a liberação no último alias continuam
iguais. Revisão independente do ajuste não encontrou defeitos de produção e
apontou default8 no diagnóstico; corrigido para128, mantendo override explícito
para reproduzir a versão antiga. A nova comparação de gameplay está abaixo.

### Resultado com 128 blocos

Executável atual e preservado em
`artifacts/native-renderer-performance/pool128_candidate.exe`, SHA256
`A20C2C511794434AD963F205710BDEA1BF25AD0D57BF1E4160633BB8754B0886`.
As seis rodadas abaixo e a repetição usaram esse mesmo hash. A distribuição não
foi substituída. `SR_NATIVE_REPLAY_POOL` continua **desligado por padrão**.
O código permanece disponível para experimentos; não foi selecionado como
solução de FPS geral nem como cumprimento da meta de 60 FPS.

Repetições do diagnóstico com lotes de 32, fora de builds/gameplay: OFF
39,7724/41,5590 ms por ciclo, ON com oito blocos 37,9724 ms, ON com 128 blocos
11,3816/11,5468 ms. Checksum igual em todos. Logs
`logs/native60_pool128_batched{_off,8,_on}_micro{2,3}.log` (as combinações
executadas estão salvas; não há rodada oito-blocos número 3).

Quatro rodadas limpas, sem timeline/profiling e sem builds/testes concorrentes,
com os mesmos parâmetros de gameplay das rodadas anteriores:

| Rodada | Parado FPS | Andando FPS | CPU cores parado/andando |
| --- | ---: | ---: | ---: |
| `native60_pool128_off_clean1` | 13,7 | 14,5 | 7,1 / 6,9 |
| `native60_pool128_on_clean1` | 14,0 | 14,5 | 6,9 / 7,0 |
| `native60_pool128_on_clean2` | 16,1 | 14,7 | 6,9 / 6,9 |
| `native60_pool128_off_clean2` | 12,8 | 14,6 | 6,4 / 7,0 |

Não há ganho estável demonstrado no movimento. A diferença parado exige
repetições com cena mais controlada antes de atribuição causal. A referência
OFF2 teve mínimo de amostras 7,6 FPS; os demais mínimos parado foram
12,3/12,2/13,3. Esses mínimos são de amostras de FPS, não de frame times.

Perfis adicionais (excluídos da aceitação de FPS): OFF 13,5/15,4 FPS, ON
16,3/14,6 e ON repetido 14,9/15,5. Últimas janelas instrumentadas de 120 quadros:

| Pool | Draws/quadro | Arena ms/quadro | Decode ms/quadro | Record ms/quadro |
| --- | ---: | ---: | ---: | ---: |
| OFF | 2681 | 10,18 | 22,42 | 55,0 |
| OFF | 2513 | 9,05 | 20,49 | 48,6 |
| ON | 2375 | 6,81 | 20,07 | 50,0 |
| ON | 2918 | 8,66 | 25,05 | 64,4 |

Arena aproximadamente 3,60–3,80 µs/draw OFF contra 2,87–2,97 ON nessas janelas.
É evidência diagnóstica consistente com o menor trabalho de alocação; cenas e
tempos de desagendamento variáveis impedem tratar as diferenças brutas como
um A/B causal exato de gameplay. O custo isolado não se converteu em aumento
estável de FPS em movimento.

Timeline dos últimos 400 quadros, ms médios:

| Pool | Intervalo | Game | Captura | Worker | Record | GPU real |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| OFF | 68,1 | 67,21 | 9,48 | 37,32 | 53,45 | 9,24 |
| ON | 65,1 | 63,91 | 8,77 | 33,02 | 53,21 | 8,75 |
| ON repetido | 70,1 | 68,76 | 9,28 | 36,61 | 57,39 | 8,46 |

Etapas se sobrepõem. Não somar seus tempos; `game` também inclui esperas e
trabalho fora do renderer. A arena não é o único estágio acima de 16,67 ms.
Gravação Vulkan, replay e frontend/guest precisam de investigação adicional.

Validação visual com os limites originais contra OFF1:

- Três comparações limpas passaram: ON1 33,45 dB / histograma 0,02105;
  ON2 34,25 / 0,01376; OFF2 36,42 / 0,02211.
- Perfil OFF passou: 35,27 / 0,02157.
- Primeiro perfil ON **falhou**: 22,54 / 0,27016; região do personagem com
  diferença média 31,7 > 20 e cromaticidade 0,290 > 0,05. Imagem inspecionada:
  personagem virado e efeito radial ausente na referência. Não alterar limites
  nem apagar essa falha; não há prova causal de que seja defeito do pool.
- Repetição ON passou: 20,55 / 0,26098. Inspeção confirmou personagem de costas,
  HUD e cenário, mas câmera aponta para outra rua. O gate cobre os critérios
  definidos, não equivalência de toda a cena. O passe da repetição não transforma
  a execução anterior em PASS nem prova validação global de imagem.

As capturas também incluem uma faixa do desktop/título e recortam parte da
janela. Investigar captura/região/DPI e controle de mouse/câmera no benchmark
antes de exigir comparação determinística. Não atribuir a variação inteira
somente ao mouse sem um teste controlado.

Arquivos: `logs/native60_pool128_{clean,profile}_image_gates.json`,
`logs/native60_pool128_repeat_image_gate.json`, logs/screenshots de cada rodada,
timelines CSV/TXT e `logs/bench_results.csv`. Helper local
`build/bench_native60_pool.ps1` salva o hash, confere ausência de builds e restaura
as variáveis de ambiente. Exemplos:

```powershell
build/bench_native60_pool.ps1 -Name native60_pool128_off_clean1
build/bench_native60_pool.ps1 -Name native60_pool128_on_clean1 -Pool
build/bench_native60_pool.ps1 -Name native60_pool128_on_profile -Pool -Profile
build/tests-native/sr_replay_storage_bench.exe --batched --pool
```

Verificação final: 183 testes nativos e orçamento de alocações, CTest 2/2,
91 Vulkan e três variantes de pixels de produção passaram. O teste de liberação
tardia usa uma thread real; CMake agora liga `Threads::Threads` explicitamente
para manter o harness portátil. Camadas de validação continuam indisponíveis.
Builds e verificações `build/native60_pool128_*` e `logs/native60_pool128_*`.

Nenhum commit/publicação nesta etapa. Próximo trabalho rumo à meta: tornar a
referência de gameplay mais reproduzível e investigar os custos de gravação e
de captura/guest que continuam acima do orçamento. Os 60 FPS permanecem pendentes.

## Revisão da estratégia após o feedback do usuário

O usuário apontou que a abordagem incremental atual demoraria meses. Priorizar
novos caches locais e longas sequências de builds/benchmarks sem uma hipótese
para reduzir o caminho crítico não atende à urgência da meta.

Evidência do limite da abordagem: `logs/timeline_native60_pool128_off_profile.txt:11`
mede record 53,45 ms e `:13` GPU real 9,24 ms. Com pool,
`logs/timeline_native60_pool128_on_profile.txt:11` ainda mede record 53,21 ms.
Mesmo eliminar o custo inteiro da arena não faria esse recorder atingir
16,67 ms. Essas etapas não devem ser somadas; cada estágio serial precisa
sustentar a cadência solicitada. O estágio game inclui trabalho e esperas e
precisa de atribuição de CPU antes de concluir sua causa.

Alvo estrutural confirmado no código: o hook de DrawIndexedVertices ainda
executa o XDK recompilado antes da captura (`native_hooks.cpp:403–411`).
`Renderer::EndCmd` captura/analisa PM4 (`native_renderer.cpp:6400–6436`) e
`Renderer::Execute` o reproduz novamente (`:6772–6777`), antes de o Vulkan
reconstruir bindings/descritores (`graphics/vulkan/game_renderer.cpp:242–244`).
Isso identifica trabalho duplicado a investigar; não prova que seja seguro
remover as chamadas originais, que também atualizam estado guest necessário.

A próxima investigação deve atribuir CPU ao XDK original e às etapas nativas
numa cena controlada e avaliar um caminho direto de estado nativo/alterações de
estado. Preservar efeitos guest, sincronização, snapshots e imagem. A prioridade
é uma redução de várias vezes nos estágios acima do orçamento, não mais uma
economia local de alocação. Protótipos com trabalho gráfico omitido só podem
estabelecer limites diagnósticos; seus FPS não contam para a meta.

Builds completos e rodadas extensas devem vir depois de uma prova representativa
de redução do estágio visado e de correção. Evitar repetir comparação de FPS
com câmera diferente para inferir ganhos pequenos. O critério final continua
sendo 60 quadros reais renderizados por segundo no Vulkan nativo.

Continuação em 17/10: a sonda completa atribuiu 7,96% às chamadas gráficas
originais e 41,97% à captura nativa. A prova independente com tabela global e
3.000 draws indiretos passou no readback de pixels da RTX 2060, inclusive com
slots esparsos acima de 32. Isso demonstra viabilidade técnica, não ganho no
jogo. A rodada instrumentada ficou em 13,5/13,4 FPS. Detalhes, limites da prova
e correção do build dos microbenchmarks em
[native-vulkan-batch-feasibility-2026-10-17.md](native-vulkan-batch-feasibility-2026-10-17.md).
