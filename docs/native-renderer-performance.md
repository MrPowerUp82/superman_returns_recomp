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

## Sixth pass: cover mip-only writes in D3D12 (2026-10-08)

The writer-coverage investigation found a concrete validation gap: D3D12
decoded all available mip levels but watched and hashed only the base-level
range. A write confined to a separately allocated mip tail could leave a
cached GPU texture stale even though its base hash remained unchanged.
This was established by code inspection and a synthetic regression case,
not by attributing a specific gameplay artifact to the gap.

D3D12 entries now retain the ranges produced by `DescribeTextureRanges`, the
same source description used by the Vulkan capture path. The renderer arms
watches for all ranges before reading, takes a sequence before arming any
range, and hashes those ranges in order before decoding. Revalidation and
audit checks cover the entire range list, including post-hash notification
checks. A changed hash still retires the old GPU resource and descriptor
through the existing fence lifetime before installing the replacement.
The original base-size threshold and checking cadence remain unchanged;
this pass does not remove hashes or enable the shared-constants experiment.

Hash byte counters now include mip ranges. Packed mip ranges can overlap and
are currently hashed individually, just as in the capture description. This
adds correctness coverage and may increase CPU cost; it is not an FPS
optimization. Coalescing these ranges and proving writer coverage remain
separate work.

Build validation also exposed existing graphics-system split errors. The
interface include was moved outside the project namespace, existing factory
callers retain a compatibility alias for the renamed implementation, and the
renderer accepts the common interface pointer. The explicit syntax-error test
marker was removed. The separate Vulkan stub was not made the active factory;
the existing implementation still handles the selected API and guest GPU
synchronization. Other pre-existing working changes were preserved.

Local Release binary SHA-256:
`3715567a248da2e8aa1b33638188145b7addae7781999f32db57098af5a318f0`.
Build diagnostics are in `build/native_c6_build.log`; runtime audit/profile
logs and screenshots use `logs/bench_native_c6_{d3d12,vulkan}.*`.

Validation passed: 107 native cases, allocation budget, 88 Vulkan cases and
12 SDK-backed texture layout cases. The latter target was built with
`SR_GUEST_TEXTURE_TESTS=ON` and run directly as `sr_guest_texture_tests.exe`;
it is not registered in CTest. Its new packed-mip regression checks the actual
SDK-generated ranges, demonstrates an unchanged base hash alongside a changed
full hash, and verifies the mip-only write predicate. Other tests cover all
mip-tail bytes, the final cube face, gaps between allocations and wide byte totals.

Both APIs completed idle/forward gameplay with audit enabled, at the same
1280x720 / 30 FPS capped settings. D3D12 reported 27.9/27.9 FPS, Vulkan
22.9/23.9. These runs have no same-state paired baseline, audit adds overhead,
and the runner needed different numbers of opening skips. No performance gain
or precise regression magnitude is established. D3D12 now checks more source
bytes and should not be treated as equivalent work to the base-only baseline.

| API | Completed audit intervals | Hash checks | Hashed KiB | Changes | Unnotified changes |
| --- | ---: | ---: | ---: | ---: | ---: |
| D3D12 | 32 | 2320249 | 347885964 | 790 | 0 |
| Vulkan capture | 21 | 117974 | 21598224 | 743 | 0 |

These observations still do not prove complete write-producer coverage.
The existing coarse start-image gate against the C5 audit images passed,
but scene/effect timing differed considerably: D3D12 PSNR 20.00 dB /
histogram 0.17687, Vulkan 19.98 dB / 0.16423. Manual inspection confirmed
gameplay/HUD; these screenshots are insufficient to certify fine visual
equivalence. The deterministic mip regression provides the targeted evidence.
Summaries: `logs/native_c6_texture_audit_summary.json`,
`logs/native_c6_image_gates.json`, `logs/native_c6_guest_texture_tests.log`.
Reproduction scripts: `build/summarize_native_c6_audit.ps1` and
`build/compare_native_c6.ps1` (Windows PowerShell 5.1).

## Seventh pass: union overlapping texture source ranges (2026-10-08)

Packed mip levels can request the same physical allocation multiple times.
The sixth pass covered these bytes in D3D12, but hashed each level's range
separately. Vulkan likewise repeated hashing, watch setup and snapshot copies
for overlapping source ranges.

`DescribeTextureRanges` now returns the sorted union of its validated physical
ranges. Overlap and adjacency are merged; gaps remain excluded. This shared
description supplies D3D12 validation and Vulkan hashing/capture. The actual
decoder retains its private per-level range list, since source size and mip
number cannot be indexed through the merged list. Snapshots contain the full
union, and each original decoder request remains contained in one captured
range. No mip pixels, checking cadence, graphics setting or resource lifetime
are removed by this change. The hash value changes with range boundaries,
but these hashes are process-local and rebuilt on startup.

