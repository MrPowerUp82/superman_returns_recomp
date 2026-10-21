# Replay Vulkan: balanceamento e medição — 21/10/2026

O serial direto permanece como padrão. Nenhuma variante preparada melhorou a mediana por processo no estrato principal de 32 comandos. O lote diagnóstico de 256 amortiza parte do custo, mas não demonstrou vantagem convincente de interleaved sobre contiguous com a mesma contagem. Não há recomendação de promoção nem alegação de ganho de FPS.

## Protocolo e rastreabilidade

Máquina: Intel Core i7-10750H, 6 núcleos/12 processadores lógicos, NVIDIA RTX 2060. Build Windows x64 Release, Clang 23.1.2, ThinLTO ON, Vulkan ON, LSFG OFF. Pool desligado, storage ligado; benchmark de dispositivo com bancos variáveis e retenção do lote anterior. Workload de 3008 draws; checksum `31793585826180` em todos os processos aceitos.

Cada variante tem dez processos independentes, dez rodadas internas com duas descartadas por processo. A estatística do processo é a mediana de oito `cycle_ms`; a tabela mostra a mediana dessas dez estatísticas. A redução é a média das reduções pareadas por bloco; IC95% bootstrap com 10000 reamostragens e semente `20261021`. Não é o percentual calculado a partir das duas medianas da tabela. Valores negativos significam piora. Ordem direta/reversa alternada em cada bloco. Estratos analisados separadamente.

Fontes: base `bfb18b0` e alterações locais preservadas; implementação `f8febcf` a `6e49260`. A matriz foi executada sobre `7d0f32a`; correções posteriores são do controlador PowerShell, sem mudança do benchmark ou EXE do jogo. Snapshot baseline/candidate, cache CMake, patch e arquivos locais com hashes em `artifacts/native-renderer-performance/replay_balance_2026-10-21_01/`.

- Baseline EXE: `B66EBC07B05C3867827C9CD5CA414FF621AA95B4A04EEE3C07F8D1D9CED1BB93`.
- Candidate EXE: `314760829700A21C1ADA1E3E2A73644F2D02FB242D084A3C2F80D35C282A641D`.
- Benchmark: `EEE58CB7E4E1E73161C2A81482425D71AB59ADCE844206C5FF8BE55D23BAADDB`.
- Não foi emitido MAP/PDB do jogo na candidata Release; o MAP baseline foi preservado.

## Triagem isolada

240 processos aceitos: 120 em cada estrato, sem falha de processo/checksum. A primeira série de 256 (`rb_iso256_20261021`, mais 120 processos) foi preservada e excluída porque uma análise concorrente começou antes dos últimos sete processos terminarem. A série `rb_iso256_clean_20261021` a substitui integralmente; não foram misturadas amostras. O arquivo `report.json` da primeira análise parcial não é resultado final; usar somente `report-final.json` dos dois manifestos aceitos.

### Lote 32 — `rb_iso32_20261021`

| Variante | Mediana cycle (ms) | Redução vs direto (IC95%), % | Redução vs contíguo igual (IC95%), % |
| --- | ---: | ---: | ---: |
| direct | 35.116 | — | — |
| contiguous-1 | 37.983 | -10.93 [-17.03; -7.36] | — |
| contiguous-2 | 38.066 | -8.87 [-12.33; -5.88] | — |
| contiguous-3 | 36.287 | -3.47 [-7.23; 0.42] | — |
| contiguous-4 | 36.912 | -5.63 [-9.19; -2.97] | — |
| contiguous-6 | 37.999 | -12.37 [-19.09; -6.61] | — |
| contiguous-8 | 40.845 | -19.13 [-25.14; -14.58] | — |
| interleaved-2 | 37.688 | -10.00 [-15.09; -5.93] | -1.30 [-7.36; 3.94] |
| interleaved-3 | 36.796 | -7.62 [-15.07; -1.92] | -4.27 [-11.51; 2.18] |
| interleaved-4 | 38.149 | -6.88 [-13.86; -0.89] | -1.41 [-8.48; 5.23] |
| interleaved-6 | 39.424 | -13.86 [-20.58; -8.18] | -1.71 [-6.82; 3.71] |
| interleaved-8 | 39.517 | -18.12 [-27.12; -12.00] | 0.57 [-6.59; 6.88] |

