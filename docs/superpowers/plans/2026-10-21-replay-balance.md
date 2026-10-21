# Replay Balance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** medir e comparar replay serial, preparado e intercalado com 1–8 participantes, preservando a imagem e a ordem e identificando o próximo gargalo com evidência reproduzível.

**Architecture:** a preparação PM4 permanece sequencial; workers persistentes decodificam snapshots imutáveis por uma política estática selecionável. A chamadora reúne resultados por índice e grava sondas por lote/shard; ferramentas independentes agregam quadros e comparam execuções. Serial direto continua padrão e a coleta de diagnóstico fica desligada nas rodadas limpas.

**Tech Stack:** C++20 nos testes/C++23 no port, CMake/Ninja, Clang/MSVC no Windows, PowerShell, Python/pytest e fixtures Vulkan existentes; sem novas dependências.

**Spec:** `docs/superpowers/specs/2026-10-21-replay-balance-design.md`, aprovada em conversa em 21/10/2026.

## Global Constraints

- Vulkan é a referência principal deste ciclo; D3D12 é controle de regressão do caminho compartilhado.
- Nenhuma promoção de configuração a padrão faz parte dele.
- Os shards leem apenas snapshots e memória capturada imutável.
- Não há reordenação do vetor de entradas nem do vetor retornado.
- O decoder continua com um único chamador por instância.
- `SR_NATIVE_REPLAY_THREADS` aceita somente os literais `1` a `8`; ausente/vazio/inválido preserva serial direto.
- `1` explícito seleciona preparado sem worker adicional; N conta a chamadora e N-1 workers.
- `SR_NATIVE_REPLAY_SCHEDULING=contiguous|interleaved`; fallback contíguo; sozinho não ativa replay preparado.
- Serialização nova não altera o esquema de `SR_FRAME_TIMELINE` nem os campos de log existentes.
- Sem profiling, não há leituras adicionais de relógio, escrita de CSV ou alocação de dados de diagnóstico por comando.
- Os tempos chamados `decode_cpu_sum`/`arena_cpu_sum` são somas de durações de parede, não CPU do SO.
- Falhas de preparação/decode permanecem na posição original e impedem publicação posterior.
- Preservar alterações locais, executáveis anteriores, shaders gerados, caches e dados do jogo.
- Nenhuma instalação, publicação, push ou implementação do próximo ciclo faz parte deste plano.

## Review Focus

- Ambiente herdado com `01`, espaços, sufixos, ou scheduling sem threads: fallback e configuração efetiva devem ser inequívocos — tarefa 1.
- Batch menor que oito shards e exceção em índice intercalado: ninguém fica preso no join nem escreve no shard vizinho — tarefas 1 e 3.
- CSV truncado, swap ausente/duplicado ou falha de disco: descartar trechos incompletos e invalidar evidência corrompida, sem fabricar zeros — tarefas 2 e 4.
- Warm-up/menu e janelas idle/forward dentro do mesmo processo: percentis devem usar somente quadros completos da janela correta — tarefas 3–5.
- Ambiente/locale/execução interrompida e rótulo já usado: restaurar variáveis, preservar logs, não sobrescrever referência nem fechar um jogo preexistente — tarefa 5.

## Estrutura dos arquivos

| Arquivo | Responsabilidade |
| --- | --- |
| `port/src/graphics/guest/replay_options.h` (novo) | Tipos e parsing puro das opções |
| `port/src/graphics/guest/parallel_replay.{h,cpp}` | Particionamento, ownership e sondas dos workers |
| `port/src/graphics/guest/replay_trace.{h,cpp}` (novos) | Esquema CSV e escrita testável sem runtime |
| `port/src/native_renderer/native_renderer.cpp` | Seleção única das opções, execução e coleta serial/preparada |
| `tests/native/test_replay_options.cpp`, `test_replay_trace.cpp` (novos) e `test_parallel_replay.cpp` | Contratos puros e concorrência |
| `port/CMakeLists.txt`, `tests/native/CMakeLists.txt` | Fontes e hook exclusivo dos testes |
| `tests/native/replay_storage_benchmark.cpp` | Matriz isolada equivalente e saída por rodada |
| `tools/analysis/replay_balance_report.py` (novo) | Leitura, percentis, agregação e comparação pareada |
| `tools/bench/cpu_matrix.ps1` (novo) | Manifesto, ordem balanceada e execução serial da matriz |
| `tools/bench/bench.ps1`, `bench_api.ps1` | Metadados opcionais das janelas, mantendo defaults existentes |
| `tools/bench/bench_window_metadata.ps1` (novo) | Conversão e validação pura dos limites UTC |
| `tests/tools/test_replay_balance_report.py`, `test_cpu_matrix.ps1`, `test_bench_window_metadata.ps1` (novos) | Provas sintéticas e mocks, sem abrir jogo |
| `tools/README.md`, documento de resultados novo | Uso, interpretação e conclusão |

