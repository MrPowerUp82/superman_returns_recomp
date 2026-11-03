# Co-op: separately owned P2 camera controller — 2026-11-03

## Current state

Continues checkpoint5 (`793429a`). There is now an opt-in **second native camera
controller graph**, with its own target, view object and computed transform.
It follows P2 without replacing the primary camera singleton. Only the original
view is rendered: transport of this transform to the render thread and split-screen
composition are still pending. This is a development experiment, not playable co-op.

## Code and lifecycle

- `port/src/coop_second_camera.{h,cpp}` defines `sr_coop_second_camera`, off by
  default and requiring co-op input, with title benchmarks excluded.
- `InstallSecondCamera` reserves a reusable SystemHeap block for a 5024-byte
  native manager and 64-byte transform scratch area. No camera object is memcpy'd.
- Hook `sub_8218CCD0` calls the original initialization, then constructs the
  secondary graph with `sub_8218C4C0`, while startup parameters are available.
  The game's global camera pointer `0x8296D86C` remains the primary manager.
- After the original main camera update `sub_821808F8`, `UpdateSecondCamera`
  resolves the actor on input port 1, checks on-foot / flight parameter arrays,
  and binds via `sub_82181590`. P1/P2 on the same input actor are rejected.
- At join, if the original game's primary camera has selected P2 as its
  home, the native setter restores that home to the actor on port 0. Other
  scripted home targets are left alone. This happens at binding, not every frame.
- Each tick invokes only the active controller's virtual +16 and the selected
  view's virtual +44 to get a transform. It does **not** run a second full manager
  update, `sub_821759B8`, world update, camera handoff or render pass. The caller's
  complete PPC context is restored after these extra guest calls.
- When the bound P2 entity disappears, updates suspend. The graph retains its
  startup-loaded parameter resources; a replacement P2 rebinds through the native
  setter before updates resume. The inactive manager event handler remains
  disabled by its constructor flag at +517. No removed actor is updated.
- Hook `sub_821620E8` destroys the secondary graph before primary destruction via
  `sub_82197E08` with delete flag 0. Native nested allocations/listener are freed;
  the small outer SystemHeap reservation remains reusable for this runtime.
  This teardown hook was source-audited; full guest shutdown/level recreation was
  not exercised in the runtime checks below.

Other changes: `coop_split.cpp` installs the experiment; `coop_camera_probe.cpp`
shares its existing camera-update hook; CMake includes the new source;
`probe.ps1 -SecondCamera` enables it. The camera reporter's `--require-secondary`
requires three finite snapshots with distinct camera-owner and actor-target IDs,
and checks that the primary singleton was not replaced.

## Root cause found during implementation

Calling the full constructor late, immediately after spawning P2, produced an
object whose on-foot and flight parameter pointers were both null. Eager target
binding then crashed because it resets **all** camera controllers, including
flight. A temporary, narrowly scoped fault observer captured:

`lr=82160D94 ctr=825F4CEC r3=3003BA10 r4=00000000`

`sub_8217CEC8` passes flight-controller +240 plus an index *288 as the source to
`sub_82160D70`, which copies 288 bytes without a null check. Null +240 caused the
fault. A readiness guard avoided the crash, but the late-created graph's arrays
stayed empty. Constructing alongside primary initialization populated the arrays
and allowed binding. The precise resource-loader lifetime behind the late lookup
remains unlocalized; do not describe this as merely a proven asynchronous delay.

The final code constructs during native initialization and still checks both
arrays before binding. Temporary exception instrumentation was removed; no fault
is swallowed or recovered. Earlier failed experiments remain in rotated logs,
separated from the final run by startup markers.

## Runtime evidence

Isolated Metropolis from the copied 15% save. No normal save was used. Synthetic
inputs were used; physical controllers/right-stick rotation remain untested.

Final run PID 18116, primary manager `0x0063ADC0`, secondary `0x3003B000`:

1. Created P2 ID 11852. The primary home was restored to P1 ID 1 automatically.
2. First three snapshots had distinct owners/targets, finite transforms and
   `singleton_unchanged=true`.
3. Despawned 11852. Log: `coop second camera suspended ... removed_target=11852`.
4. Spawned replacement 11853. The same owned graph rebound successfully; P1 home
   stayed ID 1. No manual `camera_p1.lua` switch was needed in this final run.
5. Exercised each synthetic port for two seconds and sampled both camera
   positions after six seconds:

| Input | Primary camera displacement | Secondary displacement | Result |
|---|---:|---:|---|
| Port 1 only | 0.03455 | 87.30243 | Pass |
| Port 0 only | 78.55746 | 0.01964 | Pass |

The active camera had to move >2 game units; inactive camera <0.25. Raw before /
after positions and sample ordinals: `artifacts/coop-test/camera-motion-check.json`.
These are camera-follow checks in this scene, not general physics/flight tests.

Stopped that owned run, then analyzed retained log segments:

```powershell
python tools/analysis/coop_camera_report.py --require-target 1 --require-secondary --output artifacts/coop-test/second-camera-check.json
```

Passed: 33 secondary snapshots, both P2 IDs observed, one distinct secondary
owner, maximum camera separation 154.83376 game units; 72 main update/submission
and 76 main consumption/application samples; no guest access violation or
nonzero mailbox Lua status in the final run. The reporter's sampled checks do not
verify every frame or rendering of the secondary matrix.

Release build: `.tools/build_reference_release_incremental.cmd`, latest log
`artifacts/coop-build-second-camera-home.log`, passed. Tool suite:
`python -m pytest tests/tools -q`, 150 passed and 13 subtests passed. The new
reporter test was observed failing before secondary-camera validation existed.
Diagnostic-off isolated startup was checked separately after the final run.

## Reproduction

```powershell
./tools/coop/probe.ps1 -Action Start -CameraProbe -SecondCamera
# Load copied save slot 2, select Metropolis, wait for gameplay control.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
# Observe "coop second camera" snapshots; do not switch the primary to P2.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/despawn_p2.lua
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
./tools/coop/probe.ps1 -Action Stop
# Report command above; logs may span numbered rotations.
```

## Next

1. Transport the owned secondary transform and projection across the guest
   state-buffer boundary with explicit lifetime/versioning. It currently ends in
   diagnostic scratch memory and is not supplied to the renderer.
2. Provide view-dependent preparation/culling and world drawing for each view,
   preserving existing HUD-camera usage and updating simulation once.
3. Compose two viewports with correct aspect/depth/post effects and HUD ownership.
4. Validate automatic ground/flight mode selection, right-stick rotation,
   collisions, death, cutscenes, transitions and complete teardown. The secondary
   graph currently selects its native controller when bound; later game FSM
   camera-mode changes still act on the primary singleton and need routing.

All extracted assets, dumps, copied saves and runtime evidence remain ignored
under `artifacts/`. No game assets are included in this milestone.