Manifesto bruto: `rb_iso32_20261021/manifest.json`; análise: `rb_iso32_20261021/report-final.json`.

### Lote 256 — `rb_iso256_clean_20261021`

| Variante | Mediana cycle (ms) | Redução vs direto (IC95%), % | Redução vs contíguo igual (IC95%), % |
| --- | ---: | ---: | ---: |
| direct | 31.906 | — | — |
| contiguous-1 | 35.205 | -10.12 [-11.45; -8.85] | — |
| contiguous-2 | 30.946 | 2.23 [0.24; 3.87] | — |
| contiguous-3 | 30.582 | 2.70 [0.93; 4.19] | — |
| contiguous-4 | 30.326 | 4.25 [2.29; 6.02] | — |
| contiguous-6 | 30.224 | 5.08 [3.30; 6.68] | — |
| contiguous-8 | 30.016 | 1.08 [-5.53; 5.49] | — |
| interleaved-2 | 31.003 | 2.17 [0.14; 4.04] | -0.08 [-1.32; 1.18] |
| interleaved-3 | 30.869 | 3.25 [1.86; 4.51] | 0.53 [-1.06; 2.03] |
| interleaved-4 | 30.581 | 2.89 [0.43; 4.97] | -1.44 [-3.49; 0.47] |
| interleaved-6 | 30.378 | 3.49 [1.16; 5.41] | -1.68 [-3.10; -0.38] |
| interleaved-8 | 30.684 | 3.15 [1.77; 4.55] | 1.41 [-1.94; 6.46] |

Manifesto bruto: `rb_iso256_clean_20261021/manifest.json`; análise: `rb_iso256_clean_20261021/report-final.json`.

## Correção, revisão e gameplay

- Native CTest 2/2: 229 testes sem falhas mais budget; Vulkan CTest 1/1: 108 testes sem falhas. Python: 123 testes. Testes PowerShell de metadata e matriz/CLI passam; matriz exercitada em PowerShell 5 e 7.
- Fixtures GPU global e portátil com production bindings passaram. Global imprime `validation_active=1 validation_errors=0`; loader portátil registra inserção da camada Khronos no instance e device, `validation_errors=0 exit=0`. Comparações de pixels aprovadas. Logs em `verification/`.
- A revisão independente gpt-6-astra/high não encontrou falha crítica no decoder; quatro achados importantes da evidência foram reproduzidos em RED e corrigidos: swaps ausentes, configuração divergente, aquecimento anterior ao gameplay e timeline inválida/vazia. Testes voltaram a GREEN. A integração também corrigiu datas JSON tipadas e restauração de variável ausente no PowerShell 7.
- Opções explícitas 1–8 e ambos os particionamentos têm equivalência nativa, erros injetados, payloads retidos e conclusão dos joins. O caminho sem opção continua direto; D3D12 não adota o decoder preparado.

O smoke Vulkan interleaved-8 chegou a gameplay com cidade, Superman, carros/pedestres, HUD e movimento. O smoke direto posterior chegou a cidade/personagem/minimapa e respondeu à API, mas o detector existente recusou duas capturas de HUD esmaecido. As imagens e o log foram preservados em `smokes/direct/`; não se reduziu nenhum limite e a rodada foi encerrada sem medir FPS. Trata-se de impedimento de promoção, não de comprovação de regressão introduzida pelo scheduling. Screenshots não provam equivalência exata de imagem.

## Decisão e limites

Foram selecionadas zero finalistas de desempenho no jogo: todas as medianas preparadas pioraram no estrato principal e nenhuma comparação interleaved/contíguo nesse estrato exclui zero a favor de interleaved. O encerramento negativo é permitido pela especificação. Assim, não foram executadas as séries condicionais de 10/20 processos no jogo nem a repetição a 60 FPS. Um efeito específico do jogo poderia escapar dessa triagem; não se afirma ausência de regressão nem ganho ponta a ponta.