## Ambiente de execução e comandos

Os comandos abaixo partem da raiz do checkout. Compilação deve usar um Developer
PowerShell x64 do VS Build Tools com o Clang local e CMake/Ninja do VS no PATH.
`cmake` e `ctest` não estavam no PATH do shell comum durante o planejamento.
Não mudar o compilador de um cache existente para contornar isso.

Os caches locais examinados são Release/Ninja em `build/tests-native`,
`build/tests-vulkan` e `port/out/build/win-amd64-release`. O jogo usa ThinLTO
ligado e LSFG desligado. A execução deve conferir os caches de novo e preservar
também a configuração do SDK de write tracking. Em outro checkout, configure
diretórios equivalentes com esses valores e as mesmas fontes de SDK/shaders.

Verificações recorrentes, executadas sequencialmente:

```powershell
cmake --build build/tests-native --target sr_native_tests sr_capture_budget sr_replay_storage_bench --parallel 4
ctest --test-dir build/tests-native --output-on-failure
cmake --build build/tests-vulkan --parallel 4
ctest --test-dir build/tests-vulkan --output-on-failure
python -m pytest tests/tools -q
```

O harness nativo executa a suíte inteira, sem filtro por nome. Esperado: código
zero, `0 failed checks` e todos os testes CTest aprovados. Uma compilação RED
por API nova ausente é válida; documentar a mensagem, sem confundir falha de
toolchain com teste que reproduz o comportamento pretendido.

## Task 0: Preservar o estado de partida

**Files:** manifesto e snapshots em `artifacts/native-renderer-performance/replay_balance_2026-10-21_<id>/`.

**Interfaces:** produz `baseline/manifest.json`, `baseline/working-tree.patch`, lista de untracked relevantes, executável/MAP/PDB/DLL e hashes; nenhuma mudança de produto.

- [ ] **Step 1:** antes da implementação, ler a habilidade de execução escolhida e a de worktrees. Se usar checkout isolado, reproduzir nele o estado local completo de partida, inclusive alterações não commitadas relevantes; não trocar silenciosamente pelo HEAD limpo. Preservar o checkout original.
- [ ] **Step 2:** registrar `git status --porcelain=v1`, revisão, diff binário, arquivos untracked relevantes e caches CMake. Os arquivos preexistentes alterados são `native_renderer.{h,cpp}`, `tests/native/CMakeLists.txt` e `tools/bench/bench_api.ps1`; há também notas, helper e teste de descritores untracked.
- [ ] **Step 3:** executar as suítes atuais e guardar seus códigos/logs. Compilar o estado de partida no cache correspondente, com `cmake --build port/out/build/win-amd64-release --parallel 4`, antes de copiar o baseline. Não presumir que o EXE existente corresponde às fontes locais.
- [ ] **Step 4:** preservar EXE, runtime e arquivos auxiliares necessários ao lançamento; calcular SHA-256. Não substituir snapshots anteriores nem copiar dados do jogo para o Git. Não coletar FPS enquanto esse build ou os testes estiverem rodando.

## Task 1: Opções explícitas e particionamento com 1–8 participantes

**Files:** criar `replay_options.h` e `test_replay_options.cpp`; modificar `parallel_replay.{h,cpp}`, `test_parallel_replay.cpp` e `tests/native/CMakeLists.txt`.

**Interfaces** (namespace `superman_returns::graphics::guest`):

- `enum class ReplayScheduling { kContiguous, kInterleaved };`
- `struct ReplayOptions { bool prepared=false; unsigned shards=1; ReplayScheduling scheduling=ReplayScheduling::kContiguous; bool invalid_threads=false, invalid_scheduling=false; };`
- `ReplayOptions ParseReplayOptions(std::string_view threads, std::string_view scheduling, bool packet_sink) noexcept;` — função inline pura; validade do pedido e disponibilidade do sink determinam modo efetivo.
- `ParallelReplayDecoder(unsigned threads, ReplayScheduling scheduling=ReplayScheduling::kContiguous);` — aceita construtor anterior; clamp 1–8; sem acesso ao ambiente.
- `Decode` e `ReplayResult` conservam assinaturas/semântica existentes.
- `size_t ReplayExecutionLimit(std::span<const ReplayResult> results) noexcept;` em `parallel_replay.{h,cpp}`: tamanho do prefixo incluindo a primeira falha, ou tamanho total se não houver falha. O runtime precisa executar a entrada falha para manter seu diagnóstico, mas não as posteriores.