The Vulkan backoff size now counts unique bytes rather than duplicate mip
requests. If this moves a texture below the existing 4 MiB threshold, it
receives more conservative periodic validation; this change never uses that
threshold to defer a previously eligible small texture.

Validation passed: Release build, 110 native cases plus allocation budget,
88 Vulkan cases, and 13 SDK-backed texture cases. The packed-mip SDK fixture
requires four level reads totaling 28672 bytes; the owned capture now requires
two union reads totaling 24576 bytes (14.3% fewer bytes, 50% fewer reads), with
identical decoded pixels. Additional SDK comparisons cover cube, volume and
linear mip layouts and decoding after live source mutation. The pure union
tests check duplicates, nesting, overlap, adjacency, allocation gaps, arena
end bounds, idempotence and exact byte coverage over 256 varied input sets.

The same-session benchmark uses the sixth-pass binary as its reference,
SHA-256 `3715567a248da2e8aa1b33638188145b7addae7781999f32db57098af5a318f0`,
saved as `artifacts/native-renderer-performance/native_c7_before.exe`.
The changed Release binary has SHA-256
`1bb9dcbe86aeebd0f6e6c32048ae63c07ab45e3dd8e0ae130373025a7cb4c7e0`.
Both run 1280x720, 100% scale, VSync and the 30 FPS cap on the same
i5-13420H / Intel UHD Graphics. CPU profiling is on; texture audit and the
shared-constants experiment are off. No compilation ran during FPS sampling.

| API / build | Idle FPS | Forward FPS |
| --- | ---: | ---: |
| D3D12 reference | 28.0 | 27.2 |
| D3D12 union | 28.2 | 28.9 |
| Vulkan reference | 20.9 | 21.3 |
| Vulkan union | 19.9 | 21.2 |

Final three 120-swap profiling windows (nested CPU timings):

| Measurement | Reference | Union |
| --- | ---: | ---: |
| D3D12 worker execute (ms/frame) | 35.917 | 32.103 |
| D3D12 constants (ms/frame) | 17.387 | 16.620 |
| D3D12 texture hash (ms/frame) | 7.973 | 8.160 |
| D3D12 hashed KiB/frame | 108430 | 86717.3 |
| D3D12 hash checks/frame | 751.7 | 772.0 |
| D3D12 draws/frame | 2897.0 | 2448.3 |
| Vulkan frontend capture (ms/frame) | 26.323 | 25.470 |
| Vulkan worker execute (ms/frame) | 39.960 | 39.687 |

The D3D12 run hashed about 20% fewer bytes per frame (22% fewer per check),
but submitted 15.5% fewer draws. Hash time did not improve. Lower worker time
and the forward FPS increase therefore do not establish a speedup from range
union; Vulkan FPS also did not improve. The proven result is removing duplicate
source coverage while retaining identical decoded pixels in deterministic
tests. Repeated pairs or replaying identical captured texture inputs are
needed to establish a CPU/FPS gain. Different opening skips and traffic remain
confounders; no reduction of graphics quality was used.

Paired start-image gates passed: D3D12 PSNR 33.69 dB / histogram distance
0.02383; Vulkan 36.36 dB / 0.01038. Manual inspection confirmed gameplay/HUD in
both candidates. The pixel-for-pixel SDK comparisons cover the targeted range
change; neither the screenshots nor these fixtures certify the entire game.

Local artifacts (ignored): `logs/bench_native_c7_{before,after}_{d3d12,vulkan}.log`,
their start/idle/forward PNGs, CSV rows, `logs/native_c7_profile_summary.json`,
`logs/native_c7_image_gates.json`, `logs/native_c7_guest_texture_tests.log`,
and `build/native_c7_build.log`. Summaries reproduce with
`build/summarize_native_c7.ps1` and `build/compare_native_c7.ps1` (Windows
PowerShell 5.1). Both benchmark executables are preserved in
`artifacts/native-renderer-performance/native_c7_{before,after}.exe` and need
the release runtime dependencies beside them when launched.

## Eighth pass: replay identical texture hash inputs (2026-10-08)

The seventh-pass gameplay samples varied in draw count and texture traffic.
An opt-in diagnostic now compares per-level hashing with union hashing on
the same immutable, owned texture snapshots. `SR_NATIVE_TEXTURE_REPLAY=1`
enables it; `SR_NATIVE_TEXTURE_REPLAY_START_FRAME` selects the first eligible
frame (default 1800). It is disabled by default and does not replace texture
validation or GPU resources. D3D12 makes checked diagnostic captures; Vulkan
retains existing frontend captures. No captured texture payload is written
to disk.

