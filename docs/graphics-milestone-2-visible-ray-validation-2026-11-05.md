> Renderer integration, real gameplay, benchmark results and their limits are
> consolidated in graphics-milestones-integration-2026-11-06.md. This document
> retains the original standalone helper validation; the final paragraph below
> describes what that initial fixture alone established.

# Visible-ray helper implementation and validation

This implements the existing M2 design in
`graphics-milestone-2-dxr-design-2026-11-05.md`, written before this code.
The added source is `port/src/graphics/d3d12/visible_ray_scene.cpp`; it reuses
`ray_tracing_scene.cpp` for BLAS/TLAS. No renderer or main CMake edits were made
by this worker. The GPU fixture is `tests/gpu/visible_ray_scene_smoke.cpp`.

## Implemented behavior

Build consumes the exact original post-VS float4 clips and the inverse final
raster camera, recovering already deformed world positions on the GPU. Only
caller-attested opaque, same-camera spans enter the scene. Invalid homogeneous
triangles become zero-area triples, preventing NaN/Inf BLAS input. All selected
draws share one world buffer bounded by 4,194,303 vertices (48 MiB). Spans are
grouped into at most two BLAS instances, retaining separate world/character masks.

The trace performs actual inline DXR queries against this TLAS. A reflection hit
is projected through the exact final camera into the retained pre-RT lit image.
Viewport, homogeneous-W, depth range, clear depth, finite values and depth
agreement must pass before sampling hit RGB. This path does not use the
RayTracingScene material-albedo output or an occlusion-only reflection substitute.
Misses or unvalidated hits retain zero confidence, allowing the original
environment response to survive composition.

The full-resolution filter uses depth-bilateral spatial weights and neighborhood
clamping. Consecutive-frame history additionally requires valid M1 motion and
validity, matching dimensions, previous projection, depth reprojection agreement,
and reset=false. The current-to-previous motion is in render pixels. A reset,
missing source or serial gap disables history. Disoccluded reflection misses
are not filled with old/neighbor hit colors. `history_used` reports frame-level
eligibility; per-pixel motion/depth rejection can still reject every sample.

Composite reads the retained original color and filtered outputs and writes a
separate matching-format texture. Shadow attenuation is `.35+.65*visibility`.
Reflections blend validated lit-hit color with Schlick F0=.04 times confidence.
This is an explicit first-version material proxy, not a recovered game BRDF or
an additional material-albedo pass. Original alpha is preserved. Geometry never
submitted/offscreen, translucency, unsupported alpha coverage and lighting not
represented in the retained lit image remain outside this visible-scene method.

## Inputs, formats and lifetimes

Call Initialize once, BeginFrame only after that slot's fence, Build, Trace,
then optionally Composite. Use one ordered DIRECT/COMPUTE queue and wait all
three slots before destruction. Caller supplies streamout/UAV/producer and
cross-queue barriers. Clip buffers and all textures must be shader-readable on
the same device. Inputs are retained by COM reference per slot; source contents
must obey queue ordering. Prior history is retained through the consuming
frame's fence, avoiding release when its producing slot is recycled.

Depth accepts R32_FLOAT, R32_TYPELESS or R24G8_TYPELESS; color accepts RGBA8 UNORM,
RGBA16_FLOAT or RGBA32_FLOAT. Motion accepts R16G16_FLOAT or R32G32_FLOAT;
validity accepts R8_UINT or R32_UINT. Textures must match single-sample, one-mip,
one-layer dimensions. Matching supported formats alone do not attest source
lineage. Unsupported MSAA/depth/color contracts return an HRESULT and preserve
the caller's raster fallback.

Trace outputs shadow R32_FLOAT and reflection RGBA32_FLOAT in
NON_PIXEL_SHADER_RESOURCE; restore this state after any consuming transition
before they become history. Composite keeps these pointers, dimensions and
history metadata, and adds matching-format `.color` in COPY_SOURCE. Caller
transitions its destination and CopyResource's this separate color before
DLSS/HUD. Rebind compute/graphics roots, PSOs and heaps after module commands.
No method waits, creates a thread, copies into the caller's color or adds a fatal.
Four new helper PSO creation attempts are counted by the shared pipeline audit.

## Executed checks

DXC compiled ConvertClips, TraceVisible, DenoiseVisible and CompositeVisible with
cs_6_5, -O3 and -WX. C++20 clang++ syntax/standalone link checks passed without
warnings. The standalone GPU fixture passed on **NVIDIA GeForce RTX 2060**:

- GPU clip conversion and grouped TLAS generate a real blocker shadow.
- Reflection returns the retained lit hit's RGB, with no material supplied.
- Clear-depth/unvalidated projected hits reject color rather than leaking it.
- Spatial/temporal/reset paths are exercised; temporal red was **0.751287**
  against reset/spatial **0.475245**, using clamped valid history.
- Separate composite RGB and alpha match the stated proxy formula by readback.

The D3D12 debug layer was unavailable. Thus hardware assertions and fence
completion passed; debug-layer/GPU-validation coverage is not claimed. An
explicit output-metadata assertion was added after the last hardware execution;
the prior executed fixture already consumed shadow/reflection after Composite.
Two initial temporal-fixture attempts correctly clamped history when the
synthetic sloped plane's neighbors failed the fixture's overly narrow depth
threshold. Adjusting that fixture to depth_rejection=.02 exposed an eligible
neighborhood and produced the successful temporal-vs-reset difference. The
shader was not changed to bypass rejection.

Commands (PowerShell, from repository root):

```powershell
& '.tools/clang+llvm-23.1.2-x86_64-pc-windows-msvc/bin/clang++.exe' -std=c++20 -Wall -Wextra -I port/src/graphics/d3d12 tests/gpu/visible_ray_scene_smoke.cpp port/src/graphics/d3d12/visible_ray_scene.cpp port/src/graphics/d3d12/ray_tracing_scene.cpp -o build/tests-native/sr_visible_ray_scene_smoke.exe -ld3d12 -ldxgi -ldxguid
& './build/tests-native/sr_visible_ray_scene_smoke.exe'
```

Do not run concurrently with game performance trials. No game-scene M2
acceptance, quality screenshot comparison, GPU timestamp performance budget or
three-round M2 benchmark has been established by these synthetic assertions.