- [ ] **Step 1: RED de configuração.** Adicionar `replay_options_distinguish_direct_and_prepared_one` e `replay_options_reject_ambiguous_inputs`. Asserções centrais:

```cpp
SR_CHECK(!ParseReplayOptions("", "interleaved", true).prepared);
auto one=ParseReplayOptions("1", "interleaved", true);
SR_CHECK(one.prepared); SR_CHECK_EQ(one.shards,1u);
for(auto value:{"0","9","01","+2","-1"," 2","2 ","2x"}) {
  auto p=ParseReplayOptions(value,"contiguous",true);
  SR_CHECK(!p.prepared); SR_CHECK(p.invalid_threads);
}
SR_CHECK(!ParseReplayOptions("8","interleaved",false).prepared);
auto bad=ParseReplayOptions("4","other",true);
SR_CHECK(bad.prepared); SR_CHECK(bad.invalid_scheduling);
SR_CHECK(bad.scheduling==ReplayScheduling::kContiguous);
```

- [ ] **Step 2: RED de particionamento.** Parametrizar a equivalência serial existente com os dois schedules e cada N de 1 a 8. Acrescentar lotes 0/1/3/65 comandos e misturas draw/pass-end/resolve/swap; comparar constantes literais, geometria, índices, registradores, versões e retenção de resultados depois de outro `Decode` e reset dos produtores.
- [ ] **Step 3:** compilar/rodar a suíte nativa e guardar o RED de API/política ausente.
- [ ] **Step 4: implementação mínima.** Adicionar opções e política; usar uma regra única de iteração de índices na contagem, decode e catches. Contíguo usa `[M*s/N,M*(s+1)/N)`; intercalado usa início s e passo N. Ampliar sondas privadas para oito. Manter snapshots, arena por shard, join e reunião por índice.
- [ ] **Step 5: falhas determinísticas.** Sob `SR_REPLAY_TEST_HOOKS` somente no target `sr_native_tests`, declarar `friend struct ReplayTestAccess` e um callback privado `void (*before_entry_)(size_t)=nullptr`. O friend de teste instala callback antes de `Decode`; ele lança em um índice escolhido, sem estado mutável concorrente. Testar índices de todos os shards, erro de preparação no índice 8, erro de decode, prefixo publicável e reutilização da instância. Testar o helper real `ReplayExecutionLimit`: vazio=0, todos válidos=tamanho, falha no índice 0=1, falha no 5 com entradas posteriores válidas=6. A suíte tem timeout de vivacidade de 30 s em CTest, sem thresholds de performance. O hook não existe no jogo nem no benchmark.
- [ ] **Step 6:** rodar build/CTest nativo; esperado equivalência e falhas na posição correta em todos os N/schedules. Conferir que o target de jogo não recebe `SR_REPLAY_TEST_HOOKS`.
- [ ] **Step 7:** commit `feat: add explicit replay scheduling options through eight shards`, incluindo somente arquivos novos e hunks desta tarefa; preservar o hunk preexistente de descritores em CMake fora do commit.

## Task 2: Sondas e formato de evidência independente do runtime

**Files:** modificar `parallel_replay.{h,cpp}`, `test_parallel_replay.cpp`; criar `replay_trace.{h,cpp}`, `test_replay_trace.cpp`; registrar fontes no CMake do port e dos testes.

**Interfaces:**

- `struct ReplayShardProbe { uint64_t begin_ns=0,end_ns=0,arena_ns=0,decode_ns=0; size_t commands=0,draws=0; };`
- Estender `ReplayBatchProbe` com `uint64_t dispatch_ns=0,parallel_ns=0,join_wait_ns=0; unsigned shard_count=0; std::array<ReplayShardProbe,8> shards{};`, mantendo campos atuais.
- `struct ReplayBatchSample { uint64_t sequence=0,closing_swap=0; size_t commands=0,draws=0; ReplayOptions options; uint64_t direct_decode_ns=0,direct_arena_ns=0; ReplayBatchProbe prepared; };`
- `ReplayTraceWriter(std::ostream& output, uint64_t clock_anchor_ns, int64_t clock_anchor_utc_ns); bool Write(const ReplayBatchSample&); bool good() const;` — escreve header no construtor; métodos em `replay_trace.cpp`.

