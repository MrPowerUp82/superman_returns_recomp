# Co-op camera/render handoff — 2026-11-03

## Scope and current state

Continues checkpoint4 and commits `da2c2cf` / `7ff167b`. The requested mode is
still two actors in one process/world with independent split-screen views.
**There is still only one gameplay camera and no split-screen.** This milestone
establishes its path into the renderer and identifies the active render route.

P2 creation and synthetic controls remain as in checkpoint4. No changes were
made to physics, actor control or normal saves. The new camera scripts switch
the existing camera target; they do not claim independent cameras.

## Implementation

- `port/src/coop_camera_probe.{h,cpp}`: opt-in `sr_coop_camera_probe`, requiring
  `sr_coop_input` and no title benchmark. Observes camera update, packet producer,
  packet consumer, view application, render frame, effect route and techniques.
  Calls each original exactly once, without guest-state writes or extra passes.
  Checked `ReadProcessMemory` copies; first three / every 120 calls are sampled.
- `port/src/frame_stats.cpp`: reports Swap counts to the observer.
- `port/src/native_renderer/guest_cpu_profile.cpp`: shares the existing strong
  wrapper for `sub_82783948` to observe group/subgroup calls. Existing CPU timing
  remains around the original; no duplicate hook or guest-call replay.
- `tools/coop/probe.ps1 -CameraProbe`: enables the diagnostic for this owned run.
- `tools/coop/camera_p1.lua` / `camera_p2.lua`: explicit single-camera switches.
- `tools/analysis/coop_camera_report.py`: verifies sampled positions and records
  the latest render sequence. Reads spdlog rotations oldest first, resets at the
  latest startup and fails if its marker has rotated out of retained history.
- `tests/tools/test_coop_camera_report.py`: mismatched positions, non-finite /
  unreadable data, startup boundaries, required-target submission, malformed
  target, and actual CLI handling of log rotation. Observed failing cases before
  fixing required-target checks and rotated-log reading.

## Verified runtime path

| Address | Role |
|---|---|
| `0x821808F8` | Updates camera manager; caller return `0x82149664` |
| `0x82176698` | Copies camera parameters into producer packet; return `0x82149674` |
| `0x82782200` | Packet virtual +12; serializes into state-buffer stream |
| `0x82779D68` | Packet serializer; header then 224 bytes + 60 bytes |
| `0x82777150` | Render-side consumer; return `0x82775FF4` |
| `0x8277A168` | Virtual packet read/interpolation using current/previous streams |
| `0x827773E8` | Copies decoded packet into consumer+96 |
| `0x82775AA0` | Applies selected packet view to renderer camera; return `0x82776000` |
| `0x82786208` | Copies view transform into render camera; original at camera+112 |
| `0x827863D0` | Derives projection / camera matrices |
| `0x82776038` | Render frame route selection, independent of simulation frame |
| `0x82776EB0` | Active effect-asset route |
| `0x8277B998` | Effect technique interpreter, invoked by the active route |
| `0x82783948` | Render group/subgroup execution, already CPU-profiled |
| `0x82775EF8` | End-of-world sequence: switches to another render camera, executes group 23 |

Producer packet at `producer_owner+16`; consumer packet at `consumer_owner+96`.
Packet layout (offsets relative to packet):

| Offset | Observed/source meaning |
|---|---|
| +0/+4/+8 | Vtable, serialized header marker, stream key |
| +16..79 | First 16-float view transform; position at +64/+68/+72 |
| +80/+84 | Near / far, observed 0.5 / 18000 |
| +88/+92 | FOV / aspect, aspect observed 1.7777778 |
| +96..103 | Four uint16 viewport fields: 0,0,1280,720 |
| +104 | Per-view flags/version; participates in interpolation choice |
| +112..207 | Second 96-byte view block; inactive/zero in tested run |
| +224 onward | Other serialized rendering parameters |