Each thread collects unique six-word fetch descriptors, at most 256 accepted
textures / 64 MiB, with a 4 MiB source limit per texture. Collection finishes
after 120 frames from the first accepted sample or 512 examined descriptors
if it has any accepted samples. The decoder's read requests supply the old
per-level spans; the snapshot ranges supply the union spans. Decoding,
allocation and span resolution happen outside the timed hash loops. Owned
snapshots are released after replay. These limits describe retained source
bytes, not peak process memory including temporary decode/capture buffers.

`texture_hash_replay.h` runs nine alternating AB/BA trials, four corpus passes
per policy per trial, and reports the median time per corpus pass. It checks
every timed digest against that policy's untimed digest. Different range
boundaries intentionally produce different chained hashes; cross-policy
equality would be an invalid correctness condition. A second run assigns
union spans to both policies as an identical-policy timing control.

Both real-game corpora completed at frame 1800, with 256 cases each, using
runtime-selected AVX2. These are separate corpora and must not be compared
across APIs as equivalent workloads.

| Capture path | Level / union KiB per pass | Bytes removed | Level / union median ms | Time reduction observed | Identical-control spread |
| --- | ---: | ---: | ---: | ---: | ---: |
| D3D12 | 52424 / 42240 | 19.43% | 2.896 / 2.795 | 3.49% | 3.91% |
| Vulkan | 69316 / 59100 | 14.74% | 4.016 / 3.728 | 7.18% | 3.21% |

Both had 1964 level ranges and 256 union ranges; 199 D3D12 and 198 Vulkan
cases had overlapping bytes. All timed outputs were stable. Union and both
control digests matched within each corpus:

| Capture path | Level digest | Union / control digest |
| --- | --- | --- |
| D3D12 | `C93174AFCF7F94B4` | `FDB9083E01EF26DD` |
| Vulkan | `D3A4C70623F1D141` | `4BC32B6035BF7C56` |

The byte reduction is established on real inputs. D3D12's observed timing
difference is smaller than the identical-control spread; Vulkan's larger
difference is encouraging but only comes from one corpus/run. The control
spread is a descriptive noise check, not a confidence interval. Neither
result establishes an FPS improvement or justifies removing validation.
Snapshots use owned heap storage, and repeated passes warm CPU caches; the
corpus is unique-descriptor weighted rather than draw-frequency weighted.
Original and snapshot first-range pages reported protection `0x4` in both
captures, with no differing original first-range flags observed. This does
not establish equal cache residency or check every page/alias.

Release build passed, with 113 native cases plus allocation budget, 88 Vulkan
cases and 13 SDK texture cases. New replay cases cover distinct partition
digests, input mutation during timing and invalid/empty requests. No build or
heavy tests ran during gameplay measurement. The diagnostic gameplay runs
completed both idle/forward scenarios; their FPS is not a paired speedup test.

Image comparison against C7 passed for D3D12 (21.17 dB / histogram 0.05992).
Vulkan failed the existing character-region chromaticity gate (0.070 versus
0.05), despite global 21.52 dB / 0.02130. Manual inspection shows different
character/cape poses, with the scene and HUD present in both images; this
does not turn the failed gate into a visual-equivalence pass. Thresholds
were not relaxed. A matched-pose comparison remains needed for that claim.

Reproduce using the Release binary and `tools/bench/bench_api.ps1 -Profile`,
with the two environment variables above, API `d3d12` or `vulkan` and name
`native_c8_replay_<api>`. Audit and shared-constants cache were disabled.
Local logs/screenshots use `logs/bench_native_c8_replay_*`; summaries are
`logs/native_c8_texture_replay_summary.json` and `logs/native_c8_image_gates.json`.
Scripts `build/summarize_native_c8_replay.ps1` and `build/compare_native_c8.ps1`
run in Windows PowerShell 5.1; the latter intentionally returns failure for
this Vulkan gate. Build log: `build/native_c8_build_final.log`.

Release executable and preserved `artifacts/native-renderer-performance/native_c8_replay.exe`
SHA-256: `ccd84cf529a4466e9737e755567599a3696aa4de3d3ac360e70ee0d095a42f43`.
The next investigation should measure original guest-memory hash reads with
matched descriptor frequencies, and establish writer/alias/resolve coverage
before making versions authoritative. The current replay measures hashing
alone, not the cost of capture or full rendering.

## Ninth pass: sample original guest-memory hash reads (2026-10-08)

`SR_NATIVE_TEXTURE_SOURCE_PROBE=1` enables an immediate source-cost diagnostic,
disabled by default. It shares the start-frame setting
`SR_NATIVE_TEXTURE_REPLAY_START_FRAME` (default 1800), but is independent of
the C8 replay switch. Every 32nd actual hash check is selected, retaining
repeated textures rather than deduplicating fetch descriptors. D3D12 samples
cached-entry revalidation; Vulkan samples dirty/revalidation checks including
initial loads. Limits are 1024 selected checks, 256 MiB cumulative copied
source bytes, 4 MiB per texture and 120 frames after the configured start.
Checked copies are released after each sample; no payload is saved to disk.