**CSV v1:** header fixo:

```text
schema,kind,sequence,closing_swap,api,path,shards,scheduling,commands,draws,wall_ns,prepare_ns,parallel_ns,join_wait_ns,direct_decode_ns,direct_arena_ns,dispatch_ns,shard,begin_ns,end_ns,arena_ns,decode_ns,clock_anchor_ns,clock_anchor_utc_ns
```

Uma linha `kind=batch` por lote, `shard=-1`, seguida de `kind=shard` para cada
participante preparado; serial direto não tem linhas de shard. `api=vulkan`,
`path=direct|prepared`, `scheduling=contiguous|interleaved`. Campos numéricos não
aplicáveis são vazios, nunca zeros interpretáveis como medidas. Os anchors se
repetem por linha para tornar cópias parciais detectáveis. `closing_swap=0`
indica que o lote não fecha quadro. Em linha de shard, commands/draws são locais;
campos de lote aparecem só na linha batch. Linhas de mesmo lote têm configuração
e anchors idênticos. Namespace e opções seguem a tarefa 1.

- [ ] **Step 1: RED das sondas.** Adicionar `replay_probe_counts_each_entry_once`, `replay_probe_empty_shards_are_not_work` e `replay_probe_is_reset_between_calls`: soma commands/draws igual à carga preparada bem-sucedida; N ativo correto; nenhum contador herdado; `end_ns>=begin_ns`, `parallel_ns>=join_wait_ns`; não afirmar economia temporal no teste.
- [ ] **Step 2: RED da escrita.** `replay_trace_v1_has_one_batch_and_n_shards` compara header e campos contra um `ReplayBatchSample` literal com dois shards; `replay_trace_direct_has_no_fake_shards` exige campos preparados vazios; `replay_trace_reports_stream_failure` usa streambuf que falha e exige `!Write(...)`/`!good()`.
- [ ] **Step 3:** compilar/rodar para registrar falhas das interfaces novas.
- [ ] **Step 4:** medir preparação, dispatch, execução de shard, arena, decode e espera nos limites definidos pela spec. Colocar relógios sob `probe!=nullptr`; cada shard escreve sua própria sonda. A chamadora compõe `ReplayBatchProbe` só depois do join. Guardar duração total do lote após reunião e limpeza.
- [ ] **Step 5:** implementar writer CSV v1 com locale clássico e saída bufferizada. Não serializar ponteiros nem mensagens arbitrárias do ambiente. Sem escrita/flush dentro do loop por comando. Verificar stream após escrita/flush final; não esconder erro de disco.
- [ ] **Step 6:** rodar build/CTest nativo, incluindo equivalência com profiling desligado/ligado; confirmar contagens e ownership, sem teste que espelhe apenas a fórmula de distribuição.
- [ ] **Step 7:** commit `feat: measure replay dispatch and per-shard work with a versioned trace`; stage apenas hunks novos dos CMake.

## Task 3: Integração no worker e limites das janelas de medição

**Files:** modificar `native_renderer.cpp`, `tools/bench/bench.ps1`, `bench_api.ps1`; criar `tests/tools/test_bench_window_metadata.ps1`; ampliar testes nativos das tarefas 1–2 para a política de publicação.

**Interfaces:**

- Runtime consome `ParseReplayOptions`, `ReplayExecutionLimit`, `ReplayBatchProbe`, `ReplayBatchSample` e `ReplayTraceWriter` das tarefas anteriores.
- Acrescentar `-WindowMetadata <path.json>` opcional aos dois benches. Default vazio mantém comportamento atual. `bench_api.ps1` repassa o caminho a `bench.ps1`.
- Função pura `New-BenchWindowMetadata([string]$Name,[string]$Scenario,[DateTimeOffset]$Begin,[DateTimeOffset]$End)` produz `{name,scenario,begin_utc_ns,end_utc_ns}` com inteiros Int64. Isolar sua definição em `tools/bench/bench_window_metadata.ps1` (novo) para dot-source pelos testes e pelo bench.
- JSON final `{schema:1,name,windows:[...]}` contém idle e forward de 20 s efetivos; cada janela também recebe `fps_samples`, lista das leituras de guest FPS já coletadas pelo bench, antes do arredondamento da média histórica. Escrita no finally preserva janelas já concluídas mesmo se a execução falhar.

