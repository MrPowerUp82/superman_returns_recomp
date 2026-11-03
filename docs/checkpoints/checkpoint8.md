# Co-op: native P2 world-view preview — 2026-11-03

## Current state

Continues checkpoint7 (`b50a46b`). The transported P2 camera can now drive the
existing native world viewport, while the gameplay camera manager remains owned
by P1. This is an opt-in diagnostic step toward two world-view submissions.
**It displays one world viewport at a time; split-screen composition is pending.**

## Native application

- `sr_coop_camera_preview`, off by default, requires co-op input, the owned
  second camera and packet transport, and excludes title benchmarks.
  `./tools/coop/probe.ps1 -Action Start -PreviewP2` enables the dependencies.
- The existing hook on `sub_82775AA0` runs the original once and records its
  original P1 application before calling `ApplyP2CameraPreview`. Only view 0 from
  the normal preparation caller `0x82776000` is eligible. This is before
  `sub_82783B48` preparation and `sub_82776038` world drawing.
- The consumed packet comes from `[0x829A9398]+96`, the same owner returned by
  native leaf `sub_82776838`. `DecodeOwnedSecondary` accepts only our nonzero
  binding generation, finite matrix/perspective parameters and the supported
  1280x720 viewport. Foreign flags, short buffers and invalid views are rejected.
- The camera list is resolved from the graphics object. Native setters apply
  secondary FOV, aspect, near/far, viewport, transform and projection to the
  existing **world camera 0**. HUD camera 1 is a distinct object and retains its
  existing group-23 role. Neither consumed packet block is rewritten to choose
  the source. No global gameplay camera pointer is replaced.
- Setter order matches native `sub_82775AA0`: `827858F0`, `82785960`,
  `82785758`, `82785768`, `82785778`, `82786208`, `827863D0`.
  A guard restores the complete post-original PPC context after the extra calls.
- Readback checks all 208 packet bytes, the HUD camera's 64-byte transform and
  the complete world-camera transform. The draw observer re-resolves both camera
  slots at entry to `sub_82776038`, verifies identity and compares the current
  world camera's matrix to the consumed secondary matrix. It does not trust a
  cached camera pointer to establish which camera is selected.
- When P2 is cleared, the original application supplies P1 and the preview
  leaves it selected. The next world-draw entry verifies camera identity and
  equality with the consumed **primary** matrix and logs `preview restored`.
  Invalid nonzero secondary data is a diagnostic failure, not a preview success.
- Main simulation, camera production, render preparation, effect traversal and
  world drawing still run at their original frequency. This patch adds native
  camera setters and checks, not another world draw or simulation update.

## Automated validation

Six portable C++ tests pass, including owned-view decoding and rejection of a
replaced world/HUD selection even when the old camera retains the correct matrix.
Tool suite: `python -m pytest tests/tools -q`, 154 passed and 21 subtests passed.
The decoder/report tests were run before implementation; review reproductions
also failed before their fixes.

Code review found two evidence gaps: the draw check trusted a cached camera
pointer, and the reporter could accept mutually consistent preview rows that
did not match consumed P2 data or continued after a consumed clear. The fixed
checks cover current selection, changed packet/HUD, missing draws, source-position
mismatch, stale generation after clear, and a verified return to P1.

`--require-preview` implies transport requirements. For each exercised active
publication interval, at least one preview-draw position must match a sampled
consumed position within 0.05 game units. A clear interval must include a verified
primary return matching sampled P1 consumption. Boot-time zeros cannot prove a
leave. Application/draw sample pairs must agree in generation, camera identities
and position. These remain sampled checks, not every-frame source identity.
Wait for consumption and allow a stationary sample at each lifecycle boundary.

## Runtime validation

The initial run PID 4940 loaded only the copied 15% Metropolis save, rendered
P2 ID 7394 through world camera `0x0360FDD0`, and kept HUD camera `0x023FD450`
separate. Synthetic port-1 movement displaced the drawn camera while the primary
manager stayed near its original position. Removing P2 visibly returned to P1;
replacement 7396 reached generation `0xC0000002`. That run was stopped before the
review fixes and is preliminary evidence only.