The diagnostic records the original production hash duration, then copies the
same union ranges with `CheckedGuestReads`. It alternates guest/owned hash
order across selected checks and finishes with another guest hash. The same
partition and API-specific production seed are used throughout. Timings enter
the stable totals only when the paired guest hash, owned hash and final guest
hash all match the original production hash. Failed reads and observed changes
are counted separately. Matching hashes do not provide atomic snapshots or
prove complete writer coverage. Paired reads use the same SEH hash wrapper;
the original D3D12 hash retains its existing direct path.

| API | Sample window | Observed checks | Stable samples | Distinct stable base addresses | Stable KiB |
| --- | --- | ---: | ---: | ---: | ---: |
| D3D12 | 1800–1843 | 32737 | 1024 | 470 | 123844 |
| Vulkan | 1800–1920 | 7241 | 227 | 200 | 30064 |

No samples were rejected for observed changes, failed reads or size. The
first range's source and owned page protection flags ORed to `0x4` for both
APIs. This does not inspect all pages or prove alias/cache equivalence.

Mean microseconds per stable sampled check (sums divided by stable count):

| API | Original guest hash | Guest hash after copy | Owned hash | Checked allocation/copy | (Copy + owned hash) / original |
| --- | ---: | ---: | ---: | ---: | ---: |
| D3D12 | 8.459 | 3.413 | 3.149 | 33.377 | 4.32× |
| Vulkan | 11.833 | 5.872 | 5.119 | 54.379 | 5.03× |

The original reads were slower than both subsequent reads. Cache warming
and intervening scheduling are plausible explanations, not isolated causes:
copying necessarily reads the source before the pair. A fast owned replay
therefore cannot be substituted for the original production hash time.
The checked-copy implementation costs substantially more than the original
hash on these inputs. Copy timing includes per-range allocation, zeroing and
`ReadProcessMemory`, but excludes destruction and later upload. This does not
measure an optimized reusable scratch-buffer implementation. No copy path
was enabled for normal validation.

These are short, differently weighted windows from one run per API. D3D12
exhausted the sample count before the benchmark's visible-gameplay detection;
Vulkan's window overlapped visible gameplay. Deterministic stride sampling can
also correlate with traversal order. The figures describe these samples,
not a representative whole-game distribution, confidence interval or FPS gain.
Both APIs completed idle/forward gameplay, with no concurrent build/tests.