- [ ] **Step 1: RED de metadados.** Dot-source somente o helper puro em `test_bench_window_metadata.ps1`; testar dois DateTimeOffset com offsets distintos representando o mesmo instante, intervalo positivo de 20 s, rótulo, cenário e rejeição de fim anterior ao início. Executar `powershell -NoProfile -File tests/tools/test_bench_window_metadata.ps1`; esperado falha por helper ausente.
- [ ] **Step 2: integração de configuração.** Substituir o parsing local de `2|4` em `WorkerMain`; ler variáveis uma vez, avisar uma vez por campo inválido e logar modo efetivo. Criar decoder quando `options.prepared`, inclusive para um participante. Com sink ausente usar D3D12 direto. Usar `ReplayExecutionLimit(decoded)` como limite do laço preparado existente, mantendo execução da entrada falha para o diagnóstico e proibindo chamadas posteriores a `Execute`. A revisão deve confirmar que o helper testado é o limite realmente consumido, sem escrever um teste que apenas copie o laço do runtime.
- [ ] **Step 3: coleta preparada e direta.** Habilitar writer somente com timeline ativa, detalhe e `SinkProfiling()`. Abrir `<timeline>.replay.csv`, sem reutilizar arquivo de outra execução. Capturar anchors adjacentes de steady/system clock uma vez. Serial direto acumula duração de `ReplayCapturedRenderPacket` antes do sink e arena separada; preparado transfere a sonda completa. Escrever amostra depois do lote, com sequência crescente e `closing_swap` real. Falha de abrir/escrever/fechar registra `replay_trace_invalid=1` e o motivo no log; o jogo não deve travar por falha do diagnóstico, mas o executor deve invalidar a execução.
- [ ] **Step 4: janelas.** Instrumentar apenas a opção nova de metadata no `Measure-Window`: Begin imediatamente antes da coleta, End quando terminar; registrar sem arredondar FPS/tempos brutos. Escrever metadata em caminho novo, sem alterar CSV histórico, duração, input ou gate. A conversão de UTC para domínio monotônico será feita na tarefa 4 via anchors, com tolerância de uma borda de quadro; não assumir que Stopwatch e steady_clock tenham epoch idêntico.
- [ ] **Step 5:** rodar testes nativos, teste PowerShell de metadata e pytest de ferramentas. Compilar o jogo. Fazer smoke Vulkan direct/prepared-1/contiguous-4/interleaved-8 e D3D12 com variáveis presentes: conferir logs efetivos, fechamento de CSV e imagens de gameplay. Smoke não é rodada de aceitação.
- [ ] **Step 6:** commit `feat: integrate opt-in replay tracing and benchmark window metadata`, incluindo helper novo e somente hunks desta tarefa nos arquivos já modificados antes do ciclo.

## Task 4: Relatório por quadro, percentis e comparação entre execuções

**Files:** criar `tools/analysis/replay_balance_report.py`, `tests/tools/test_replay_balance_report.py`; modificar `frame_timeline_report.py` e seu teste somente para acrescentar p95 aos resumos existentes, sem retirar campos.

**Interfaces Python** (sem dependências além da biblioteca padrão):

- `load_replay(path: Path) -> list[dict]`: validar CSV v1 e retornar lotes completos com shards associados; inconsistência estrutural gera `ValueError` com linha/lote.
- `aggregate_frames(batches: list[dict]) -> dict`: `{frames:[...],dropped_prefix_batches:int,dropped_suffix_batches:int}`; primeiro swap é somente fronteira inicial.
- `summarize(samples: list[float]) -> dict`: `{samples,mean,p50,p95,p99}`; vazio retorna `samples=0` e medidas `None`.
- `analyze(replay: Path, timeline: Path, windows: Path) -> dict`: filtra quadros completos nas janelas e retorna resumos por cenário, lote/tamanho, quadro, shard e atrasos. Usa os anchors para mapear limites UTC. Cruza ids com timeline; não atribui frame incompleto ao cenário vizinho.
- `paired_bootstrap(control: list[float], candidate: list[float], seed: int=20261021, resamples: int=10000) -> dict`: diferenças relativas por pares, redução positiva, estimativa e CI 95%; rejeita vazio, zero denominador ou tamanhos diferentes. Estatística é média das reduções por par; IC usa percentis 2,5/97,5 das médias reamostradas.
- CLI: `python tools/analysis/replay_balance_report.py --matrix <manifest.json> --seed 20261021 --json <report.json>`; código não zero para evidência corrompida. Manifesto e estrutura de runs são definidos na tarefa 5. Runs limpos usam somente metadata/FPS e não exigem trace; ausência de trace em run marcado instrumentado é erro. Runs isolados usam JSON por rodada, agregado primeiro por processo para a comparação pareada.

