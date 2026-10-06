# Native Vulkan game renderer — M3 validation in progress

The opt-in build presents guest draws through the project's Vulkan device and
Win32 presenter. It does not create a D3D12 reserve provider or silently fall
back to Xenos. M3 is still open: opening videos and the animated title have been
observed, but tutorial/meteor HUD, city gameplay, War World parity and the full
window lifecycle have not yet been fully verified in this build. Since 2026-10-05 Vulkan
is the launcher's default API and is part of the release (Direct3D 12 stays selectable);
it is still experimental.

## Building and selecting the backend

Use the existing Windows ReXGlue/LLVM setup, with the SDK GPU sources available.
Configure a separate build directory with `SR_NATIVE=RENDERER`,
`SR_VULKAN_FOUNDATION=ON` and `SR_VULKAN_GAME=ON`. The default build keeps Vulkan
game rendering disabled and `sr_native_api=d3d12`.

```powershell
cmake -S port -B build/vulkan-game -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DSR_NATIVE=RENDERER -DSR_VULKAN_FOUNDATION=ON -DSR_VULKAN_GAME=ON
cmake --build build/vulkan-game --target superman_returns
& build/vulkan-game/superman_returns.exe --sr_renderer=native --sr_native_api=vulkan
```

Native Vulkan currently uses the local M2 shader tooling: Python, the runtime
shader script, patched XenosRecomp corpus executable/common header, and DXC.
Missing tools cause initialization failure. This is a development build, not a
portable packaged runtime. `sr_native_vulkan_gpu_uuid` selects the logged 32-hex
Vulkan UUID; DXGI adapter identifiers are not used for this selection.

The selection policy rejects invalid APIs and Vulkan without the opt-in build.
Unsupported FXAA, SSAO, LSFG, A/B, foliage, render scale and MSAA options are
reported explicitly rather than silently ignored. Use scale 1 and MSAA 1 for
comparison with D3D12.

## Ownership and ordering

The frontend captures PM4 dependencies, original shader containers, buffer
updates and versioned texture bytes before the render worker consumes them.
Neutral packet decoding uses owned ranges and fails on missing data; Vulkan
never reads live guest memory. On Windows the frontend checks and copies each
requested source with `ReadProcessMemory`, preserving nested reader results
until the end of that command. Physical aliases use the SDK physical mapping.

Resources and descriptor leases remain alive until their submission fence
retires. Host-visible buffer reuse requires exclusive pool ownership; pending
submissions and external leases prevent reuse. The pool has a 64 MiB aggregate
budget. Descriptor pools contain up to 128 draws per page. The shader service
uses two production workers, owns request bytes, deduplicates requests and
cancels all child jobs on shutdown.

Render targets retain color/depth/stencil state, regional clears and EDRAM alias
ordering. Color resolves preserve offsets, mip levels and slices. Depth/stencil
resolves use a compute conversion. Presentation selects the actual resolved
guest frontbuffer and takes an immutable mailbox snapshot. Host immediate UI
and guest HUD draws use separate paths on the same Vulkan context.

CPU frame recording runs outside the shared queue mutex. Only queue submissions
are serialized with host presentation, so recording guest draws does not hold
up the window's paint/event loop. The GPU frame fixture verifies both recording
without that lock and submissions with the lock, while checking snapshot pixels.

### Optimizations and fixes from 2026-10-05 (Intel UHD)

**Presentation lock (largest gain).** `NativePresenter::Submit` took
`Host::gpu_mutex` for every packet (77+ per frame). Painting holds that mutex
across present. On Intel UHD, about 95% of the frame went to waiting
(title screen ~0.5 FPS). The `GameFrame` pointer is now published in an atomic
after initialization. `RefreshGuestOutputImpl` writes its mailbox slot without
the mutex, because the SDK guarantees that slot is exclusive to the refresher.
The title screen went from ~0.5 to ~10 FPS (frame ~2,500 ms to 50-160 ms).

**Black speckles in the image (XenosRecomp patch 0013).** The edge-AA post-process
filter (PS `B397B05F4CE5D5BB`) divides by the sum of its weights, which is 0 on
uniform 3x3 areas. The translator emitted `clamp(rcp(x), -FLT_MAX, FLT_MAX)`.
The clamp is still present in the SPIR-V, but the Intel driver removes it, so
`0 * inf` became NaN and those pixels turned black (100% of the speckles were
uniform areas). `0013-no-infinity-scalar-ops.patch` makes `rcp`/`rsq`/`log`
return +/-FLT_MAX for |x| below the smallest normal float, without ever forming
an infinity. That is also closer to Xenos, where `0 * inf = 0`. The D3D12 DXIL
corpus uses the same translator but has not been regenerated yet.

