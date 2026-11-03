# Co-op: isolated control and rejoin milestone — 2026-11-03

## State

The user requested a milestone commit and continuation from checkpoint2/3.
Commit `da2c2cf` records experimental P2 spawning, separate synthetic controller
ports and the isolated probe. This continuation improves that development path.

The objective remains two players in one process, one shared world and two
independent split-screen views. **Split-screen is not implemented.** The normal
launcher still only prepares inputs; P2 is spawned manually in the probe.

## Changes and evidence

- `port/src/coop_lua.cpp`: file polling caches one pending chunk; context matching
  runs on every valid callback instead of only every 250 ms. Both `player1` and
  `player2` markers resolve the actor assigned to their respective input port.
  A newer file edit replaces pending work. Tested a P2 request before P2 existed,
  replaced it with a generic chunk, and observed only `PENDING_REPLACED_OK`.
- The controller-binding overload now looks up a conflicting slot's entity.
  A live occupant remains protected. A removed entity's stale ID is reclaimed
  through `sub_8229F380`, which already handles a missing old entity.
- On the patched runtime, P1 stayed ID 1; P2 7094 was explicitly despawned,
  then P2 7105 was created. The log recorded stale actor 7094 reclaimed on port 1
  and actor 7105 bound. `check_bindings.lua` then returned
  `same=true occupied0=false occupied1=false invalid=false fraction=false`.
- Added `bind_p2.lua`, `despawn_p2.lua`, actor-context `movement_p1.lua` /
  `movement_p2.lua`, and `isolate_movement.lua` under `tools/coop/`.
- Added `tools/coop/test_movement.ps1`: an actual runtime check using unique
  per-stage log tokens, finite actor positions, timeouts and a JSON evidence file.

Build passed with `.tools/build_reference_release_incremental.cmd`; latest log:
`artifacts/coop-build-rejoin.log`. No guest access violation or nonzero mailbox
Lua status occurred in the latest run (owned PID 1780). Earlier failures remain
in historical appended logs; use the last mailbox-enabled startup boundary.

## Movement validation

Warworld, P1 and P2 on foot, one enemy FSM paused, P2 moved to open ground near
P1. Each movement command lasted 2 seconds; release was sampled again after an
additional 2 seconds. Active movement must exceed 2 game units; inactive/idle
movement must stay below 0.25. Commands were synthetic, not physical controllers.

| Check | P1 displacement | P2 displacement | Result |
|---|---:|---:|---|
| Port 1 only | <0.000001 | 20.42515 | Pass |
| Port 1 released | <0.000001 | <0.000001 | Pass |
| Port 0 only | 21.17015 | 0 | Pass |
| Port 0 released | <0.000001 | <0.000001 | Pass |
| Both ports, opposite forward values | 20.79768 | 21.17012 | Pass |
| Both released | <0.000001 | <0.000001 | Pass |

Raw positions and results: `artifacts/coop-test/motion-check.json`; execution log:
`artifacts/coop-test/probe.log`, tokens starting
`COOP_TEST_fe177da5806f4f008c4f1a5330c3f4b6_` at 14:25–14:26 local log time.

This establishes isolated ground start/stop and port separation for this setup.
It does not localize the earlier combat displacement. No per-frame position or
velocity clamp was added. Combat/physics, flight, transitions, physical pads,
reconnect and long-running lifecycle behavior still need validation.

To repeat: start `tools/coop/probe.ps1 -Action Start`, load a grounded gameplay
scene, run `spawn_p2.lua` through `-Action Script`, then run
`tools/coop/test_movement.ps1`. The test leaves enemy FSMs paused and changes the
single camera target; stop the disposable probe with `-Action Stop` afterward.
The normal game/save is separate. The owned test was stopped after validation.

## Camera source map for the next milestone

These findings come from the generated guest source and local guest-image
constants, not a verified second rendering path:

| Address | Finding |
|---|---|
| `0x8296D86C` | Camera-manager singleton pointer |
| `0x82186258` | SetCameraHomeEntity Lua wrapper; resolves entity then calls below |
| `0x82181590` | Sets manager+432 target ID; updates embedded camera controllers |
| `0x8218EC00` | SetGameCamera Lua wrapper; named mode plus blend time |
| `0x82180360` / `0x82174758` | ResetGameCamera wrapper / reset implementation |
| `0x821808F8` | Camera update, called once from main-frame function `0x821494C0` |
| `0x8218CCD0` / `0x8218C4C0` | Allocates 5024-byte manager / constructor |
| `0x821620E8` | Manager destruction and singleton reset |

`SetGameCamera` selects FLIGHT/ONFOOT/ORBIT/FLYBY/MULTI within this same manager;
these names do not establish simultaneous local-player views. Active camera
pointer is at manager+4880, mode at +4896, blending fields at +4904/+4908.
Update writes pose at +480 and orientation at +448 and uses view pointers near
+40/+48. Controller objects targeted by the home setter are at +544, +2032,
+3424, +3744 and +4192. Treat these as source leads requiring runtime verification.

`SetCameraHomeEntity(P2)` works in the probe and makes the existing camera follow
P2. This is one camera target switch. No second manager was allocated or cloned.

Renderer profile has frame handoff `0x824707B0` and swap `0x82112050` in
`port/src/native_renderer/game_profile.h`; neither has been established as a
safe world-only render boundary. `CameraCandidateProbe` still observes a shader
constant candidate, not a verified camera interface.

Next: identify world submission/culling and camera consumption boundaries; prove
that two view submissions can share one simulation update, then handle viewport,
aspect, depth, HUD and post effects. Audit manager shared state before creating a
second camera. Avoid replaying the main frame or duplicating P1's image.

Only source/scripts/docs belong in Git. Extracted assets, runtime dumps, build
logs, copied saves and test evidence remain ignored under `artifacts/`.