Não há estimativa aceita de overhead das sondas, percentis p50/p95/p99 de gameplay comparável, nem efeito pareado em Worker/Record/GPU. Os CSVs dos smokes são diagnósticos; o prefixo/sufixo e as transições de menu não substituem janelas válidas. Somar durações de shards sobrepostos não representa frame time ou CPU do sistema. O flush por lote também faz parte do custo instrumentado.

Como pista para um ciclo posterior, o smoke direto registrou em um bloco diagnóstico de 120 quadros `record=60.1 ms`, `bindings=25.3 ms` e `execute=33.11 ms`; em outro, `record=46.5`, `bindings=18.8`, `execute=26.05`. Não são amostras pareadas nem intervalos equivalentes. Junto ao histórico de 19/10 (Record 75.02 ms vs Worker 42.36 ms), sustentam investigar recording/bindings com novas janelas válidas antes de ampliar shards. Não se iniciou esse próximo subprojeto.

## Reprodução

```powershell
powershell -NoProfile -File tools/bench/cpu_matrix.ps1 -Phase Isolated -Name novo_rb32 -BenchExe build/tests-native/sr_replay_storage_bench.exe -Runs 10 -BatchCommands 32
python tools/analysis/replay_balance_report.py --matrix artifacts/native-renderer-performance/novo_rb32/manifest.json --seed 20261021 --json novo_rb32-report.json
```

Opções e protocolo de Computer Use/API: `tools/README.md`. Rótulos novos são obrigatórios; flags experimentais permanecem opt-in. Logs, dados do jogo, executáveis e mudanças locais preexistentes não fazem parte dos commits deste ciclo.

## Controles de compatibilidade

D3D12 sem variáveis de replay (`rb_d3d12_absent_v2`) e com threads=8/interleaved (`rb_d3d12_present`) chegaram a gameplay observado, com Superman e cidade. Os logs identificam `mode=direct threads=1` nos dois; a política aparece no log mas não ativa o decoder preparado no D3D12. Capturas preservadas em `logs/`. São smokes, sem comparação quantitativa de FPS ou câmera alinhada. O primeiro smoke sem variáveis foi interrompido antes de gameplay e repetido sob rótulo novo.

O parser estrito pós-revisão também validou o trace real de interleaved-8: 322322 lotes, 5203 quadros completos, dois lotes de prefixo e 67 de sufixo descartados. Isso valida o formato/agrupamento, sem converter o smoke em uma janela de aceitação.

## Decisões de execução registradas

Registro completo das decisões e seus custos, na ordem em que foram tomadas:

- Task 0: Ruling: use Git Bash explicitly at C:/Program Files/Git/bin/bash.exe for skill scripts; default bash is WSL. Costs if wrong: scripts fail before implementation; sources unaffected.

- Task 0: Ruling: the developer wrapper initially resolved Clang through the worktree junction while rebuilding the original cache; CMake detected a compiler-path change and discarded configuration. Restore the original compiler path and explicitly reconfigure all known baseline options (SDK package, ThinLTO ON, Vulkan ON, LSFG OFF, memory tracking OFF). No source changes; risk is loss of an unrecorded cache-only option, so configuration is recorded in the baseline manifest.

- Task 2: Ruling: repeated device-fixture failure is uninitialized fixture bytes, not scheduler behavior. WorkBatch uses DefaultInitAllocator and resize(size) intentionally leaves bytes unwritten; declaration usage byte varied (e.g. 172), affecting reference and candidate. Explicit resize(size,0) initializes the fixture; production allocator is unchanged. Cost if wrong: an unrelated decode issue could remain; stress repeats and independent review retain coverage. ASan first launch failed with entrypoint status 0xC0000139 before tests; no sanitizer claim made.

- Task 3: Ruling: flush trace once per batch, never per command, because the legacy bench can force process termination and buffered output otherwise ends in a truncated record. Buffered-failure test observed RED before flush; instrumented overhead must include this I/O. Cost if wrong: per-batch I/O perturbs diagnostic timings; it cannot be treated as clean performance. Task 3/5: Ruling: computer-use guidance forbids mixing direct PowerShell UI automation with Computer Use. Actual gameplay runs will use existing SR_BENCH_INPUT game API and Computer Use screenshots instead of invoking bench.ps1 UI injection/capture. Preserve its optional metadata interface for legacy use; add a pure game-API runner with the same warm-up/20s windows. Cost if wrong: workload differences must be disclosed; camera/path are already nondeterministic.