**Frame dump.** `SR_VULKAN_DUMP_FRAME=N` or `SR_VULKAN_DUMP_TRIGGER=<file>`
(dumps the next frame and deletes the file), with `SR_VULKAN_DUMP_DIR`. It writes
`packets.txt` (each draw with VS/PS, targets, depth/blend and textures), a
readback of every resolve destination right after the resolve
(`p<packet>_resolve_<base>.raw`), and the targets at the end of the frame. The .raw
header is u32 width, height, VkFormat and texel size. Use it to locate which pass
breaks the image.

CPU recording costs reduced on the same date:

- Host-visible buffers stay mapped for their lifetime; there is no
  map/unmap per write.
- Upload staging and per-draw constants (3 x 4 KiB per draw) are carved from
  8 MiB mapped chunks per submission. Chunks are reused once the fence retires
  (64 MiB pool per arena). This replaces one `vkAllocateMemory` per upload or
  constant block.
- Inline vertices and expanded indices are written straight into transient
  mapped memory, with no device-local copy. `BufferResource::offset` must be
  honored wherever the buffer is bound.
- `GameFrame` records uploads into a second command buffer submitted ahead of
  the frame. A single barrier at the end replaces one barrier per upload.
  Every upload creates a new version, so moving it earlier is safe.
- Consecutive draws on the same targets share one render pass. Any barrier
  on the recording command buffer (`ImageState::before_barrier`), alias, clear,
  resolve or tiling packet closes it first. Without an upload command buffer
  (`BeginSubmission` without `upload`), the per-draw pass is kept.
- The pipeline store caches the SPIR-V digest and the VS/PS pair validation
  per artifact instead of hashing every draw. Descriptor pools are reset and
  reused. Presentation snapshots are recycled when no mailbox or submission
  holds them anymore.

The `--game-record-merged` fixture covers two draws in the same pass with
uploads moved to the upload command buffer.

Gameplay measurement (work laptop, Intel UHD, New Game, `bench.ps1` with
the Vulkan executable): **5.8 FPS idle / 6.3 FPS walking** (min 5.2 / 3.9),
GPU ~30% busy, so the frame is CPU-bound. In the log (frames >100 ms or every
120th, ~3,100 draws), `record_submit_ms` dropped from ~210 ms (lock fix only)
to an average of 88 ms with the recording optimizations; `fence_ms` averages 35 ms.
On this machine the native D3D12 renderer holds 28-30 FPS, so Vulkan still
spends most of its CPU time recording each frame.

Driver pipeline cache files are separate from D3D12 PSOs and include ABI,
vendor/device/driver and pipeline-cache UUID checks. Checkpoints are written to
a temporary file and replaced during execution, at most once every five seconds
after a submitted frame; clean teardown also saves pending changes. This is
necessary because the SDK's normal window-close path hard-exits the process.
Cache failures are logged and do not substitute missing pipelines.

### FPS parity with D3D12 (2026-10-05, Intel UHD, windowed 1280x720, 30 FPS limit)

Three CPU costs were removed, each measured with `tools/bench/bench_api.ps1` and `SR_VULKAN_PROFILE=1`:

| Step | Idle FPS | Walking FPS | Guest-thread textures_ms | record / fence / queue (ms) |
| --- | --- | --- | --- | --- |
| baseline (`par_base`) | 7.7 – 8.3 | 8.0 – 10.1 | 37 – 46 | 38 / 27 – 34 / 22 – 36 |
| a: texture hash in place (`par_a`) | 10.5 | 10.6 | 22 – 23 | 63 – 67 / 16 – 17 / 6 – 10 |
| b: waits outside the queue mutex (`par_b`) | 16.3 | 13.7 | 23 – 24 | 60 – 63 / 3.4 – 3.8 / 0.0 |
| c: dynamic offsets for constants (`par_c`) | 14.3 | 15.5 | 21 – 24 | 57 – 63 / 2.7 – 2.9 / 0.0 |
| final (`par_final`) | 14.3 | 15.5 | 21 – 24 | 57 – 63 / 2.7 – 2.9 / 0.0 |
| D3D12 (`par_final_d3d12`) | 28.7 | 28.3 | n/a | n/a |

- Texture revalidation hashes guest memory in place (`HashGuestRange`, SEH-guarded); only a changed texture is copied.
- `FrameLoop::DrawGame` takes the queue mutex only around retire, recording, submit and present, so a vblank wait no longer blocks the game's submissions.
- Draw constants (VS, PS, shared: one contiguous 12 KiB block) are bound through `STORAGE_BUFFER_DYNAMIC` descriptors, one set per arena chunk, instead of a descriptor set per draw.

### Cycle 3 final measurements (2026-10-06, same Intel UHD, 1280x720, 30 FPS limit)