The external comparison was revisited in the primary
[UnleashedRecomp video implementation](https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/gpu/video.cpp#L2005).
Its texture lock/unlock hooks expose mapped storage and enqueue an upload on
unlock; the render thread copies that storage to the GPU. This supplies a
concrete producer-driven design to investigate. Our confirmed profile has
vertex/index buffer unlock hooks, but no corresponding identified texture
unlock hook. Adapting the approach requires establishing Superman Returns'
actual texture writers and lifetimes; those hook addresses and object layouts
cannot be transferred from Sonic Unleashed. No external source code was copied.

Validation: Release build, 116 native cases plus allocation budget, 88 Vulkan
cases and 13 SDK texture cases passed. New tests exercise AB/BA callback order,
the final guest bracket, disagreement at every hash observation and read
failures. Image gates against C8 passed: D3D12 30.43 dB / histogram 0.06189;
Vulkan 36.45 dB / 0.00865. Manual inspection confirmed scene/HUD. Rechecking
against C7 passed D3D12 (21.46 / 0.04699), but Vulkan still failed the character
chromaticity region (0.065 > 0.05; global 21.53 / 0.02220). The earlier C7
comparison remains unresolved; a C8-relative pass does not override it.

Logs/screenshots: `logs/bench_native_c9_source_{d3d12,vulkan}.*`.
Reproduce with `tools/bench/bench_api.ps1 -Profile`, the source-probe switch
above, start frame 1800, and names `native_c9_source_<api>`. C8 replay,
texture audit and shared-constants cache were off. The tracked summarizer
`tools/bench/summarize_texture_source_probe.ps1 -NamePrefix native_c9_source`
checks completed counts and writes `logs/native_c9_source_summary.json`.
Image results: `logs/native_c9_image_gates.json`, from
`build/compare_native_c9.ps1` (Windows PowerShell 5.1, nonzero exit because of
the C7 Vulkan failure). Build: `build/native_c9_build_final.log`.

Release and preserved `artifacts/native-renderer-performance/native_c9_source_probe.exe`
SHA-256: `2cbf1a740c5d92773afcf73152c57d192eeb21e1cf4bccb8f9aadb0c6b9cd8c7`.
Next: identify and audit texture write producers, using the lock/unlock path
as a concrete lead. Retain content validation until aliases, video writes,
allocation reuse and resolves are covered. No optimization is enabled by
these diagnostics.

## Tenth pass: identify and observe resource unlock producers (2026-10-08)

The producer-driven route suggested by
[UnleashedRecomp's texture lock/unlock implementation](https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/gpu/video.cpp#L2005)
now has concrete observation points in this game's generated code. No external
implementation or address was copied. The static report reproduces with
`python tools/analysis/texture_unlock_candidates.py --out logs/native_c10_unlock_candidates.json`.
It records instruction-comment hashes and callers and rejects changes to the
two short unlock wrapper shapes.

| Guest address | Evidence / role |
| --- | --- |
| `820F3C18` | Common unlock; exact tail target of both confirmed buffer unlocks. Decrements resource lock count and calls `82106F98`, whose code contains cache-line `dcbf` loops and `sync`. |
| `820FFCC8` | Texture-shaped unlock: reads object words at offsets 32/48, masks page addresses and tail-calls the common helper. |
| `821002F8` | Surface-shaped unlock: obtains the texture through object+24, then performs the same page extraction. |
| `820FFCB0` | Level-zero texture lock candidate; shifts arguments and tail-calls `820FF530`. |
| `821002D8` | Surface lock candidate through object+24; tail-calls `820FF5C0`. |
| `8235CA48` | Locks three outputs, supplies their addresses/strides to `824707B0`, unlocks the outputs and binds three textures. |
| `824707B0` | Four-instruction indirect thunk through the object's vtable at offset 72; actual target and codec identity remain unresolved. |

Only the common helper was added as a confirmed profile role and hooked.
Its original always executes. `SR_NATIVE_RESOURCE_UNLOCK_AUDIT=1` enables
checked, read-only observation of 52 object-header bytes afterward. The
classifier requires texture type, page arguments matching the header and
successful SDK source-range description. These remain structural candidates,
not a general resource-type or write-coverage guarantee. The audit is off by
default. It retains at most 64 callers, 1024 descriptors and 1024 range sets,
emits at most 256 distinct-descriptor details and 32 first-match details, and
reports cumulative counters every 120 frontend swaps. It copies no texture
payload. Diagnostic allocation/locking means its FPS is not a speedup test.

Object-header pages are CPU aliases, while source ranges for rendering use
physical pages. The common helper's arithmetic masks to 29 bits and adds
4 KiB for E/F aliases. The audit now performs that normalization before SDK
range description, preserving the low descriptor bits and rejecting arena-end
overflow. Example: object page `EBDC9000` maps to `0BDCA000`, not `0BDC9000`.
This changes audit metadata only; normal renderer address translation was
not changed.

With `SR_NATIVE_TEXTURE_AUDIT=1` also enabled, real revalidation checks compare
both the entire normalized six-word descriptor and the exact sorted physical
range partition against earlier observed unlocks. Those are separate metrics.
All hashes, validation cadence, backoff and GPU uploads remain unchanged.

Final run, last completed cumulative unlock summaries:

| API | Frontend frame | Unlock calls | Texture candidates | Stored range sets | Matched range sets | Matching validation checks | Full-descriptor matches |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| D3D12 | 3120 | 19365 | 516 | 148 | 50 | 46104 | 0 |
| Vulkan | 2160 | 14931 | 2483 | 148 | 49 | 6296 | 0 |

Both observed 18 callers, with no failed header reads, invalid fetch layouts
or dropped callers/descriptors/range sets. Overall validation-check counters
were 2055746 and 79677. Different opening skips and backend policies mean
these are independent workloads. Exact descriptors still differed even after
page normalization; equality of physical coverage is the established result.
Allocation reuse or alternate views can share these ranges, so a range match
does not establish object identity, a write since the last hash or lifetime.

The three-plane path's actual return PCs `8235CBD4`, `8235CBE0`, `8235CBEC`
were observed 84 times each in D3D12 and 740 times each in Vulkan. Its live
descriptors have dimensions 1280×720 and two 640×360 outputs, consistent with
a planar video producer (an inference, not confirmed codec identity). The
texture-object globals read by this routine are `829761E4/E8/EC`; the object
passed to the producer thunk comes from `829761D4`. The current aggregate
does not prove that every one of these three planes matched a bound range,
nor attribute each hash change to one specific unlock.

Completed content-audit windows saw D3D12 2056864 checks / 238 changes and
Vulkan 79677 checks / 1960 changes, with zero unnotified changes under the
existing physical-watch definition. These windows and unlock snapshots have
different boundaries. No event-by-event coverage percentage is established,
and zero unnotified changes does not prove that unlock notifications alone
would cover writes.

Validation: Release, 120 native cases plus allocation budget, three profile
consistency Python tests, 88 Vulkan cases and 13 SDK texture cases passed.
New tests cover big-endian header extraction, unrelated/short objects, exact
range partitions, alias normalization and arena-end rejection. Profile role
checks now include the project's common helper alongside the original 21
kit roles. Both APIs completed idle/forward gameplay. No builds or heavy
tests ran during FPS sampling.

Image gates against C9 passed: D3D12 33.40 dB / histogram 0.01844, Vulkan
37.65 dB / 0.00510. Manual inspection confirmed scene/HUD. This comparison
does not resolve the older C7 Vulkan gate failure or certify all game visuals.

Reproduce with both audit environment variables above and
`tools/bench/bench_api.ps1 -Api <api> -Name native_c10_ranges_<api> -Profile`.
C8 replay, source probe and shared-constants cache were off. Run
`tools/bench/summarize_resource_unlock_audit.ps1 -NamePrefix native_c10_ranges`
for `logs/native_c10_ranges_summary.json`. It checks category and caller totals
and requires at least one physical-range correlation. Logs/screenshots:
`logs/bench_native_c10_ranges_{d3d12,vulkan}.*`. The first descriptor-only
pilot is retained as `logs/bench_native_c10_unlock_*`; it is not a performance
reference. Gates: `build/compare_native_c10.ps1` (Windows PowerShell 5.1),
`logs/native_c10_image_gates.json`. Build: `build/native_c10_ranges_build.log`.

Release and preserved `artifacts/native-renderer-performance/native_c10_unlock_audit.exe`
SHA-256: `53a6711b57552767a427f7a57b527ad709d643377d4430d5148ea3b99030a9e6`.
Next: resolve the three-plane thunk's actual vtable target at runtime; record
per-resource unlock sequences and content-change checks across initialization,
playback, skipping, destruction and allocation reuse. That evidence is needed
before any producer-based version can replace content validation. No FPS
improvement or authoritative invalidation policy is claimed by this pass.

## Eleventh pass: identify the planar frame handoff (C11)

The common-unlock audit now samples the producer object at `829761D4`
after the three successful plane-unlock return sites (`8235CBD4/E0/EC`).
Checked guest reads follow the current object's vtable and slots +72/+76.
This remains behind `SR_NATIVE_RESOURCE_UNLOCK_AUDIT=1`, with a 16-tuple
limit and unreadable/overflow counters. Hash validation and uploads are
unchanged. Sampling after return does not trace the actual indirect call
or establish object identity across reuse or concurrent writes.

Both APIs observed vtable `82050038`, slot +72 `8247E9D0`, slot +76
`82480D80`. Static inspection of the owner's generated PPC establishes:

- `824707B0` dispatches through slot +72.
- `8247E9D0` is a seven-instruction adapter: r6 keeps the descriptor,
  r4 receives descriptor word 1, r5 points to descriptor+8, then slot +76.
- `82480D80` has 717 instructions. Its planar branch (descriptor word 0
  equals 1) obtains a source buffer through another object, copies to
  descriptor pointers +12/+16/+20, and handles contiguous versus row-strided
  output (+36/+40/+44). Width/height come from object+232/+236; chroma is
  half-size in both dimensions. A separate branch for value 255 performs
  vector conversion to packed output. The routine is a frame handoff/copy,
  not a confirmed codec decoder and not a safe whole-function memcpy target.

`texture_unlock_candidates.py` now verifies the seven-instruction adapter's
shape and reports both sampled targets; optional `--target HEX` adds other
observed functions. The runtime summarizer checks that producer samples plus
read failures/overflow equal the three successful plane callers' totals.
`bench_api.ps1 -Profile` now preserves the game log in `finally`, including
failed gameplay/image checks, before another invocation clears `game.log`.

Per-cache-entry unlock/hash sequencing is still pending. A single global
range baseline would confuse views and reused allocations. The next step
is to observe the handoff's entry/exit, descriptor and return result, then
classify first observation, content change, intervening unlock, concurrent
unlock and reuse separately for each cache entry/API. No hash bypass or
FPS gain is claimed. Checkpoint 14 records validation and artifacts.

Final runs: D3D12 2247 producer samples at frontend frame 3720; Vulkan
2211 at 2280. Each API retained one tuple, with zero failed reads/drops;
caller totals matched. These are unlock-site samples, not decoded-frame
counts. D3D12 was already at 2247 by frame 2160; this route's activity was
concentrated before the end of gameplay, so no gameplay bottleneck is
established. Texture audits retained all hashes: D3D12 1992072 checks /
1962 changes, Vulkan 87375 / 1952, zero physically unnotified changes.
There is still no event-by-event proof that unlocks cover those changes.

Release and existing native/allocation, Vulkan, profile and SDK texture
suites passed. Final idle/forward gameplay runs passed both APIs. The first
Vulkan attempt failed its final HUD assertion and was discarded; its images
are retained as `bench_native_c11_chain_vulkan_failed_*` and
`vulkan_not_gameplay.png`. The old script lost that attempt's log; the new
`finally` preservation fixes this for future runs. No builds/heavy tests
overlapped FPS sampling. Image gates against C9 passed: D3D12 34.09 dB /
0.02725 histogram, Vulkan 39.26 / 0.00865; scene/HUD manually inspected.
The old C7 Vulkan image-gate failure remains unresolved.

Logs: `logs/bench_native_c11_chain_{d3d12,vulkan}.*`; summary command
`tools/bench/summarize_resource_unlock_audit.ps1 -NamePrefix native_c11_chain`.
Static report: `logs/native_c11_unlock_candidates.json`; image gates:
`build/compare_native_c11.ps1`, `logs/native_c11_image_gates.json`.
Build: `build/native_c11_producer_build.log`. Preserved Release:
`artifacts/native-renderer-performance/native_c11_producer_audit.exe`, SHA-256
`8c9c641dddfe58cedadde132773c3df1be2f612b5d7f39e4a8e5b944048e927e`.

## Twelfth pass: correlate handoff returns with their three unlocks (C12)

The four-instruction `824707B0` dispatch thunk now has an observation hook,
appended as profile role `FRAME_HANDOFF` (23 roles total, old indices kept).
The static report verifies its exact shape. With resource-unlock auditing
off, the hook delegates directly to the original. With auditing on, it
checks the object, current vtable slots +72/+76 and 48-byte output descriptor
before calling the original, then records the actual returned r3. No mutex
spans guest execution; no guest register, memory or hash policy is changed.

Each entry receives a unique increasing identifier. Eligible planar calls
from `8235CB5C` with the C11 target pair are retained per thread after return.
The three sites `8235CBD4/E0/EC` each consume their designated destination
once, requiring page agreement between the locked output pointer and the
resource's base page (including CPU aliases). Mask 7 completes the group.
Failure-path sites clear pending state; a replacement entry counts an
abandoned group. Nested calls invalidate associations. Duplicate planes,
wrong destinations and unattributed unlocks are counted separately. Storage
is one pending observation per thread and detailed logs cover only the first
12 identifiers. Return sign is counted separately: nonnegative does not
prove that a frame's bytes changed.

The identifier orders entries, not completed writes: different threads can
return in a different order. Page agreement also does not establish resource
identity across reuse. This pass establishes observed call/unlock ordering,
not complete producer coverage or hash-change attribution. Existing hashes
remain active. The next step is per-cache-entry/API validation baselines,
capturing producer/unlock state on both sides of each content check and
classifying first observation, concurrent writes, views and reuse explicitly.
No FPS improvement or hash/upload optimization is claimed.

Three new native tests cover descriptor endian/length/strides, alias and
arena-boundary page agreement, and rejection of duplicate/wrong/packed planes.
The summarizer checks return categories and the three plane callers' totals
against complete/partial/missing associations. Checkpoint 15 records runtime
results, images and reproducible artifacts.

Final D3D12 journal at frame 3720: 268 entries/returns, 268 complete groups,
804 matched planes. Vulkan at frame 2400: 262 entries/returns, 262 groups,
786 planes. Both had zero failed reads, unsupported calls, nesting,
abandonment, destination mismatches or unattributed successful-site unlocks.
All returns were nonnegative. Initial detailed sequences record individual
destinations matching the three resources and strides 1280/768/768; half-size
chroma must not be assumed to have a 640-byte stride. Texture auditing still
hashed content: D3D12 2309192 checks / 790 changes, Vulkan 110693 / 770,
zero physically unnotified changes. These aggregates do not attribute each
hash change to a specific handoff.

Release, 123 native cases plus allocation budget, 88 Vulkan cases, three
profile Python tests and 13 SDK texture cases passed. Static shape checks and
PowerShell parsing passed. Both APIs completed idle/forward gameplay without
builds or heavy tests overlapping FPS sampling. Gates against C9 passed:
D3D12 32.36 dB / histogram 0.05854, Vulkan 37.45 / 0.01023; scene/HUD manually
inspected. The C7 Vulkan gate failure remains unresolved. The initial D3D12
attempt closed its window before any producer entry and was discarded;
its log is `bench_native_c12_handoff_d3d12_closed.log`. Retrying the same build
passed; the discarded log does not record a hook crash.

Reproduce with both resource-unlock and texture audits on, other experiments
off. Logs/images: `logs/bench_native_c12_handoff_{d3d12,vulkan}.*`; run
`tools/bench/summarize_resource_unlock_audit.ps1 -NamePrefix native_c12_handoff`.
Static report: `logs/native_c12_unlock_candidates.json`; gates:
`build/compare_native_c12.ps1`, `logs/native_c12_image_gates.json`.
Build: `build/native_c12_handoff_build.log`. Preserved Release:
`artifacts/native-renderer-performance/native_c12_handoff_audit.exe`, SHA-256
`60d5f042a0c3afad50e38e115604bfb282838d484b01656706be13c78ea99008`.

## Gameplay optimization: reuse clean texture page scans

The planar-video investigation is closed for gameplay optimization. In C12,
handoff counters stayed unchanged throughout the gameplay measurement while
D3D12 still spent roughly 7.5–8.1 ms/frame hashing textures, and Vulkan spent
roughly 10 ms in texture capture even in sampled frames with negligible hash
time. The identified video producer does not justify replacing general
texture validation. No further checkpoint or producer-version policy was
added. Existing diagnostic hooks remain off by default.

Vulkan capture repeatedly scanned the same texture's watched pages for many
draws in one frame. It now memoizes only a *clean page scan*, keyed by frontend
frame, texture watch baseline and a completed-notification counter. A frame
change, changed baseline or completed physical-write notification forces a
fresh page scan. Dirty results are never memoized. Periodic content hashing,
backoff, alias handling and snapshot/upload policies are unchanged. This is
not a content cache or a hash bypass.

The notification counter is published after all per-page notifications, and
sampled before scanning. A notification that finishes during a scan changes
the next lookup's key. The existing `write_seq_` is incremented before pages
are published, so using it for this shortcut would be incorrect. The new
counter describes notification publication, not completion of guest data
writes. Cached scans have a valid observation point before any later callback
completion; the next lookup observes the new counter and rescans.

The optimization is on by default; `SR_NATIVE_TEXTURE_WATCH_SCAN_CACHE=0`
selects the original repeated scan for same-binary comparisons. Both modes
include the completion counter and profiling. Handoff/texture/source/replay
audits and the shared-constants experiment were off during FPS sampling.
The D3D12 scan/hash policy was not changed; it only publishes the additional
notification counter and was not benchmarked in this comparison.

Three new tests exercise clean-result reuse, invalidation by frame/baseline/
completion, repeated dirty results, and notification completion during a scan.
Release, 126 native cases plus allocation budget, 88 Vulkan cases, three
profile Python tests and 13 SDK texture cases passed. No builds/heavy tests
overlapped the gameplay measurement windows.

Same-binary Vulkan comparison, in OFF/ON/ON/OFF order, with the existing
1280x720 / VSync / 30 FPS / native scale 1 benchmark settings. Each idle and
forward window lasted 20 seconds and passed the final gameplay assertion:

| Run | Idle FPS | Forward FPS | Capture CPU ms/frame |
| --- | ---: | ---: | ---: |
| OFF 1 | 22.2 | 23.8 | 23.065 |
| ON 1 | 25.1 | 25.7 | 18.470 |
| ON 2 | 22.3 | 23.6 | 20.635 |
| OFF 2 | 19.8 | 22.5 | 24.585 |

Capture CPU figures are medians of the last six 120-frame profile windows
per run: reductions of 19.9% and 16.1% in the adjacent pairs. Average idle
FPS across repeats rose from 21.0 to 23.7; forward from 23.15 to 24.65.
The mean of the four scenario averages rose from 22.075 to 24.175 FPS
(9.5%). Both adjacent pairs improved in both scenarios, including the
reversed ordering. These are local observed gains, not a guarantee for
other scenes, machines or uncapped settings; the between-run variation is
visible in the table.

The last six sampled capture frames per run corroborate the mechanism:
OFF has no reused scans; ON 1 reuses 168458 of 186330 (90.4%), ON 2 reuses
174311 of 191875 (90.8%). The median texture-capture time of those individual
sampled frames falls from 9/12 ms to 4/4 ms. These sampled-frame figures
are separate from the 120-frame CPU profile averages above. All original
content checks remain scheduled; no producer identity or new write-coverage
assumption is used to skip a hash.

Image gates passed for ON 1 versus OFF 1 (34.45 dB / histogram 0.02903) and
ON 2 versus OFF 2 (35.53 / 0.00628). Scene/HUD manually inspected in both
ON runs. This does not resolve the older C7 Vulkan image-gate failure.

Current executable: `port/out/build/win-amd64-release/superman_returns.exe`.
Preserved copy: `artifacts/native-renderer-performance/native_texture_watch_scan_cache.exe`;
SHA-256 `e2c181e5a7f5d4a5212c04401533270cb2c5cdc5c682bbc8c174989b9fc8daab`.
Use the Release runtime dependencies. No distribution executable was replaced.
Build log: `build/texture_watch_scan_build.log`; SDK tests:
`logs/texture_watch_scan_guest_tests.log`. Benchmark logs/images:
`logs/bench_watch_scan_{off1,on1,on2,off2}_vulkan.*`, CSV `logs/bench_results.csv`.
Analysis: `build/summarize_watch_scan.py` -> `logs/watch_scan_comparison.json`;
gates: `build/compare_watch_scan.ps1` (Windows PowerShell 5.1) ->
`logs/watch_scan_image_gates.json`. Runtime artifacts/analysis helpers are
local and ignored. Earlier changes retained; no commit or publication.
