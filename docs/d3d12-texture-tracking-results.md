# Resultado do rastreamento de texturas D3D12 — 2026-10-20

**60 FPS não foram atingidos. O modo ativo permanece experimental e Off continua padrão.**
A implementação elimina hashes apenas para um recurso GPU ligado a snapshot próprio,
identidade de alocação e cobertura completa de todas as faixas. Não altera shaders,
qualidade, resolução, MSAA, geração de quadros, VB/IB ou Vulkan.

## Evidência e decisão

Mesmo EXE/DLL na sequência Off/On/On/Off, 1280×720, escala 1, MSAA 1, VSync,
limite 60 e LSFG desligado. Sem builds, testes ou profiling nas janelas de 20 s.
Médias das amostras de guest FPS a cada aproximadamente 2 s; não são 1% lows.

| Rodada | Modo | Parado | Andando |
| --- | --- | ---: | ---: |
| tt_off_b | off | 12.92 | 11.92 |
| tt_on_a | on | 12.49 | 14.43 |
| tt_on_b | on | 18.42 | 20.32 |
| tt_off_c | off | 12.21 | 12.83 |

EXE SHA256: `4A39D0192CEBDF959717CE863951BDCD19E57834363DC35A7501575E8F497B9E`.
Runtime SHA256: `8445EE77A69376493FB494882A18A4397C40CE4AB4556D831B96F50681FB57DA`.
SDK fonte base: `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, com patch e manifesto versionados.
A árvore inclui alterações anteriores de reciclagem de descritores preservadas;
elas são iguais em todos os modos e não pertencem aos commits deste plano.
As medições pertencem ao binário anterior ao ajuste final da auditoria Compare;
os hashes acima identificam essa evidência, não o executável reconstruído depois
da revisão. A revisão independente examinou apenas os commits deste plano:
não constitui validação isolada das alterações anteriores presentes no jogo medido.

Os intervalos completos entre publicações de contadores dispensaram 99,318–99,343%
dos bytes que a cadência anterior mandaria hashear dentro do cache On. On usa
chave de descritor completa, portanto esse percentual não compara volumes de
cache idênticos entre Off e On. Isso não inclui texturas que
já ficavam sem hash pela política anterior. Não equivale a redução de 99% do custo
de CPU. O tracker, consultas e hashes residuais também consomem tempo.

**Resultado de promoção: inconclusivo, não aprovado.** Câmeras, pose e transparência
do HUD não ficaram alinhadas. O gate existente falhou para On e também para o
segundo Off contra o primeiro Off. Nenhum threshold ou referência foi relaxado.
PSNR global: 20,96 dB (On_a), 18,85 dB (On_b), 19,95 dB (Off_c); a região `logo`
reprovou os pares. Os PNGs de cliente só removem a moldura da janela; os originais
permanecem preservados. Esses pares não demonstram regressão causada pelo tracker.

O HUD foi observado visualmente no começo/fim das quatro rodadas. O detector
antigo rejeitou o início de Off_b: o brilho vermelho ocupou oito linhas amostradas,
contra limite de sete. A falha automática foi preservada. As outras três rodadas
passaram no detector. Tentativa `tt_off_a` foi abortada antes da medição por uma
incompatibilidade de timestamps do PowerShell 7; foi excluída e a comparação passou
a usar UTC ticks, com testes em PowerShell 5 e 7.

Também não há duas rodadas por modo com custo total de runtime/proteção/faults
contabilizado. O relatório recusa recomendar On sem essa evidência e sem a imagem
alinhada. Os FPS acima são observações descritivas, não ganho causal aprovado.
Não há verificação de cadência/simulação do jogo completo a 60 FPS.

## Custo que permaneceu

Uma execução separada `tt_on_cost`, com StageTiming, registrou os valores abaixo.
São regiões aninhadas: **não somar as colunas**. Seu FPS não entra na aceitação.

| Amostra de 120 quadros | Worker | Preparação | Constantes | Streams | Lookup textura | Hash residual |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 67,88 ms | 33,20 ms | 23,24 ms | 11,94 ms | 6,01 ms | 0,32 ms |
| 2 | 61,55 ms | 30,80 ms | 21,68 ms | 9,28 ms | 5,30 ms | 0,30 ms |
| 3 | 63,66 ms | 31,98 ms | 22,74 ms | 11,30 ms | 5,66 ms | 0,34 ms |

Entre duas publicações completas, as consultas do tracker somaram aproximadamente
2,50 ms/quadro. O lookup inclui esse custo. O trabalho do runtime fora do worker
não foi integralmente cronometrado; não aprovar a meta de redução total ≥30% com
esses números. O worker processou aproximadamente 2,8–2,9 mil draws por quadro.
A evidência mostra que remover os hashes não reduz todo o worker a 16,67 ms.
Estado gráfico incremental e rotinas XDK exigem outro escopo, conforme o plano;
não foram iniciados nesta entrega.

## Correção e reprodução

- Modelo puro e política: CTest nativo 2/2, 217 casos na árvore de trabalho.
- Memória real, I/O parcial/erro/zero/scatter, rede, aliases, lifecycle e WARP/readback:
  34 casos normais mais três processos de falha, CTest 4/4.
- Cliente com SDK stock: CTest 1/1; export ausente mantém hash.
- Ferramentas Python: 93 testes; fixtures PowerShell verificam ambiente, neutralização,
  processo exato e timestamps. Setup reproduzível validado sem sobrescrever edições.
- Título/attract, vídeo de abertura, transição New Game, gameplay parado/movimento,
  HUD, minimapa e capa observados. Novas entradas de textura durante carregamento e
  movimento não registraram divergência no modo Compare. Cobertura de cenas não é
  prova universal. A camada de debug D3D12 continuou indisponível.

Fontes/contrato: [inventário](d3d12-memory-write-coverage.md),
[especificação](superpowers/specs/2026-10-20-d3d12-resource-write-tracking-design.md),
[evidência compacta](d3d12-texture-tracking-evidence.json).
Logs/PNGs locais: `logs/tt_*`, `logs/texture_tracking_compare1_*` e
`logs/texture_tracking_report.json`. Os arquivos brutos são artefatos locais ignorados.

Preparar com `powershell -NoProfile -File tools/setup_memory_tracking_sdk.ps1`.
Build: `SR_MEMORY_TRACKING_SDK=ON`, saída separada em
`port/out/build/win-amd64-write-tracking`, e `build.cmd`.
O SDK precompilado continua selecionado com `SR_MEMORY_TRACKING_SDK=OFF`.
Em execução, `SR_D3D12_TEXTURE_WRITE_TRACKING=off|compare|on`; valor inválido vira Off.
DLL sem contrato v1 completo mantém a política antiga. Compare mantém hashes e uploads.

Benchmark por fases: `tools/bench/bench_texture_tracking.ps1 -Exe <exe> -Name <nome>
-Mode off|compare|on [-StageTiming]`. Usar ações Launch, Input, Measure (idle/forward),
Validate com PNGs capturados imediatamente pelo computer-use, e Stop. O script só
encerra o processo que lançou e restaura variáveis; não opera a interface do Windows.
StageTiming é diagnóstico e invalida aceitação de FPS. Relatório:
`python tools/analysis/texture_tracking_report.py <manifest.json> --json <saida.json>`;
retorna 2 quando a promoção é inconclusiva. Não confundir esse retorno com teste falho.

## Decisões registradas durante a execução

A revisão independente do intervalo `05bdf7c..7e1c5ec` não encontrou defeito crítico
nem itens menores. Identificou uma lacuna importante de verificação: o teste
sintético de divergência não exercitava a invalidação real da alocação. O renderer
agora compartilha essa ação com um teste de memória real. O teste remove a proteção
diretamente para injetar uma escrita sem notificação, altera bytes e verifica que
observadores sobrepostos, outra página e novos observadores ficam invalidados
na geração atual. Após encerrar essas referências, uma nova alocação pode obter
cobertura. Também limpa ambas as provas locais após a divergência.

RED inicial: função compartilhada ausente. GREEN: CTest real 4/4, 34 casos normais.
Uma mutação que remove apenas a invalidação real causou as quatro falhas esperadas;
após restaurá-la, os quatro grupos passaram. Testes nativos 2/2, cliente stock 1/1
e Python 93/93 passaram novamente. Nenhum item menor ficou adiado.
Os executáveis Release completos com SDK stock e com runtime opcional também
compilaram e linkaram após essa correção. Logs locais desta verificação e da
mutação: `logs/texture_tracking_final_verification/`.

- Ruling: Work in current folder on new branch as explicitly requested; no isolated checkout — user preference overrides skill default — other uncommitted changes require selective staging. Risco se a premissa estiver errada: misturar alterações anteriores ou dificultar reversão; staging seletivo preserva a separação.

- Ruling: CMake/CTest are not on PowerShell PATH; use VS bundled tools through a local environment wrapper — fixes environmental command failure — no product behavior difference. Risco se a premissa estiver errada: build não reproduzível ou compilador incorreto.

- Task 1 Ruling: Separate shared-page views from a new neighbor allocation transition in tests — allocation on shared page itself conservatively invalidates the prior generation's coverage; the fixture expected eligibility after this loss — keep fail-closed behavior, assert new-neighbor invalidation separately. Risco se a premissa estiver errada: perder elegibilidade e desempenho por excesso de invalidação conservadora.

- Task 2 Ruling: Additional physical alias 0x7F000000 maps 16 MiB and LookupHeap returns null — guard actual map_info views, not only A/C/E — omitting this would permit missed writes. Risco se a premissa estiver errada: perder escritas de alias ou proteger endereço errado.

- Task 2 Ruling: Use a runtime-global session under SDK global critical region instead of adding Memory members/MMIO public API — all guards/records remain serialized and Memory ABI need not change — teardown and cross-session handles must be tested explicitly. Risco se a premissa estiver errada: corrida de escrita, uso após liberação ou contenção de CPU.

- Task 2 Ruling: Keep runtime implementation in tracked tools/rexglue-write-tracking/src/runtime_memory_tracker.{h,cpp}, materialized by setup like pure state — patches only wire version-specific hooks — avoids a massive opaque patch and preserves source ownership. Risco se a premissa estiver errada: misturar fontes/patches e perder reprodutibilidade.

- Task 2 Ruling: Physical heap mutations always acquire global critical region in optional runtime, including Windows 4 KiB pages — original reconcile lock was skipped there, unsafe against concurrent guard install — allocation/protection overhead must enter later cost accounting. Risco se a premissa estiver errada: sobrecusto de locks ou sincronização insuficiente.

- Task 2 Ruling: Source build hit libmspack symlink stubs on Windows (lzxd.c contained a relative path, not C) — SDK patch selects canonical libmspack/libmspack/mspack sources instead of cabextract symlinks — identical upstream contents without requiring elevation/symlinks. Risco se a premissa estiver errada: erro de build ou de descompressão de assets se fontes não forem equivalentes.

- Task 2 Ruling: Source-tree hook does not inherit SDK windows-base preset (-march=x86-64-v2), causing six SSSE3 intrinsic compile errors — apply the same baseline in optional source-build scope; also alias source target xxhash to installed name rex::xxhash — no change to stock build flags. Risco se a premissa estiver errada: instruções incompatíveis/flags diferentes ou erro de link.

- Task 2 Ruling: SDK wrote DLL/libs into the source directory across builds — patch output paths to PROJECT_BINARY_DIR so separate build trees cannot silently overwrite each other's runtime — no semantic runtime change. Risco se a premissa estiver errada: usar DLL/libs de outra saída.

- Task 3 Ruling: Count host scopes before looking for a monitor, even when guards are absent — otherwise a session installed during already-running kernel I/O could miss writes — optional SDK has a small I/O-only bookkeeping cost in Off, no page monitor or per-frame audit. Risco se a premissa estiver errada: sessão indisponível por produtor pendente ou escrita anterior não observada.

- Task 3 Ruling: Scope holds shared metadata lease; Stop detaches and restores guards without waiting indefinitely for blocking recv, while pending global producer count prevents a new session until scopes end — callbacks serialize under global region, metadata survives until last producer — avoids both teardown deadlock and use-after-free. Risco se a premissa estiver errada: deadlock de encerramento ou metadados liberados durante I/O.

- Task 3 Ruling: Host producers active before first watch block watch installation, rather than permanently invalidating never-watched allocations; monitored allocations still lose coverage permanently on first write — guard installation after completion is mechanical, not audit-based promotion. Risco se a premissa estiver errada: redução de elegibilidade ou certificação anterior ao fim do produtor.

- Task 3 Ruling: Network output also needs old SDK virtual invalidation AFTER I/O when guard handoff occurred; XFile retains its existing post-read callback order — preserving old VB/IB consumers despite new kernel buffer handling. Risco se a premissa estiver errada: VB/IB antigos sem invalidação correta após kernel I/O.

- Task 6 Ruling: split benchmark into Launch/Input/Measure/Validate/Stop phases and read timestamped PNG evidence captured through computer-use — skill prohibits mixing direct PowerShell UI automation with sky — exact same 20s protocol, safer process/input cleanup; no automatic visual PASS. Risco se a premissa estiver errada: evidência inválida ou limpeza de processo/entrada incorreta.

- Task 6 Ruling: image alignment/gate failed (including Off/Off) and total runtime cost is not fully accounted — apply spec stop-of-promotion, retain Off default and experimental On, do not infer >=30% total cost gain or 60 FPS from 99.3% skipped bytes — further repeated stage promotion runs withheld; one separate On profiling window identifies remaining worker cost (61.55–67.88ms, residual hash0.30–0.34ms). Four descriptive same-binary windows preserved; off_b automatic HUD false-negative remains recorded. Risco se a premissa estiver errada: deixar de promover uma otimização útil; não autorizar imagens/FPS sem evidência.

- Final Ruling: preservar as alterações anteriores que a revisão deixou fora do escopo — as medições incluem essas alterações e não validam isoladamente só os commits deste plano. Risco se a premissa estiver errada: atribuir comportamento ou desempenho incorretamente a esta branch; o resultado permanece inconclusivo.

- Final Ruling: manter a branch local, conforme a preferência do usuário por trabalhar nesta pasta numa nova branch — nenhuma integração remota foi solicitada. Risco se a premissa estiver errada: integração adiada; os commits continuam disponíveis.
