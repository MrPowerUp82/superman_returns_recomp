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