- Task 3: Ruling: new worktree lacked runtime Vulkan cache/pre-shader libraries. First portable smoke reached PRESS START after cold compilation, not a benchmark. Preserve its cold cache and copy original warmed runtime cache plus both .srvk libraries into the isolated build; later comparisons will explicitly record/pin the ABI mode and use the same warm cache. Costs if wrong: residual cold misses invalidate a performance run; log misses separately.

- Task 3: Ruling: removed empty stale worktree index.lock after >120 s with unchanged timestamp, no git process and successful exclusive-open check. Only this empty lock was removed; source/index contents untouched. Costs if wrong: a non-git background library could still have held logical ownership; staged tree is checked before committing.

- Task 3: Ruling: remaining direct/prepared-1/contiguous-4/D3D12 launch smokes are consolidated into final-candidate matrix preflight (Task 5/6); avoid redoing boots before the pure game-API runner exists. Cost if wrong: integration-specific defects are found later; all modes have native equivalence tests. Actual warm CSV validated: 322322 batches, 5203 complete frames, prefix 2/suffix 67 discarded, no structural errors.

- Task 5: Ruling: Game matrix uses a phased SR_BENCH_INPUT API runner with Computer Use ready/end evidence files; no PowerShell UI automation is mixed into this session. The existing HUD/image gates and 25 s warmup/20 s windows are retained. Cost if wrong: timing of scene entry differs from legacy keyboard automation; manifests disclose the API workflow and image gate failures prevent promotion.

- Task 6: Ruling: perform the one fresh-context whole-implementation review after Tasks 1-5 and before accepted measurements, as Task 6 Step 3 requires, rather than after the results document. No code changes will be made after the single fix pass without relevant tests/new hashes. Cost if wrong: results prose lacks a second independent review; the measured implementation is reviewed before use.

- Final: Ruling: reviewer deferred GPU/performance/results because Task 6 remains in progress — finish these gates before conclusion; cost if wrong: acceptance could rely on incomplete evidence.

- Final: Ruling: reviewer excluded preexisting renderer/descriptor changes from feature review — preserve baseline patch/untracked snapshot and never stage them; cost if wrong: a baseline bug may confound absolute behavior, controlled comparisons share it.

- Final: Ruling: reviewer accepted game API + Computer Use replacement — retain it under the existing computer-use safety constraint, fixing scene warmup independently; cost if wrong: workload differs from legacy keyboard automation and is disclosed.

- Final: Ruling: reviewer set aside screenshots as exact equivalence proof — keep the existing gate for triage and deterministic native/GPU fixtures for correctness; cost if wrong: scene-specific pixel defects outside fixtures may remain and block promotion if observed.

- Task 6: Ruling: preserve but exclude the first 256 matrix from acceptance because report/bootstrap analysis began while its last seven processes were still running. Repeat all 120 processes under a new label with no concurrent analysis; keep first manifests/reports as protocol-deviation evidence. Cost if wrong: extra runtime, but avoids a workload interference claim.

- Task 6: Ruling: select zero gameplay performance finalists. All 11 prepared variants have worse process median at the primary batch32 stratum; interleaved comparisons against equal-count contiguous include zero, and the diagnostic256 gains do not establish scheduling benefit at the production stratum. Under the spec no-convincing-variant exit, retain direct default and close this hypothesis. Do not claim clean FPS, probe overhead or p95/p99 acceptance without the conditional 10/20-run game series; the unrelaxed HUD gate also failed on direct smoke. Cost if wrong: isolated triage could miss a game-specific gain; report this limitation and make no absence-of-regression assertion.

Não houve achado menor adiado pela revisão independente. O gate HUD e a ausência de séries aceitas no jogo permanecem limitações explícitas deste resultado negativo.

Vulkan preparado-1 e contíguo-4 também chegaram a gameplay observado. Logs registram respectivamente prepared/1/contiguous e prepared/4/contiguous; screenshots em logs/rb_prepared1.png e logs/rb_contiguous4.png. O detector HUD recusou também a captura de prepared-1; nenhuma tolerância foi relaxada.
