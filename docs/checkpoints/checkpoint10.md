# Co-op: independent native radar anchors — 2026-11-03

## Scope

Continues checkpoint9 (`5194ec2`). The experimental `-SplitPreview` path now
uses P2's native actor position, terrain height and heading for the right-hand
radar. The native minimap's aspect calculation uses 8/9 in both split panes,
compensating for the final half-width composition. This does not implement a
complete per-player HUD: Flash bars, district labels, messages and reticles
remain shared/P1-derived and retain their previous sizing.

All hooks are inactive outside the existing split preview dependencies. The
ordinary launcher, single-view preview and benchmark paths retain their behavior.
No new game assets are added to Git.

## Native state ownership

- `82308DB0` produces the native threat/radar state from manager `82970174`.
  Its SUPERMAN-bin query at return site `82308EBC` normally selects P1. The
  3120-byte state has vtable `820215D0`, `VALID` magic `56414C44`, actor position
  at +16, terrain height at +32 and heading at +36. Visibility begins at +40;
  64 shared-world threat records occupy +48 onward.
- `HudThreatRenderObj+8` is a render-state lookup key, **not** an actor ID.
  `823149B8` consumes that key through `822F2668`. The minimap mesh is drawn by
  `8230EC00`; its aspect query returns at `8230EC70`. Flash stamina setters
  `822A51B8`/`822A52E8` belong to a different interface.
- On the simulation thread, the normal original producer runs first. A flat
  private 3232-byte manager copy then runs the original native producer with a
  private one-element actor-bin span containing the validated P2 actor. Only
  the exact SUPERMAN query at that return site is intercepted. The real bin,
  camera singleton, input table and primary manager are never rebound.
- The existing `82782200` submission hook captures the private producer's
  24-byte actor anchor and suppresses **all** of that private invocation's
  submissions, including its reticle packet. The regular primary submissions
  still execute. A full manager readback checks that the private pass did not
  change P1's manager. The complete PPC context is restored afterward.
- A short host mutex publishes an immutable anchor plus target/binding
  generation. The previous complete anchor remains available while its replacement
  is prepared; failure clears publication. This avoids a transient empty snapshot
  between producer updates. Unsupported renderers do not run the private producer.
  The render thread reads no actor pointer. Its P2 radar lookup
  accepts only the currently consumed owned-camera generation, exact state
  class/magic, a valid target and finite anchor fields. If unavailable, it
  refuses the P1 radar in P2's pane. This is generation alignment, not an exact
  same-simulation-frame transport guarantee.
- Retargeting changes only +16..39 in the radar's **local consumed copy**.
  A full 3120-byte readback verifies that the header, visibility and shared
  threat records remain equal to the expected copy. Shared queued packets
  are not edited.
- P2 uses private render-object bytes +112..1759 (map placement/height state,
  threat slots, interpolation/occlusion caches and timestamps). They are saved
  across P2 draws, initialized anew when the object or binding changes, and
  restored to P1's post-draw bytes before returning. GPU resource pointers at
  +64..96 stay shared. Restoration is checked by readback.
- The 8/9 override is scoped to the native minimap aspect query while a split
  world pass is active. The HUD camera transform and ordinary aspect queries
  remain unchanged. It corrects the map geometry, not every HUD icon or label.

## Validation

The incremental Release build passed (`artifacts/coop-build-radar-publication.log`).
The native pure suite passed **642 tests**; new negative cases reject stale
bindings, foreign packets, short packets, invalid IDs and non-finite anchors,
and verify preservation of all non-anchor bytes. Tool tests passed **156 tests
and 46 subtests**. The report's negative cases exercise missing producer/draw
evidence, wrong generation/target, failed packet/cache checks, missing aspect
correction and non-finite values.

The movement run used isolated PID 8348, save slot 2 (15%), Metropolis. Native
anchor values matched explicit Lua actor-position queries. Initial P1/P2 headings
were respectively pi and zero, independently produced by the original native
calculation. Both maps were visually circular; their street maps followed their
respective actors. Movement checks used separate two-second forward inputs,
requiring active movement >2 units and inactive movement <0.25:

| Input | Radar P1 displacement | Radar P2 displacement | Draw frames |
|---|---:|---:|---|
| Port 1 only | 0.00010 | 87.27790 | 120 to 480 |
| Port 0 only | 87.28981 | 0.00000 | 480 to 840 |

Both passed (`artifacts/coop-test/radar-motion-check.json`). These are consumed
radar actor positions, not camera positions. This scene had both actors hovering;
it does not establish complete flight, ground control or combat behavior.

Removed P2 10112 at 19:53:52.700 and observed consumed zero plus single-view
return. Rejoined as 10191 at 19:54:22.586, generation `C0000002`; separate maps
returned and used the replacement actor's anchor. The archived run
`radar-motion-run.log` passed `--require-radar`: 17 same-frame two-radar/world/
capture/composition samples, 34 radar draw samples and 63 native producer samples
across both generations. There were no radar, split, guest or script failures.

Local review then corrected the transient publication gap described above. The
first Gemini reconnaissance identified the radar/Flash separation, but its claim
that render-object +8 is an actor ID was rejected against native instructions.
Two later Gemini review attempts returned no final answer; they are not counted
as completed code reviews.

The final publication-fix retest used isolated PID 22536 and P2 9876. After a
separate P2 movement input, removed 9876 at 20:00:03.100 and rejoined as 10653 at
20:00:54.822. The consumed binding cleared and advanced to `C0000002`; visual
inspection confirmed separate circular maps again. The stopped, archived final
run `artifacts/coop-test/radar-final-run.log` passed
`artifacts/coop-test/radar-final-check.json`: 9 same-frame radar/world/capture/
composition pairs, 18 radar draws and 28 native producer samples across both
generations, with no reported radar, split, guest or script failure.

**Leave/HUD limitation:** after the despawn script, the image returned to one
view but the HUD was absent in this scene; rejoining restored it. The script
also resets the native camera home. The cause is not localized, and normal HUD
visibility after leaving is **not** accepted as fixed by this milestone.

Diagnostic-off isolated PID 19220 reached the ordinary title screen. Its command
line contained input preparation but no camera, transport or split flags. There
were no radar/camera/split messages or guest access violations in that startup;
the process continued presenting and was stopped through the owned probe.
The retained check is `artifacts/coop-test/radar-off-check.json`.

## Reproduction and remaining work

```powershell
./tools/coop/probe.ps1 -Action Start -SplitPreview
# Load copied save slot 2 (15%), select Metropolis, wait for gameplay control.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
./tools/coop/probe.ps1 -Action Input -Port 1 -Forward 32767 -Milliseconds 2000
./tools/coop/probe.ps1 -Action Input -Port 0 -Forward 32767 -Milliseconds 2000
./tools/coop/probe.ps1 -Action Script -Script tools/coop/despawn_p2.lua
# Wait for the consumed clear/single-view return, then rejoin.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
./tools/coop/probe.ps1 -Action Stop
python tools/analysis/coop_camera_report.py --require-target 1 --require-secondary --require-radar --output artifacts/coop-test/radar-check.json
```

`--require-radar` implies split/transport checks and requires at least three
same-frame two-radar/composition samples, corrected-aspect evidence for each
pane, and a matching native P2 producer/draw anchor for every published binding.
These are sampled state checks; visual inspection and isolated movement remain
necessary. The report does not establish correct pixels throughout all frames.

Next: separate the Flash HUD's actor data and layout. The threat set remains the
native shared set; distant P2 threats/streaming, physical controllers, combat,
mission transitions, death, arbitrary effect histories and performance still
need validation. No reduction in frame time is claimed for the extra producer.

For the Flash continuation, `822A9F80` resolves the first SUPERMAN actor and reads
its `StaminaPoints` through ACTV; `822A51B8` sends `setStaminaValue` and
`822A52E8` sends `setStaminaMax`. The UI root/wrapper globals are `8296EE7C` /
`8296EE80`, with the command interface at `8296EB80`. These are research entry
points, not an implemented safe second Flash instance.