- [ ] **Step 1: RED de percentis.** Asserts literais: `summarize(list(range(1,101)))` tem p50=50, p95=95, p99=99 e samples=100. Acrescentar p95=95 ao teste equivalente da timeline, preservando todos os campos anteriores.
- [ ] **Step 2: RED de agregação.** Fixture CSV: lote 1 fecha swap 10 (prefixo descartado); lotes 2/3 de 2 ms/3 ms fecham swap 11; lote 4 sem fechamento é sufixo. Exigir apenas frame 11, total preparado 5 ms, prefixo=1 e sufixo=1. Com máximos de shard de 1,5 e 2 ms, exigir soma dos máximos 3,5 ms; uma distribuição que troca o shard mais lento deve demonstrar que máximo da soma por shard é outra medida.
- [ ] **Step 3: RED de corrupção e janelas.** Testar header truncado, versão desconhecida, shard duplicado/ausente, contador negativo, end<begin, sequência/swap duplicado e configuração/anchor trocado no mesmo run. Campos preparados vazios em direct são válidos, não zeros. Um último registro incompleto é falha de evidência, não só warm-up descartável. Fixtures UTC/steady com origem diferente colocam frames distintos em idle/forward e excluem os que cruzam bordas e warm-up.
- [ ] **Step 4: RED estatístico.** Para vinte pares control=100/candidate=90, exigir reduction=0.1 e CI=[0.1,0.1]; testar semente reproduzível, pares desbalanceados, zero e evidência ausente. Verificar que rodadas internas/quadros não são tratados como processos independentes.
- [ ] **Step 5:** rodar `python -m pytest tests/tools/test_replay_balance_report.py tests/tools/test_frame_timeline_report.py -q`; registrar RED.
- [ ] **Step 6:** implementar interfaces e CLI. Calcular durações por quadro antes dos percentis; somar lotes sequenciais, nunca somar prepare com paralelo ou CPU-sum para fabricar frame time. Separar diagnóstico instrumentado de FPS limpo. Comparação pareada exige mesmo cenário, estrato, regime e bloco. O JSON inclui amostras, descartes e configuração efetiva.
- [ ] **Step 7:** repetir pytest e rodar relatório sobre fixture serializada; esperado JSON conhecido sem NaN/Infinity. Commit `feat: analyze replay frames and paired performance evidence`.

## Task 5: Matriz isolada e executor reproduzível

**Files:** modificar `replay_storage_benchmark.cpp`; criar `tools/bench/cpu_matrix.ps1` e `tests/tools/test_cpu_matrix.ps1`; atualizar `tools/README.md`.

**Interfaces:**

- Benchmark preserva argumentos e resumo existentes; passa a aceitar `--threads=1..8`, `--schedule=contiguous|interleaved` e `--json=<path>` opcional. Sem `--threads` permanece serial direto; `--threads=1` é preparado. JSON `{schema:1,mode,threads,scheduling,batch_draws,rounds,warmup_rounds,checksum,samples:[{arena_ms,decode_ms,total_ms,release_ms,cycle_ms}]}` inclui só rodadas pós-aquecimento e mantém verificação do checksum.
- `cpu_matrix.ps1 -Phase Isolated|Game -Name <label> -Exe <game.exe> -BenchExe <bench.exe> -Runs 10|20 -Variants <string[]> -StageTiming -PlanOnly`.
- Variantes válidas: `direct`, `contiguous-1`, `contiguous-2`, `contiguous-3`, `contiguous-4`, `contiguous-6`, `contiguous-8` e interleaved para 2/3/4/6/8. Fase isolada sem lista usa todas; Game exige lista explícita de finalistas/controles. Parâmetro `-BatchCommands 32|256` fixa um estrato e `-FpsLimit 30|60` fixa um regime, sem misturá-los.
- `-Action Run|Library` (default Run) permite dot-source das funções puras sem iniciar processos. `New-ReplayMatrixPlan([string[]]$Variants,[int]$Runs)` produz lista determinística balanceada alternando ordem direta/reversa em cada bloco, uma repetição por variante por bloco.
- `Invoke-ReplayEnvironment([hashtable]$Changes,[scriptblock]$Body)` executa e restaura ambiente em finally; `Invoke-ReplayMatrix([object[]]$Plan,[scriptblock]$Runner)` invoca Runner com um registro de run por vez e devolve os mesmos registros com status/código/erro. Esses pontos permitem testes com filhos simulados, sem adicionar flags de teste ao produto.
- Manifesto JSON `{schema:1,phase,name,source,executables,settings,runs:[{id,block,order,variant,scenario_files,profiled,status,exit_code,error}]}`; cada `scenario_files` aponta logs, JSON isolado ou windows/timeline/replay CSV e screenshots. `source` inclui revisão, patch e hashes de partida; `executables` inclui EXE/DLL/MAP e SHA-256. Salvar status antes/depois de cada run.

