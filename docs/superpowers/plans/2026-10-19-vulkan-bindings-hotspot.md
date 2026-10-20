# Vulkan bindings hotspot investigation

Objective: locate and reduce the actual CPU cost inside the roughly 35 ms of
global binding preparation, preserving native Vulkan, scale 1, real frames,
resource versions, resolves and draw order. Continue the approved research
recommendation without enabling experimental replay threads by default.

## Execution

- [x] Preserve current 6C0C2CD6 executable and matching MAP, then sample the
  existing recorder/worker/guest threads in observed gameplay. Keep CPU-sampling
  windows separate from FPS benchmarks. Record limitations of leaf-IP samples.
- [x] Resolve samples using the preserved MAP and inspect exact instruction
  offsets before choosing a change. If sampling cannot distinguish the cause,
  add opt-in binding phase counters rather than guessing from the parent timer.
- [x] For the confirmed cause, write a failing semantic/performance-work test,
  implement one targeted change and verify native/Vulkan/GPU correctness.
- [x] Review, build once the candidate is ready, compare real gameplay with
  no concurrent builds, preserve all results including any lack of FPS gain.

No new driver, profiler installation, publication, commit or PR is required.
An aggregate binding timer does not prove descriptor API overhead. CPU copies,
hashes, allocation, driver calls and scheduling stalls remain separate hypotheses.

## Ledger

2026-10-19: user approved the additional research recommendation. Current
production default is serial replay / 32 commands. Preserved
bindings_sample_before.exe + bindings_sample_before.map before any rebuild.
The first action is sampling, not another speculative renderer architecture.

2026-10-20: two 20-second leaf-IP samples found recorder costs in constant
copies and resource lookups. DLL nearest-export names are approximate: the
hot VCRUNTIME RVAs are 0x1ccab/0x1ccf4/0x1cd26/0x1cd2e, an AVX copy body,
not __NLG_Return2. An earlier investigation note inspected the wrong RVA
(0x1e6b0, a fill body); that interpretation is discarded. Raw stack words
are return-address hints, not unwound stack frames.

Added opt-in SR_VULKAN_PROFILE phase counters and built F9B48306. Observed
gameplay report: 2851 draws, bindings 34.1 ms; layout 1.9, streams 5.3,
page 0.6, constants 9.2, textures 15.1, finish 1.5 ms (rounded). This
identifies texture preparation as the largest subphase; driver API cost
alone is not established. These are elapsed CPU times including profiling.

Targeted candidate: two recent exact fetch descriptions per guest slot,
with a live ResourceStore lookup before every reuse to detect upload/resolve
replacement. Descriptor ownership protects resource identity until serial
retirement; caches reset on BeginSubmission. Sampler, dimension and gamma
are covered by the full fetch comparison. SR_VULKAN_TEXTURE_BINDING_CACHE=0
disables reuse for A/B. A failing GPU work assertion was captured before
the cache (Repeated textures exceeded preparation budget). The initial
validation invocation lacked VK_LAYER_PATH and is not the red proof.

Green run: 108 Vulkan unit tests passed; full GPU global contract passed
with validation_active=1 and validation_errors=0, including same-ID churn
and two pending resource versions. Fresh code review found no issues.
Final strengthened churn-count assertion, game build and FPS comparison
are still pending at this ledger entry.

2026-10-20 final: strengthened churn assertion passed (258 preparations,
zero reuses); repeated draws used 2 preparations / 2998 reuses. Candidate
D3DBDACA built and passed GPU validation, 0 errors. Disabled-cache negative
control failed the budget assertion. Clean same-executable A/B real FPS:
off 13.129 idle / 12.305 held-forward; on 13.025 / 12.490. No meaningful
FPS gain demonstrated; 60 FPS remains unmet. Cache stays enabled to retain
the confirmed preparation-work reduction. See
`docs/native-vulkan-texture-reuse-results-2026-10-20.md` for limitations and
the overlapping CPU timeline. No builds/tests/sampling ran in FPS windows.
