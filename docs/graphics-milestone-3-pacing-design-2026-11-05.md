# Milestone 3: presentation pacing (2026-11-05)

## Contract and implementation plan

1. Add deterministic tests for a rational 30/60/120 Hz deadline, missed-deadline
   recovery, and bounded scheduler oversleep history. Add a presentation CSV sink.
2. Reuse the SDK's existing UI presenter and triple mailbox. Force native output
   requests onto that presenter; never sleep or call a blocking Present from the
   guest render worker. No additional thread is necessary.
3. Use DXGI interval 1 with no RESTART/tearing flags when vsync is enabled. Vulkan
   already selects FIFO for vsync; report that contract to the SDK so its worker
   does not present directly. Apply deadline pacing outside GPU queue locks.
4. Record one CSV row at each actual Present/QueuePresent return, preserving
   failed/occluded calls and source/generated/repeat classification. Run the
   deterministic tests and a sequential hardware matrix after other workers stop.

## Timing and history

Guest vblank remains a synchronization contract (two vblanks per selected FPS),
not a simulation clock modification. Host presentation uses steady-clock rational
deadlines, retaining phase after ordinary jitter and skipping missed slots instead
of issuing bursts. A bounded history of recent sleep overshoot selects an early
wake margin; final yield/spin waits correct scheduler jitter without accumulating
oversleep. Sleep is on the existing presentation/UI thread, outside queue locks.

The selected target is a ceiling. 120 FPS on a 60 Hz display with interval 1/FIFO
cannot produce 120 visible real frames/s. The mailbox may replace intermediate
outputs; CSV must expose this. Acceptance for 120 visible FPS requires a display
and swapchain mode at 120 Hz or higher. No tearing or simulation-speed changes
are introduced to claim otherwise.

## Measurement boundary

CSV timestamps are monotonic CPU entry/return timestamps of actual presentation
API calls, not guest swaps, render submissions, physical scanout timestamps, or
proof that every accepted frame became visible. Successful CPU calls and their
intervals are useful for jitter diagnosis; PresentMon/display timing is required
for scanout acceptance. SDK original checkout is preserved: any SDK change ships
as a versioned repository patch with explicit application to an isolated checkout.

## Implemented and verified boundary

`port/src/graphics/presentation_pacing.h` implements rational deadlines, a
32-sample bounded sleep overshoot history, and CSV recording. Native setup forces
the existing SDK UI presenter, independently of vsync. Both project D3D12 paths
(including each generated/original LSFG Present) use interval 1 under vsync;
Vulkan reports implicit vsync and records actual QueuePresent timestamps via a
callback after releasing the queue lock. No extra thread was added.

The standalone pacing suite compiled with clang++ C++20 and passed **5 tests,
0 failed checks**. MSVC headers emitted the existing `getenv` deprecation warning
when compiling without the project's `_CRT_SECURE_NO_WARNINGS` setting. Red/green
execution was deferred until the coordinator allowed isolated unit testing;
the tests were authored before implementation but were not observed failing first.

The versioned SDK overlay is
`tools/patches/rexglue/v0.10.0/0003-native-presentation-pacing.patch`, with its
canonical manifest alongside it. `tools/setup_presentation_pacing_sdk.ps1`
requires an explicit isolated checkout, refuses the original SDK, checks pinned
revision and exact original/patched file hashes, and records
`.sr-presentation-pacing.json`. The memory-tracking setup accepts these two
additional paths only after exact overlay verification. Both overlay verification
and `setup_memory_tracking_sdk.ps1 -VerifyOnly -SkipDependencies` passed on
`.tools/rexglue-object-motion-source`; the original SDK was not changed.

## Hardware verification pending

No builds, tests or GPU workloads may overlap the other milestone workers.
The coordinator must run native tests, the Vulkan policy tests and native Release
build, followed by 30/60/120 trials with vsync on/off at a fixed scene. Compare
presentation API cadence, source serial replacement/repeats, p50/p95/p99 intervals
and long gaps. This design does not declare measured acceptance.

Rebuild the isolated SDK before the game, since the default non-LSFG presenter
lives in rexruntime. Set `SR_PRESENTATION_CSV` to a unique absolute CSV path per
process before launch. Summarize with
`python tools/bench/summarize_presentation.py <csv>`. Accepted API calls include
real, repeat and generated rows; failed/occluded D3D calls remain in the CSV with
`accepted=0`. Vulkan success/suboptimal QueuePresent calls are accepted. Enable
PresentMon separately to measure display delivery; CPU API cadence alone cannot
establish visible frame rate or low display jitter.

The launch-ready matrix is `python tools/bench/bench_presentation.py --name
m3_pacing_3rounds --api d3d12` (or `--api vulkan`). It runs 3 rounds of all
30/60/120 × vsync on/off combinations sequentially, reverses order in round 2,
rejects concurrent game/build/test processes, saves a distinct CSV and command
per run, and retains the final measurement duration separately from startup.
`--plan-only` was run successfully; no GPU trial was run by the M3 worker.
Native rendered-output readbacks are also converted to PNG when available;
Vulkan capture support and scanout screenshots require separate validation.
Readback can perturb a frame, so the CSV retains its long gap rather than
silently excluding it.

Review corrections: changing the FPS target now updates both host deadlines and
the guest vblank cadence live, rather than changing only the former. Vulkan
vsync changes invalidate and recreate the swapchain on the existing UI thread.
Shutdown publishes `running=false` before waking all guest GPU waiters and
signals the command-processor wait event. Existing worker shutdown precedes UI
presenter destruction; Vulkan also stops its asynchronous producer before
detaching the presenter. CSV flushes every row because benchmark termination
uses `std::_Exit`, which bypasses destructors. This opt-in telemetry performs its
file writes outside the Vulkan queue lock.
# Comparação do mecanismo anterior

Antes da medição, acrescentar `SR_PRESENTATION_LEGACY=1` apenas para diagnóstico:
desativa o deadline de apresentação e restaura Present(0)/vsync externo do SDK.
O vblank/sleep do guest permanece igual nos dois casos. A matriz compara esse
caminho com deadline + Present(1), com 30/60/120 solicitados e mesmo executável.
Capturas ficam fora das três rodadas medidas para não introduzir stalls de leitura.


### Taxa física do monitor na validação

O desktop observado está em 1920x1080/144 Hz. A matriz principal de cadência usa
60 Hz temporários, sem escrever configuração persistente do Windows, restaurando
o modo original em finally. O comparativo 30/60/120 continua com os mesmos pedidos;
120 solicitado num display de 60 Hz tem limite físico de 60 apresentações distintas.
Uma medição extra a 120 Hz é necessária para atestar 120 visíveis. CSV de retorno
de Present não prova scanout; registrar separadamente FPS guest e API.