- [ ] **Step 1: RED do benchmark.** Criar casos de teste de CLI no teste PowerShell com processo stub e, depois de build, verificar que `--threads=8 --schedule=interleaved` é aceito, que `--threads=9`/schedule inválido saem com código 4, que checksum permanece correto e JSON tem oito rodadas medidas (10 menos 2 warm-ups). Nenhum teste exige tempo menor.
- [ ] **Step 2: RED do executor.** Em Library mode, testar plano com três variantes e dez blocos: trinta runs, uma ocorrência de cada variante por bloco, ordem reversa no bloco seguinte, ids únicos. Testar snapshot/restauração de ambiente inclusive valores originalmente ausentes e exceção do runner; locale pt-BR; caminho com espaço; recusa de rótulo já existente, processo de jogo preexistente e falha do filho. Usar funções de lançamento injetáveis/mocks no teste, sem capturas de tela, teclado ou término de processos reais.
- [ ] **Step 3:** executar teste PowerShell e CLI antiga para guardar o RED. Implementar JSON do benchmark e ampliar o limite do CLI, sem modificar workload/checksum/retenção.
- [ ] **Step 4:** implementar executor sequencial com `try/finally` para o ambiente. Fixar pool desligado, storage ligado, tamanho de lote e configuração de build; diagnosticar demais flags de profiling herdadas. Rodadas limpas explicitamente desligam profile/timeline; rodadas `-StageTiming` usam as três flags da tarefa 3 e metadata. Nunca reaproveitar nomes; criar diretórios com falha se já existirem. Fazer flush do manifesto após cada run. `-PlanOnly` só retorna plano de comandos e caminhos, sem lançar processo nem mudar ambiente.
- [ ] **Step 5:** reutilizar `bench_api.ps1` com `-Api vulkan -Name <id> -Exe <explicit> -FpsLimit <regime> -WindowMetadata <unique.json>`. Recusar jogo já aberto antes de chamar o bench histórico, que encerra instâncias pelo nome. Não acrescentar outro controlador de UI; quando precisar observar/interagir com Windows na execução, seguir a habilidade computer-use. Usar o mesmo gate existente para referência/candidata sem sobrescrever referência boa. Registrar falhas de gameplay, imagens e saída mesmo quando não houver resultado de FPS. Um log com `replay_trace_invalid=1` impede status de sucesso, assim como CSV ausente em execução instrumentada.
- [ ] **Step 6:** smoke da fase isolada com `-PlanOnly` e depois uma execução de cada direct/prepared-1/interleaved-8 fora da coleta de aceitação. Rodar os testes PowerShell novos e pytest de ferramentas. Esperado nenhum vazamento de ambiente e formatos aceitos pelo relatório da tarefa 4.
- [ ] **Step 7:** documentar opções, significado novo de `1`, diferença entre latência/soma de durações/FPS e como repetir uma variante. Commit `feat: automate isolated and gameplay replay comparisons`.

## Task 6: Validação completa, experimento e conclusão

**Files:** novo `docs/native-vulkan-replay-balance-results-2026-10-21.md`; logs e manifesto em diretórios exclusivos do ciclo; nenhum novo comportamento de produto.

**Interfaces:** consome executáveis/manifestos da tarefa 5 e JSON de análise da tarefa 4; produz recomendação e dados rastreáveis até as fontes/binários.

