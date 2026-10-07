# Native renderer: generic CPU optimizations (2026-10-06)

This pass reduces CPU work in the existing native renderer without changing
resolution, shader quality, texture validation cadence, or GPU resource lifetime.
It does not select settings for one notebook or GPU vendor.

- Both APIs transfer completed commands into the worker batch instead of copying
  their shader/texture references and diagnostic strings.
- Both APIs use the existing runtime-selected XXH3 implementation for bulk buffer
  hashing. D3D12 texture validation now uses that implementation too. AVX2 is used
  only when supported by the CPU and OS; the baseline remains available. Seed zero
  produces the same content hashes. `SR_NATIVE_HASH_NO_AVX2=1` forces the baseline.
- Vulkan replay and packet decoding share one immutable captured-memory snapshot.
  Packet ownership still survives source-batch recycling. Capture reserves the
  complete byte and range arrays once, after validating all input ranges.
  Overlapping reads still use the latest range containing the entire request.

## Validation

Release build with both native APIs succeeded. Native tests: 94 cases plus the
allocation-budget executable; Vulkan tests: 87 cases. A new replay regression
checks PM4 constants and overlapping captured bytes after batch reuse. Existing
tests cover invalid bounds, missing dependencies, and exact AVX2/baseline hashes.

The allocation-budget test uses 32 captured ranges of 2 KiB and compares replay
against a single decode. Before the change it failed: replay allocated 411,416
bytes/command versus 205,708 for decode. Afterward both allocate 66,023 bytes on
this toolchain (84% less allocation traffic in replay). The test compares the
relative budget, not a fixed ABI-dependent size or a wall-clock threshold.

Run with:

```powershell
cmake -S tests/native -B build/tests-native -DCMAKE_BUILD_TYPE=Release
cmake --build build/tests-native
ctest --test-dir build/tests-native --output-on-failure
```

## Gameplay measurements

Local system: Core i7-10750H, NVIDIA RTX 2060. Both API logs confirm the NVIDIA
adapter. Benchmarks use `tools/bench/bench_api.ps1`, a 1280x720 window, native
render scale 1, MSAA 1, FXAA off, VSync on, and a 30 FPS cap. Each gameplay
window lasts 20 seconds. Logs and screenshots stay under ignored `logs/`.

Results are recorded in `logs/bench_results.csv`. The first Vulkan baseline ran
while native tests compiled; the `generic_base_vulkan_repeat` run removes that
confound. New Game camera motion, traffic, and timing vary between runs, so these
samples do not establish performance across every scene or hardware.

| API / run | Idle average FPS | Forward average FPS |
| --- | ---: | ---: |
| Vulkan before, repeat without concurrent compilation | 7.0 | 8.0 |
| Vulkan after | 8.0 | 8.2 |
| D3D12 before, first run | 16.4 | 14.9 |
| D3D12 after | 14.2 | 14.4 |
| D3D12 before, repeated immediately after the new build | 13.6 | 15.0 |

D3D12 has no conclusive overall FPS improvement: the later before/after samples
move in opposite directions for idle and forward, and the original executable's
idle performance varied substantially between its two runs. The optimizations
preserve exact rendering behavior, but these samples cannot separate a small
D3D12 speed change from run-to-run and system-state variation. No thermal cause
was measured or established. D3D12's image gate passed (PSNR 30.9 dB, histogram
distance 0.019 against the first baseline).

The last three gameplay worker profile windows show replay decreasing from
78.8 / 56.7 / 61.0 ms per frame to 53.4 / 39.2 / 41.1 ms, with similar packet
counts (2914 / 2324 / 2645 before; 2926 / 2287 / 2652 after). This supports the
targeted CPU-stage improvement, but capture and Vulkan command recording remain
expensive. It does not imply that the full frame time decreases by that amount.
The per-API Vulkan image gate passed (PSNR 35.0 dB, histogram distance 0.017).

Executable SHA-256 before:
`c988f7f5c8c63beff06fe3d08e9d2b7c3858ac71d77dc229d8dfe21a5e182e8e`.
After:
`95b2df56fa7055ac43f9bebacb1f907b1c86162be909071d010b0455fa76503f`.