| Run (local time) | Idle FPS | Walking FPS | Capture textures / hash (ms) | Record (ms) | Image gate |
| --- | --- | --- | --- | --- | --- |
| `c3_final1` (14:42) | 17.4 | 18.4 | 17–19 / 11–13 | 28.5–30.9 | PASS (21.6 dB, histogram 0.057) |
| `c3_final2` (14:44) | 17.6 | 17.5 | 17–22 / 11–15 | 27.6–31.6 | PASS (21.4 dB, histogram 0.026) |
| `c3_final_d3d12` (14:47) | 25.5 | 27.5 | n/a | n/a | not requested |

The game was rebuilt at `0c87c6c` before these consecutive Vulkan runs. CPU recording fell from the cycle-3 baseline 49.8–56.1 ms to about 28–32 ms, but **the ≥29 FPS goal failed in both scenarios in both runs**. Resource lifetime, exact texture hashes each frame and backend selection are preserved. The approved second round removed further work from resources/descriptors, targets/pipelines and constants/binds; the hash pool was skipped. Investigation identified replay worker (~40 ms), guest capture including PM4 (22–26 ms) and GPU (32–35 ms) as remaining limits. Further worker/capture/GPU or parallel-recording work needs a new user decision.

Final validation: 93 native and 85 Vulkan unit tests, production bindings contract, resource base, 15 GPU fixtures with pixel checks and the hold-lifetime ownership fixture passed; HUD tests passed. The initial image-gate suite exited 1 because its wildcard classified four local screenshots as accepted positives, including D3D12 and previously rejected camera/window/pose captures; synthetic checks and the two current Vulkan gates passed. Separate commit `d5b866f` replaced the wildcard with an explicit calibrated positive/negative inventory; final rerun passed 43 checks with zero failures/skips (13 synthetic/API/CSV plus 30 real-image checks: 19 positive, 11 negative). No thresholds or historical artifacts were changed. Full conditional `verify_vulkan_m3.ps1` was unavailable because `build/vulkan-main` is absent (local shader corpus exists). Validation layers remain absent. A single D3D12 result within historical scene variation does not establish non-regression. Scene parity and window lifecycle remain separate M3 requirements. The [cycle-3 spec](superpowers/specs/2026-10-06-vulkan-fps-parity-cycle3-design.md#resultado-2026-10-06-ciclo-3) records all task measurements and limitations.

## Recorded checks

On 2026-10-04, using the RTX 2060, the native suite passed 89 tests, the Vulkan
suite passed 66 tests, and the texture suite passed 11 tests. Shader Python,
launcher and Windows shader-process Unicode/timeout/cancellation checks passed.
The existing local shader corpus validation recorded 241 ready, zero failed.
The default D3D12 build and opt-in Vulkan build compiled successfully.

Actual Vulkan window QA on the same GPU confirmed fullscreen-to-windowed
presentation (1920x1080 to 1600x900), minimize/restore and maximization
(1920x1051 client extent). The swapchain log recorded generations 1, 2 and 3;
video and host settings remained visible afterward. This does not yet cover
explicit surface reconnect or resize during gameplay.

Production GPU fixtures passed buffer/2D/3D/cube uploads; descriptor remapping;
regional clears; depth/stencil/MRT draws; partial and stacked resolves;
depth/stencil conversion; EDRAM aliases; resolved-texture sampling; mailbox
snapshots; immediate UI; and gamma/letterboxed composition. A new driver-cache
fixture verifies persistence before destruction, replacement and reopening.
Unknown fixture arguments fail instead of silently running the default fixture.
External validation layers were unavailable; these results do not claim an
external Vulkan validation run.

Run the consolidated checks after building the test targets:

```powershell
& tools/verify_vulkan_m3.ps1 -Python python
```

`-SkipCorpus` and `-SkipGpu` produce explicit warnings. Empty shader corpora are
rejected. Automated fixture success does not imply full-game visual parity.

Diagnostic logs separate frontend capture, shader waiting, previous-fence
waiting, queue-lock waiting and recording/submission. An earlier complex frame
spent about 49 seconds scanning memory mappings during texture capture; after
the checked-copy change, a comparable 255 MB/6,524-read frame spent about
0.49 seconds in texture capture. This measures one capture stage, not game FPS.
FPS improvement and D3D12/Vulkan parity still require matched gameplay runs.

## Remaining acceptance evidence

- New Game/tutorial with meteor objective markers and orange minimap triangle.
- Camera/player movement and effects/combat in city and War World.
- Matched D3D12/Vulkan scene and intermediate-pass comparisons.
- Actual resize, minimize/restore, surface reconnect and close behavior.
- Final independent review and material corrections before declaring M3 complete.

The user's War World save remains available. A local backup for the autonomous
New Game QA is under the ignored `build/vulkan-m3-qa-saves` directory.