- [ ] **Step 1:** executar as suítes recorrentes uma vez sobre a candidata pronta; repetir somente após mudança/falha que justifique. Rodar também `powershell -NoProfile -File tests/tools/test_cpu_matrix.ps1` e `powershell -NoProfile -File tests/tools/test_bench_window_metadata.ps1`. Conferir `git diff --check`.
- [ ] **Step 2:** build final do jogo e fixtures GPU, sequencialmente. Para GPU, apontar apenas o ambiente do processo para `.tools/vulkan-validation/Bin`, preservando/restaurando o valor anterior de `VK_LAYER_PATH`. Executar `build/tests-vulkan/sr_vulkan_global_test.exe --require-validation` e `build/tests-vulkan/sr_vulkan_contract_test.exe --production-bindings`; exigir camada realmente ativa e zero erros, além das comparações de pixels. Se o segundo teste não imprimir estado da camada, preservar evidência do debug messenger/ativação em vez de inferir por exit=0. Não instalar uma camada ausente para completar a prova.
- [ ] **Step 3:** preservar candidata e auxiliares com SHA-256. Pedir uma revisão independente conforme a forma de execução aprovada, cobrindo ordem, catches intercalados, lifetime, CSV, janelas e ausência de relógios extras com profiling desligado. Corrigir achados antes da coleta final; novas alterações exigem novo hash e verificação pertinente.
- [ ] **Step 4:** executar triagem isolada com dez processos por variante/estrato. Exemplos de comandos da interface nova:

```powershell
powershell -NoProfile -File tools/bench/cpu_matrix.ps1 -Phase Isolated -Name rb_iso32 -BenchExe build/tests-native/sr_replay_storage_bench.exe -Runs 10 -BatchCommands 32
powershell -NoProfile -File tools/bench/cpu_matrix.ps1 -Phase Isolated -Name rb_iso256 -BenchExe build/tests-native/sr_replay_storage_bench.exe -Runs 10 -BatchCommands 256
```

- [ ] **Step 5:** selecionar no máximo duas finalistas pela menor mediana por processo de `cycle_ms`, exigindo checksum correto e comparando cada uma ao contíguo de mesma contagem e ao serial direto do mesmo estrato. Registrar inclusive variantes que pioraram. Rodar jogo em 720p/30: dez processos por configuração na triagem e vinte por configuração finalista/controle na decisão. Coletar séries limpas e instrumentadas separadas; ambas têm janelas de 20 s por cenário após aquecimento. Repetir finalistas a 60 somente se o limite de 30 ocultar diferenças. Não juntar estratos 32/256 nem contar 20 s como vinte execuções.
- [ ] **Step 6:** executar controle D3D12 na candidata com variáveis experimentais presentes e ausentes, observar gameplay e conferir modo efetivo direto. Se houver regressão aparente, investigar com controles repetidos; não atribuir benefício de replay ao D3D12.
- [ ] **Step 7:** analisar manifestos com `replay_balance_report.py --matrix ... --seed 20261021 --json ...`; exigir ausência de dados corrompidos. Documentar overhead das sondas em controle/candidata, equilíbrio por shard, preparo serial, Worker/Record/GPU, p50/p95/p99 instrumentados e FPS limpo. Para CI, usar execuções pareadas por bloco, não quadros correlacionados.
- [ ] **Step 8:** escrever resultado contra os critérios da spec: ganho de pelo menos 5% em replay ou 2% no Worker, alvo desejável 10% de replay, sem piora reproduzível de caudas/estabilidade/correção. Se a evidência for inconclusiva, dizer isso. Separar melhora isolada de ganho no jogo. Serial direto permanece padrão mesmo se houver recomendação positiva.
- [ ] **Step 9:** relatar gate visual/fixtures e suas limitações sem relaxar tolerâncias. Se Record/bindings continuar dominante, recomendar investigação posterior; se frontend/capture dominar, recomendar essa frente. Não começar o próximo subprojeto.
- [ ] **Step 10:** commit `docs: report replay balancing evidence and remaining bottlenecks`; conferir que o Git não recebeu executáveis, dados do jogo, logs locais ou mudanças preexistentes por acidente. Entregar links para resultados e manifesto, hashes, verificações e pendências reais.

## Autorrevisão e handoff

Cobertura: tarefa 0 preserva o estado; 1 cobre seleção/particionamento/erros;
2 define medições e formato; 3 integra publicação, clocks e janelas; 4 fecha
agregação/percentis/estatística; 5 executa a matriz; 6 valida e conclui.
As cinco condições de Review Focus têm testes atribuídos, e os contratos
de nomes/tipos entre tarefas estão definidos nos blocos Interfaces.

Este plano não foi executado. A especificação está aprovada; a implementação
aguarda revisão deste plano e escolha do método de execução pelo usuário.
Recomendação: execução Native nesta conversa, pois decoder, writer e análise
dependem de interfaces sequenciais e não exigem implementadores independentes
por tarefa. A revisão independente final permanece prevista.