The updated executable is in `port/out/build/win-amd64-release/`.
The original executable is retained locally in
`artifacts/native-renderer-performance/superman_returns_before.exe`.

## Second pass: constants, declarations, and CPU profiling

The next pass adds CPU instrumentation to both APIs and removes repeated state
work. `SR_NATIVE_CPU_PROFILE=1` enables it; `SR_VULKAN_PROFILE=1` also enables it
so `tools/bench/bench_api.ps1 -Profile` works for both APIs. Every 120 swaps it
reports frontend capture and worker waiting, plus worker execution, D3D12 draw
preparation, constants and vertex-stream binding. Worker measurements are nested:
constants belong to preparation, and preparation/streams belong to execution.
Do not add these numbers or add frontend and worker times: the threads overlap.
Frontend counters are shared under the recording lock to include commands from
different guest threads. Profiling is disabled by default.

PM4 float-constant versions now change only when raw register bits change.
An initial zero write still marks the register written. Signed zero and distinct
NaN payloads remain distinct raw values. Sanitized VS/PS banks are cached inside
each mirror and refreshed by content version; packets copy those arrays and
never borrow a mutable bank. D3D12 keeps its existing submission-local upload
reuse, while Vulkan avoids re-sanitizing unchanged banks on every packet.

Vertex declarations are cached by owned byte contents, with full equality
checking after hashing. Each recording thread has a bounded 128-entry cache;
guest pointer identity is not a cache key. The shared decoder preserves element
order, endian conversion, semantic metadata, and the terminator. Bulk reads are
used only if no newer partial capture overlaps them; otherwise the original
scalar read precedence is preserved. Captured declarations remain independent
of guest memory and source-batch reuse.

Validation: 99 native cases plus the allocation-budget test, 87 Vulkan cases,
and the combined native release build passed. Tests cover repeated writes,
zero-written state, raw NaN/infinity/signed-zero preservation, bank refresh/reset,
declaration content changes/address reuse, and later partial capture overlays.
Independent static review found no actionable correctness issues.

Benchmarks named `native_c2_before_*` overlapped compilation and are diagnostic
only; they are excluded from FPS comparisons. Clean runs use the
`native_c2_clean_*` prefix, with no builds running alongside gameplay.

Second-pass reference executable SHA-256 (instrumentation only):
`5d2a0cb48239ec1cd720fd5f254780c86fdc2abc8b91a1a56c6699ce817d337d`.
Optimized executable SHA-256:
`18988b89f31ad1d258cf4bdc201c434072cc1cea752a501de63af062360f9297`.

### Second-pass measurements and limits

| API / executable | Idle average FPS | Forward average FPS |
| --- | ---: | ---: |
| D3D12 reference, verified gameplay repeat | 15.2 | 14.7 |
| D3D12 state caches | 14.3 | 14.1 |
| Vulkan reference | 7.8 | 8.5 |
| Vulkan state caches | 8.4 | 7.9 |

The first clean D3D12 reference (`native_c2_clean_before_d3d12`) is also
excluded: inspecting its start screenshot revealed an opening cinematic, which
the current HUD detector incorrectly accepted. Its image gate failed because
it compared different scenes. The replacement `native_c2_clean_repeat_d3d12`
start screenshot was manually checked and contains the gameplay HUD and city.
Both reference and optimized replacement images then passed the image gate:
D3D12 PSNR 29.45 dB / histogram distance 0.0416; Vulkan 38.97 dB / 0.00548.
These checks cover the sampled scene, not every rendering path.

This second pass does **not** demonstrate a consistent FPS improvement.
D3D12 decreased by 5.9% idle and 4.1% forward in this comparison. Vulkan
increased by 7.7% idle and decreased by 7.1% forward. A single scene/run is
insufficient to establish a general regression or gain; the implementation
must not be presented as a verified FPS win or ready for release on that basis.
The opening camera and traffic are not deterministic, and the HUD detector has
a known cinematic false positive. Future comparisons should use a deterministic
scene/replay, strengthen that detector, and collect repeated paired samples.

