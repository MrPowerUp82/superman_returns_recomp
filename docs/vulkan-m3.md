# Native Vulkan game renderer — M3 validation in progress

The opt-in build presents guest draws through the project's Vulkan device and
Win32 presenter. It does not create a D3D12 reserve provider or silently fall
back to Xenos. M3 is still open: opening videos and the animated title have been
observed, but tutorial/meteor HUD, city gameplay, War World parity and the full
window lifecycle have not yet been fully verified in this build. Vulkan is not exposed
in the launcher or included in a release.

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

Driver pipeline cache files are separate from D3D12 PSOs and include ABI,
vendor/device/driver and pipeline-cache UUID checks. Checkpoints are written to
a temporary file and replaced during execution, at most once every five seconds
after a submitted frame; clean teardown also saves pending changes. This is
necessary because the SDK's normal window-close path hard-exits the process.
Cache failures are logged and do not substitute missing pipelines.

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
