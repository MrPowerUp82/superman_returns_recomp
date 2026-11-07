# Object Motion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect continuous per-object/deformation RG16F motion to DLSS with verified scene lineage and conservative raster fallback.

**Architecture:** Reuse the exact post-VS capture, geometry history, pixel ownership and motion rasterizer. Separate diagnostic two-frame capture from continuous production; qualify CPU coverage and immutable scene content before NGX, while retaining history even when raster is presented.

**Tech Stack:** C++20, D3D12, NGX 3.7.20, HLSL/DXC, native CTest, game benchmark.

**Spec:** `docs/graphics-milestone-1-object-motion-design-2026-11-05.md` (approved 05/11/2026).

## Global Constraints

- No new REX_FATAL; optional failures retain raster output.
- Hooks only through manifest or documented midasm. Reuse current hooks first.
- Preserve default camera-only path for explicit A/B until full coverage qualifies.
- Three arenas, 64 MiB each; at most 4096 draws and 4,194,303 expanded vertices.
- Current-to-previous, render-pixel RG16F, jitter removed, NGX MVLowRes and scale 1.
- No performance measurement concurrent with profiling, dumps, tests or builds.
- Shared temporal contracts must compile without D3D12/Vulkan dependencies.

## Review Focus

- Raster fallback must continue collecting history, allowing recovery next frame.
- Recycled object/mesh/COM addresses cannot establish cross-frame correspondence.
- Guest depth clear after scene must not change the immutable motion input.
- Clear/partial resolve/unknown compositor must break scene lineage before NGX.
- Continuous capture must not produce unbounded diagnostics/readbacks or duplicate jitter.

## Task 1: Continuous motion readiness and version lineage

Files: new `port/src/graphics/object_motion_frame.h`, new
`tests/native/test_object_motion_frame.cpp`, `tests/native/CMakeLists.txt`.

Interface: `ObjectMotionFrame` accumulates source draw eligibility/failure and
final scene/depth ticket, reports explicit readiness reason and resets on every
frame. Versioned color lineage propagates only through explicit audited writes.

- [x] Add failing tests for first-frame fallback/recovery, unsupported contributors,
  version mismatch, frame gaps, partial copies and changed source content.
- [x] Run native tests to observe those failures before implementation.
- [x] Implement bounded, API-neutral policy; keep unknown inputs rejected.
- [x] Build native tests and run the complete native CTest suite.

## Task 2: Continuous GPU producer

Files: `native_renderer.h`, `native_renderer.cpp`, `native_motion_capture.inc`.

Interface: a continuous request independent of diagnostic capture; current-frame
RG16F/validity/depth resources and qualification report tied to one scene ticket.

- [x] Add opt-in `sr_native_object_motion` for A/B and staged validation.
- [x] Start capture each requested frame, enable audited skin/cape paths, preserve
  previous slots and history through raster fallback, reset on mode/scene changes.
- [x] Render every eligible scene, pool immutable depth per fenced frame slot;
  leave all diagnostic-only dumps/attribution behind the two-frame trigger.
- [x] Use actual DLSS jitter once, and record the same offsets in motion.
- [x] Count every scene contributor and reject skipped/unsupported writes.
- [x] Validate CPU contracts and build the complete game with DLSS On/Vulkan On.

## Task 3: Scene lineage and NGX consumer

Files: same renderer files, `graphics/d3d12/shaders/dlss_inputs.hlsl` and generated
header if a composition pass is needed; existing scene-boundary contracts.

Interface: validated frame report -> frozen RG16F/depth resources -> NGX
`DlssEvaluation`, with original camera-only pipeline used only in explicit A/B.

- [ ] Track actual write/resolve versions and original texture bindings through
  the audited HDR/tonemap/filter path; retain actual depth snapshot.
- [x] Reject unknown/partial/changed lineage; never promote by size/hash alone.
- [ ] Compose object vectors only with proven static/sky fallback; otherwise
  reject the whole frame before NGX and invalidate its temporal history.
- [x] Restore renderer state after producer/NGX; retain submitted resources
  through fence; publish actual readiness/coverage/rejection counters.
- [x] Run motion fixtures and no-SDK/native tests; confirm NGX flags locally.

## Task 4: Runtime coverage, A/B and review

Files: tools/bench integration as needed, implementation report in docs/.

- [ ] Observe title and city gameplay, identify unqualified shader/object classes
  from logs and extend only contracts backed by source/capture evidence.
- [ ] Verify Superman, cape, NPCs and vehicles with actual owner generations and
  previous vertex order; retain unknown invalidators rather than guessing.
- [x] Run average of three built-in benchmark rounds per supported variant;
  parse CSV mean/stddev/p99/p99.9 and lows. Capture screenshots separately.
- [ ] Verify Off and Vulkan fallback; force optional pipeline failure.
- [x] Fresh independent final review and fix actionable issues. Report remaining
  failures without declaring the milestone complete or removing readiness gates.

## Execution ledger

Ruling: execute inline on `codex/m1-object-motion-20261105`, reusing the current
checkout and local SDK/game/build assets. The user's approval explicitly requests
implementation of M1; no additional plan approval is requested.

Baseline: three Quality camera-only runs average 40.2928 FPS; native CTest 5/5.
Source of truth for measurements is `docs/graphics-milestone-1-baseline-2026-11-05.md`.


05/11 execution checkpoint: code builds, native CTest 5/5 (667 cases), runtime
CTest 7/7 and RTX temporal fixtures pass. Object NGX consumption deliberately
remains blocked by incomplete pixel-validity/auxiliary-lineage proof. Full M1
and gameplay acceptance are NOT complete. Final measurement directories are
`logs/graphics_m1_quiet_{camera,objects}_20261105_r{1,2,3}`; both sets use EXE
38e72b6075409b7285b61e542cef0f9e36c33bf73fd8ce1bad85636788646da3.
Camera mean 35.6381 FPS; producer+raster fallback mean 26.2724 FPS.
Earlier `graphics_m1_after_objects_*` runs are discarded for performance because
an inherited diagnostic switch spilled geometry/surface logging each frame;
the three guards now exclude `motion_continuous_` and the final harness confirms
zero unexpected per-draw diagnostics. Screenshots and Vulkan smoke are pending
at this checkpoint. See the implementation report for definitive final status.


Final evidence update: screenshots, RG16F/R8 readbacks and Vulkan run.cmd smoke
are complete. Vulkan final benchmark valid, process exit 0. No gameplay ghosting
acceptance or full M1 acceptance. A final API guard limits automatic lifecycle
observers to native D3D12; final EXE hash is
15e5776fe9d567acde24289ba7d4286713d79c3dfe3a933e02d1e6a480403111.
The three-round figures are explicitly from the preceding measurement build.
Remaining tasks are the consumer GPU validity contract, full color/auxiliary
lineage, gameplay object classes, performance and paired temporal validation.