D3D12's last three reference worker windows were 61.77 / 63.10 / 68.43 ms,
with constants at 24.53 / 24.81 / 26.34 ms. The optimized last three were
62.56 / 66.87 / 70.91 ms, constants 24.00 / 23.82 / 27.11 ms: no clear
CPU-stage reduction here either. Vulkan reference worker windows were
50.84 / 75.30 / 63.09 ms versus 71.34 / 50.15 / 67.03 ms optimized.
The useful result is the instrumentation and narrower measured targets;
constant preparation, capture/replay, and Vulkan recording remain costly.
Profiling samples were enabled for both builds; timings are nested and must
not be summed across stages or threads.

Fresh verification after measurement: native cases and allocation budget,
Vulkan cases, and `git diff --check` passed. The reference executable is
retained in `artifacts/native-renderer-performance/native_c2_before.exe`;
the current local release executable includes the state-cache experiment.
No release was published.


## Third pass: shared buffer invalidation and Vulkan capture

The shared frontend now registers the physical write watch before its first
command and before starting the worker. Vulkan previously missed registration
because it installs a packet sink without entering D3D12 initialization.
The PM4 dependency scanner constructs its 92,692-byte fallback mirror only
when the caller does not supply one. Small buffers (up to 32 KiB) confirm
page notifications by hash before discarding captured clean intervals. This
avoids uploads caused by neighboring allocations while preserving physical
writes within a frame, explicit Unlock invalidation, and the per-frame virtual
alias fallback in both APIs.

Vulkan partial updates also use an exact-size device-local buffer pool,
retaining at most 64 MiB of Vulkan allocation bytes. Reuse requires the pool
be the sole owner: active IDs, pending submissions and descriptor uses all
prevent mutation of an older version. Static initial uploads remain outside
the pool; allocations exceeding the budget use the existing lifetime.

This pool was necessary: enabling the watch alone increased detected buffer
updates and exposed repeated driver allocation costs. That intermediate run
(`native_c3_after_vulkan`) regressed to 6.0/5.2 FPS and is rejected as a standalone
optimization. With pooling but before filtering neighboring page writes,
`native_c3_pool_after_vulkan` reached 9.3/9.3 FPS. The final measurements include
both changes.

Third-pass reference executable SHA-256:
`18988b89f31ad1d258cf4bdc201c434072cc1cea752a501de63af062360f9297`.
Final executable SHA-256:
`5af0ac1971a3560c3978bf2a2803be5962184d0582950c90dd63a4083c50b0bc`.
Benchmarks are `native_c3_final_before_*` and `native_c3_final_after_*`.
The final binaries ran before their reference binaries, without concurrent
compilation, at 1280x720 with the same graphics configuration and profiling.
Start screenshots were manually inspected for gameplay rather than cinematic.

| API / executable | Idle average FPS | Forward average FPS |
| --- | ---: | ---: |
| Vulkan reference | 7.8 | 8.2 |
| Vulkan third pass | 10.8 | 11.4 |
| D3D12 reference | 15.1 | 15.1 |
| D3D12 third pass | 13.9 | 15.0 |

Vulkan improved 38.5% idle and 39.0% forward in this local pair. This is one
pair, not a hardware-independent percentage or proof for all scenes. Traffic,
opening timing and draw counts vary. D3D12 did not demonstrate an FPS gain: idle decreased 7.9%, while forward
was effectively unchanged (-0.7%). It must not be advertised as a D3D12
FPS improvement or a release-ready optimization for both APIs. The existing
constant/texture binding and worker costs remain targets for further work.
The last two-second D3D12 counter intervals showed 5,448 partial updates in
the reference versus 99 after filtering, but the CPU worker still took roughly
64–69 ms in the final three optimized intervals. Fewer uploads alone did not
remove the remaining bottleneck.