The reviewed build passed (`artifacts/coop-build-camera-preview-reviewed.log`).
A later execution PID 9272 was interrupted before the automated sequence was
finished; its log ends with normal window-close/title-termination messages. It
is not used as acceptance evidence.

Final reviewed-build run PID 11632 loaded the copied 15% save in Metropolis:

1. P2 ID 8746 selected generation `0xC0000001`. The complete selected world
   matrix matched the consumed secondary matrix after application and at world
   draw entry. World/HUD identities remained `0x0360FDD0` / `0x023FD450`.
2. Two seconds of synthetic port-1 forward input moved the drawn camera from
   approximately `(-2153,1202,-159)` to `(-2152,1202,-70)`, while the primary
   stayed approximately `(-2156,1202,-148)`. The displayed world followed P2.
3. Despawn cleared the transported view. At 18:48:55.722, `preview restored`
   verified world-camera selection and its complete matrix against consumed P1;
   the displayed image returned to P1. Subsequent sampled consumption was zero.
4. Replacement P2 ID 9519 selected generation `0xC0000002`; its world image and
   matrix application/draw checks passed again. No manual primary-camera switch
   was used. The owned process was then stopped.

Archived the final run in `artifacts/coop-test/camera-preview-run.log` and checked:

```powershell
python tools/analysis/coop_camera_report.py --log artifacts/coop-test/camera-preview-run.log --require-target 1 --require-secondary --require-preview --output artifacts/coop-test/camera-preview-check.json
```

Passed: 18 complete preview application/draw pairs spanning both generations,
14 preview positions matching sampled P2 consumption, one verified primary
return, 48 primary update/submission pairs and 50 consumption/original-application
pairs. Transport had 21 publication samples and 14 active consumption samples;
the lifecycle intervals contained 8 first-generation, 4 post-leave zero, and 6
second-generation samples. There were 20 secondary controller samples with both
actor IDs. No preview/transport refusal or failed readback, guest access
violation or nonzero mailbox Lua status occurred in this run.

The primary propagation observations are deliberately taken before the preview
override. Separate preview observations prove the selected P2 world camera at
draw entry; they do not turn the primary propagation report into P2 evidence.

Separate diagnostic-off startup PID 14876 reached the title screen with normal
rendering. Its latest-run log contained no camera diagnostic messages or guest
access violations. It was stopped after this smoke check; evidence is saved in
`artifacts/coop-test/camera-preview-off-check.json`. A final incremental build
also passed (`artifacts/coop-build-camera-preview-verified.log`).

## Reproduction and next work

```powershell
./tools/coop/probe.ps1 -Action Start -PreviewP2
# Load copied save slot 2 (15%), choose Metropolis and wait for gameplay control.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
# Inspect P2's world view and preview / preview draw log pairs.
./tools/coop/probe.ps1 -Action Input -Port 1 -Forward 32767 -Milliseconds 2000
# Allow a stationary sampled consumption / draw before leaving.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/despawn_p2.lua
# Wait for zero secondary consumption and preview restored.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
# Wait for stationary samples from the new generation, then stop.
./tools/coop/probe.ps1 -Action Stop
python tools/analysis/coop_camera_report.py --require-target 1 --require-secondary --require-preview --output artifacts/coop-test/camera-preview-check.json
```

Next: create two world-view submissions and retain their outputs for viewport
composition with correct aspect, depth, post effects and HUD ownership. Do not
reuse the HUD camera slot or replay the main simulation. This preview verifies
the selected camera at normal preparation/draw boundaries; it does not establish
independent visibility/streaming at large P1/P2 separation or safe repetition of
all render preparation/effect updates for a second view.

Full ground/flight routing, physical pads/right stick, collision, death,
cutscenes, level transitions and complete teardown remain unvalidated. Logs,
copied saves, game data and dumps stay ignored under `artifacts/`.