`sub_8277A048` interpolates two 96-byte blocks when their flags match. Two blocks
in the packet are evidence of a data interface, not evidence of two world views.
The ordinary apply path explicitly selects index 0. The render camera used for
group 23 comes from the separate render-camera array's index 1; blindly assigning
P2 to that index could break the existing end-of-world/HUD path.

Do not assume the raw transform's fourth components form a conventional affine
matrix: observed guest SIMD copies contain redundant components. The verifier
checks finite data and three position components, not inversion/handedness.

## Active render route

The initial candidate `0x827758F8` is a **fallback** loop over groups 0..22 and
three subgroups. It was never called during this run. The diagnostic was renamed
accordingly instead of treating a zero counter as a missing world render.

`sub_82776EB0` uses three asset references at owner+80/+84/+88 and order at
+304/+308/+312, count at +316. Source constants identify them as
`XenonRenderShadow`, `XenonRender`, `XenonPostEffects`. Observed order: 0,1,2.
The main post-effect technique selects MetropolisTech / WarWorldTech /
TwisterTech / QuantumSpeedTech according to owner+336.

Sampled render frame 10440, present 10440: updates/submissions 10922,
consumes/applies/effect frames 10440, fallback calls 0, 81 group calls and four
technique calls. Techniques in that sample:

| Effect | Technique | Caller return |
|---|---:|---|
| `0x02404DE0` | -1 (default, logged as uint32 4294967295) | `0x82777078` |
| `0x02400608` | -1 (default) | `0x82777078` |
| `0x02400D30` | 0 | `0x82777078` |
| `0x02400D30` | 12 | `0x82776B50` |

The complete sampled group/subgroup order is in the JSON report. Group calls
inside techniques return through `0x8277C23C`; the final group 23 call returns
through `0x82775F74` with a different render camera active. These are sequencing
observations, not a complete naming/classification of all passes.

## Validation

Release build passed: `.tools/build_reference_release_incremental.cmd`, log
`artifacts/coop-build-camera-route.log`. Latest isolated camera run PID 11052,
Metropolis loaded from the copied 15% save; P1 ID 1, P2 ID 12942. Switched to P2,
observed both actors in the world, sent a synthetic P2 movement command, and
restored the camera to P1. The owned run was stopped before reporting.

Command:

```powershell
python tools/analysis/coop_camera_report.py --require-target 1 --require-target 12942 --output artifacts/coop-test/camera-p2-check.json
```

Result: pass; 95 update/submission pairs and 90 consumption/application pairs,
maximum position delta 0 at both boundaries; targets 0,1,12942 sampled and matched
to submissions; no guest access violation or nonzero mailbox Lua status in the
latest retained run. Logs rotate at 5 MiB; evidence spans `probe.1.log` and
`probe.log`. Read all numbered segments before the active file when investigating.

This establishes sampled single-camera propagation for P1/P2 target switching.
It does not establish producer-to-consumer packet identity, latency, complete
matrix correctness, independent camera control, simultaneous views, culling for
P2, collision/flight/combat correctness, or rendering performance.

Tool tests: `python -m pytest tests/tools -q`: 149 passed, 13 subtests passed.
Diagnostic-off isolated startup was checked separately after this run: normal
rendering continued without camera-observer messages or guest access violations.
That owned smoke-test process was also stopped.

## Next work

1. Create a separately owned gameplay camera through its lifecycle. Audit the
   5024-byte constructor/destructor and shared registries; do not memcpy a camera
   or replace the global singleton while another thread may read it.
2. Carry P2 camera data across the state-buffer boundary with explicit ownership.
   Respect interpolation and the existing render-camera/HUD usage.
3. Identify view-dependent preparation/culling in `sub_82775FA8` /
   `sub_82783B48` as well as technique drawing. Replaying only a group list may
   use P1's visibility results; replaying the main frame updates the world twice.
4. Define two world-view submissions and final composition with independent
   viewport/aspect, depth and post-effect resources, followed by correct HUDs.

There are no extra camera updates, allocations or render passes in this patch.
Assets, guest dumps, copied saves and logs remain ignored under `artifacts/`.