The last three Vulkan capture samples show PM4 at 20–25 ms before versus
5–7 ms after; texture capture at 44–57 ms versus 16–19 ms; texture hash at
30–40 ms versus 0–1 ms; and texture read bytes at 175–192 MB versus 1.7–6.8 MB.
Frontend interval averages were 66.06/86.36/81.59 ms before versus
56.35/38.61/47.24 ms after. Timers are nested and sample different draw counts;
they must not be summed. Buffer updates in the final samples were 12–21 per
frame, versus 229–247 before the neighboring-page filter. Explicit buffer
invalidations and actual hash changes remain effective.

Validation: 101 native cases plus the allocation-budget test, 88 Vulkan cases,
and the combined release build passed. New tests cover same-frame repeated
content changes, unchanged page notifications, frame fallback, persistent PM4
state across commands, and retention/reuse of pending Vulkan buffer versions.
The buffer tests were observed failing before their implementations and passing
afterward. Independent code review found no actionable lifetime or invalidation
issues. Both start-image gates passed: Vulkan PSNR 38.79 dB / histogram distance
0.00901; D3D12 36.85 dB / 0.01226. Fresh native and Vulkan CTest runs passed
after all four final benchmarks. `git diff --check` also passed.

These image checks cover the sampled scene only. Cloth animation across the
whole game, all resolves, and every UI/video path still require broader play
coverage. No graphics setting was reduced and no release was published.
The original reference binary is retained under
`artifacts/native-renderer-performance/native_c3_before.exe`.

## Fourth pass: D3D12 texture-watch reuse (2026-10-07)

Resumed the approved constants/bindings investigation from checkpoint 7. The
existing detailed timers identify texture slots as the largest constants
substage on this machine; hashes remain the largest individual component.
The historical 24–27 ms constants figures are not a baseline for this machine.

`GetTextureSrvIndex` previously rearmed physical page watches whenever it
hashed a cached texture, including unchanged movie/UI textures checked every
frame. It now rearms only after a physical write notification: clean watches
remain protected. Initial creation still arms before reading, and rearming
records its sequence before protecting/hashing, preserving subsequent writes.
Per-frame hashing up to 4 MiB and the existing larger-texture fallback remain
unchanged, including detection of virtual-alias writes needed by codecs/UI.
No hash cache or longer revalidation interval was added.

New profiling reports watch check/rearm time, rearm counts and hash changes
triggering reloads, only when profiling is enabled. These counters cover cached
D3D12 texture revalidation, not initial creation or Vulkan capture. Opening/movie
windows recorded 2–3 rearms and 2–3 reloads per frame. The final three gameplay
windows retained approximately 711 hashes/frame with zero rearms/reloads
(integer interval averages) and 0.083 ms/frame of watch work. Clean protection
is reused while hashes and observed content changes remain active; this does
not validate every video/UI path or every possible concurrent write.

Hardware: Intel Core i5-13420H, Intel UHD Graphics (DXGI device 0xA7A8).
Runs used `tools/bench/bench_api.ps1 -Profile`, 1280x720, 100% render scale,
vsync, a 30 FPS cap, identical graphics options and 20-second idle/forward
windows. Order: reference D3D12, reference Vulkan, changed D3D12, changed Vulkan,
without concurrent builds. Start images show gameplay with the HUD.

| API / executable | Idle average FPS | Forward average FPS |
| --- | ---: | ---: |
| D3D12 reference | 28.8 | 29.9 |
| D3D12 fourth pass | 29.8 | 29.2 |
| Vulkan reference | 22.9 | 24.4 |
| Vulkan fourth pass | 25.2 | 27.1 |

The following averages use each D3D12 run's final three 120-swap profiling
windows. Timings are nested and must not be added together.

| D3D12 measurement | Reference | Fourth pass |
| --- | ---: | ---: |
| Worker execute (ms/frame) | 32.687 | 29.177 |
| Constants (ms/frame) | 14.890 | 13.477 |
| Float constants (ms/frame) | 3.197 | 2.793 |
| Texture slots (ms/frame) | 9.800 | 9.060 |
| Texture lookup (ms/frame) | 6.457 | 6.230 |
| Texture hash (ms/frame) | 5.250 | 5.490 |
| Samplers (ms/frame) | 1.310 | 1.083 |
| Shared remainder (ms/frame) | 1.900 | 1.620 |
| Draws/frame | 2958.7 | 2526.7 |
| Hash checks/frame | 778.7 | 710.7 |
| Hashed KiB/frame | 57070.7 | 54659.3 |

There is **no established FPS gain** from this pass. The changed D3D12 run
submitted about 14.6% fewer draws; lower absolute constants time does not prove
faster preparation. Constants time per draw rose from approximately 5.03 to
5.33 microseconds. Hash time also did not improve. The cap and variable
traffic/opening timing further limit interpretation. Vulkan does not execute
the changed D3D12 lookup path, so its FPS variation cannot be attributed to
watch reuse. Its final frontend capture averages were 22.563 versus 19.683
ms/frame, and worker execute averages were 35.200 versus 31.360 ms/frame,
without a Vulkan-path optimization. Repeated paired runs in a deterministic
scene are needed for a performance claim. The retained change removes redundant
watch setup; it is not a verified solution to the dominant hashing cost.

Validation: combined Release build, 101 native cases plus allocation-budget
test, and 88 Vulkan cases passed. Paired start-image gates passed using existing
global and HUD/character region thresholds: D3D12 PSNR 38.84 dB / histogram
distance 0.00362; Vulkan 37.45 dB / 0.00544. These cover the sampled scene,
not the entire game. No graphics setting was reduced; no release was published.
The report's pre-existing Windows-1252 en dashes were converted to UTF-8.

Local artifacts (ignored by Git):

- `logs/bench_native_c4_{before,after}_{d3d12,vulkan}.log` and corresponding
  start/idle/forward PNGs; FPS rows are in `logs/bench_results.csv`.
- `logs/native_c4_profile_summary.json`: final-three-window averages.
- `logs/native_c4_image_gates.json`: both paired image gates, reproducible with
  `build/compare_native_c4.ps1` using Windows PowerShell 5.1.
- Reference: `artifacts/native-renderer-performance/native_c4_before.exe`
  (run beside release runtime dependencies), SHA-256
  `83910b0f5bffb815fcbae1e42ac3e34719de319181450351acaacf12e5093cb4`.
- Changed: `port/out/build/win-amd64-release/superman_returns.exe`, SHA-256
  `7f0aa9e39185ba23f72114ccc77b3b1a22e066f4634d8ac3c8ecfbd1e3906bb7`.

The next measured target remains texture hashing; any attempt to skip hashes
must preserve virtual-alias content changes.

## Fifth pass: reference-driven experiments and texture audit (2026-10-07)

The reference investigation is recorded in `native-renderer-reference-research.md`.
This pass adapts the dirty-state idea to an exact final-block comparison, rather
than copying game-specific hooks. It also adds opt-in diagnostics for texture
hash changes with and without write-watch notifications. No hash checks are
removed, and no texture producer is considered fully covered.

`SR_NATIVE_SHARED_CONSTANTS_CACHE=1` enables the D3D12 experiment. Its key is
all 4096 final bytes, including derived descriptors and vertex metadata. A hit
reuses an immutable upload allocation; a miss commits only after a successful
upload. Recycling the frame upload ring resets the cache. Root bindings are
still emitted every draw, so resetting a command list does not skip bindings.
The Vulkan constants path does not use this cache.

The first benchmark binary enabled the cache unconditionally (SHA-256
`886aa81066d76b4e53382a474e58cf7b77474899e30b08af5d150e2e550bfbd0`).
It was compared with the C4 binary, on the same i5-13420H / Intel UHD Graphics,
1280x720, 100% scale, VSync and a 30 FPS cap. Audit was off. These are single
pairs, with variable traffic and opening timing.

| API / binary | Idle FPS | Forward FPS |
| --- | ---: | ---: |
| D3D12 reference | 29.9 | 30.0 |
| D3D12 cache experiment | 29.2 | 28.8 |
| Vulkan reference | 23.5 | 23.7 |
| Vulkan experiment binary | 22.7 | 23.2 |

Final three 120-swap D3D12 windows, nested CPU timings:

| Measurement | Reference | Cache experiment |
| --- | ---: | ---: |
| Worker execute (ms/frame) | 31.230 | 34.300 |
| Constants (ms/frame) | 13.803 | 14.853 |
| Shared remainder (ms/frame) | 1.777 | 2.757 |
| Texture hashing (ms/frame) | 5.037 | 5.143 |
| Draws/frame | 2863.7 | 2952.3 |
| Shared compare/upload (ms/frame, nested) | uninstrumented | 1.003 |
| Shared cache hits/frame | unavailable | 1273.7 |
| Shared uploads/frame | unavailable | 1678.7 |
| Upload KiB avoided/frame | 0 | 5095 |

The experiment avoided about 43% of shared uploads, but **did not establish a
performance gain**. Constants time per draw rose from about 4.82 to 5.03 us;
the shared remainder also increased. The final implementation therefore keeps
the original direct upload path by default and exposes the cache only as an
opt-in experiment. Vulkan frontend capture was essentially unchanged,
23.700 versus 23.727 ms/frame; its FPS variation cannot be attributed to this
D3D12-only experiment. No graphics setting was reduced.

Validation: Release build, 104 native cases plus allocation-budget test, and
88 Vulkan cases passed, including after restoring the direct-upload default.
The cache tests change every word, exercise reset and failed-upload behavior,
and check audit classification. Paired start-image gates passed: D3D12
38.91 dB PSNR / 0.00908 histogram distance; Vulkan 34.79 dB / 0.01683.
Manual inspection confirmed gameplay and HUD in both sampled images. These
checks do not cover the entire game or all videos.

`SR_NATIVE_TEXTURE_AUDIT=1` adds diagnostic checks to both texture paths,
including a post-hash notification check to account for concurrent writes.
Aggregate logs appear each 120 frames; up to 64 unnotified-change details are
logged per thread. No-change observations cannot establish writer coverage.
Audit adds CPU/logging work and its FPS must not be mixed with cache measurements.

Separate audit runs of the final binary, with the cache disabled, completed
both gameplay scenarios. The completed 120-frame intervals contained:

| API | Intervals | Hash checks | Hashed KiB | Content changes | Changes without notification |
| --- | ---: | ---: | ---: | ---: | ---: |
| D3D12 | 23 | 1325570 | 104197216 | 1952 | 0 |
| Vulkan capture | 19 | 116541 | 21180848 | 311 | 0 |

These samples include startup and the benchmark's short opening transition;
the runner skips the cinematic. They are not a complete video/write-producer
test. Zero unnotified changes provides no basis to remove conservative hashes.
The final binary's audit-run start-image gates also passed: D3D12 36.42 dB /
0.00841 histogram distance; Vulkan 23.86 dB / 0.17006. The Vulkan comparison
has visibly greater scene/timing variation and is only a coarse regression gate.
Audit-run FPS was 29.8/29.6 D3D12 and 26.6/24.9 Vulkan (idle/forward); it is
recorded for reproducibility, not a performance claim.

Local artifacts (ignored by Git):

- `logs/bench_native_c5_{before,after}_{d3d12,vulkan}.log`, screenshots and CSV rows.
- `logs/native_c5_profile_summary.json`, generated by `build/summarize_native_c5.ps1`.
- `logs/native_c5_image_gates.json`, generated by `build/compare_native_c5.ps1`
  with Windows PowerShell 5.1.
- `logs/bench_native_c5_audit_{d3d12,vulkan}.log`,
  `logs/native_c5_texture_audit_summary.json` and `logs/native_c5_audit_image_gates.json`.
- `artifacts/native-renderer-performance/native_c5_before.exe`, SHA-256
  `7f0aa9e39185ba23f72114ccc77b3b1a22e066f4634d8ac3c8ecfbd1e3906bb7`.
- `artifacts/native-renderer-performance/native_c5_experiment.exe`, the
  unconditional-cache binary measured above.
- Final opt-in build: `port/out/build/win-amd64-release/superman_returns.exe`,
  SHA-256 `a9a766362b64e54d01ceb3c25ea0f55c35826c7808318b2b1132dc881547ac26`.
