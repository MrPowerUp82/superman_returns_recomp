# Co-op: shared idle-camera/HUD ownership — 2026-11-03

## Investigation and scope

Continues checkpoint10 (`43fea40`). The HUD disappearance previously observed
after P2 despawn was traced to the game's native idle-camera transition, not
established render corruption. With no input, camera mode 2 fades the Flash HUD
and clears native radar visibility. Input restores normal camera mode and HUD.
The earlier leave/HUD observation is therefore reclassified; this does not
claim that every leave, cutscene or transition issue has been excluded.

The investigation used isolated PID 11312 and save slot 2 (15%), Metropolis.
Despawn without a camera reset initially retained the HUD. Repeating the original
despawn also initially retained it, then the HUD faded with camera mode 2.
A P1-only input restored mode 0 and the HUD without spawning another actor.
Read-only state snapshots are retained as `artifacts/coop-test/hud-leave-*.json`.

Native evidence:

- `82258240` updates global idle tracker `8296E0E4`. It compares actor movement,
  state and elapsed time; `8229AAD8` reads per-port input inactivity from input
  manager `8296E8C8`. The query returns to `822582F4`.
- Actor callbacks `8226B4D0` and `8226B618` supply the actor and its input port
  to that same global tracker. Letting both actors update it mixes position and
  activity histories, including the idle-enter flag at tracker+41.
- `8224A978` enters native idle mode 2, clears manager HUD flags +3120/+3121 and
  sends `HUDFadeOut` through `822A4D00`. `8224AA68` restores the camera, sends
  `HUDFadeIn` through `822A4C70`, and restores the native HUD flags.
- The native camera's idle threshold at manager+3652 was 10 seconds in the test.
  Compact local disassembly is `artifacts/coop-research/idle-native-functions.txt`.

## Implementation

New opt-in `sr_coop_idle` requires co-op input and an owned P2 camera, and excludes
benchmarks. `probe.ps1 -SplitPreview` implies it; `-SharedIdle` enables the policy
with the owned camera but does not itself enable split rendering/transport.
The ordinary launcher remains input preparation only.

Only the original primary actor on port 0 updates the shared native tracker
while the binding is valid. The owned secondary actor's port-1 callback is
suppressed. Ownership checks include distinct valid IDs, current input slots,
the snapshot's primary camera identity and owned generation. Unknown actors,
wrong ports, cleared/invalid bindings and disabled configurations use the
original native behavior.

During that exact P1 update's input-duration query, the returned idle duration is
the minimum of valid finite durations from ports 0 and 1. Native port-1
timestamp rounding residues down to -1 millisecond are normalized to zero;
larger negative/non-finite secondary values preserve the native primary result.
The extra
leaf query restores the complete post-original PPC context, changing only the
returned `f1`. Other input-duration queries are untouched. This lets either
player keep the shared HUD awake while preserving native idle entry when both
are inactive. No actor, camera, input table or visibility global is rebound.

The native fade/wake functions remain original; diagnostic hooks record their
transitions. The design intentionally keeps one global idle camera/HUD owner.
Per-pane Flash widgets, individual idle fades and cinematic rules remain future
work.

## Verification

The portable policy tests were observed failing against native-only stub behavior:
three failed checks covered P1/P2 routing and P2-only activity. After implementation,
the full native suite passed **646 tests** (`coop-idle-tests-green.log`).
The first prolonged P2-input run exposed native float timestamp residues of
approximately -7 microseconds. Rejecting these durations caused spurious
idle-entry/wake cycles during P2 input. A new regression test failed two checks
against that version before the rounding normalization was implemented. The
first run is retained as `artifacts/coop-test/idle-roundoff-run.log`, not counted
as acceptance evidence.
The final native suite passed **647 tests** (`coop-idle-tests-final.log`).
The tools suite passed **156 tests and 46 subtests**. The final Release build
passed (`artifacts/coop-build-idle-final.log`). No generated game code/assets
are committed.

A bounded read-only code reviewer found no concrete correctness issues in hook
scoping, PPC context preservation or binding checks. Runtime/cutscene/lifecycle
behavior was explicitly left to the owned runtime validation below.
The rounding adjustment received a narrow follow-up review with no concrete
findings. Long-session timestamp precision remains unvalidated.

## Owned runtime acceptance

Final PID 34480, copied save slot 2 (15%), Metropolis. P2 initially 8958, owned
generation `C0000001`. Four overlapping five-second forward commands on port 1
kept input active across sixteen seconds without sending P1 input. The sampled
primary input-idle duration grew from 11.67 to 23.67 seconds; secondary durations
were approximately -3 microseconds and the combined duration remained zero.
There was no idle-enter transition during that interval. Readback showed camera
mode 0, tracker+41 zero, primary target 1 and both native HUD visibility flags 1.

After both ports became inactive, native mode 2 and the normal HUD fade returned.
P2-only input restored camera mode 0 and both HUDs; visual inspection confirmed
the bars and circular maps in both panes. A second idle period followed by
P1-only input also restored mode 0 and both flags. No visibility override is
used by the implementation.

Removed P2 8958 at 22:34:26.721 in the retained log. The consumed secondary view
cleared and the idle binding became zero. P1 initially retained normal HUD
flags/mode; ordinary inactivity later entered mode 2. P1 input restored the HUD
in a single full view **without rejoining**, verified visually and by readback.
Rejoined as 9756 at 22:35:09.138, generation `C0000002`; P2 input restored the
two circular maps and ordinary camera behavior with the new binding.

The stopped run is archived as `artifacts/coop-test/idle-final-run.log`.
`idle-policy-check.json` passed **76 samples**, verified combined-duration math,
secondary callback suppression, the two generations separated by zero, absence
of an idle-enter transition during the initial P2-only activity interval, and
nine read-only native state snapshots. These checks cover this scene and the
recorded samples, not all gameplay or every intermediate frame.

The existing report passed with `--require-target 1 --require-secondary
--require-radar` (`idle-split-check.json`): **24** same-frame world/capture/
composition pairs, **11** matching two-radar/composition samples, and matching
consumed-camera/native-radar producer evidence for both generations. Radar
samples are naturally absent during the intentional native idle fade. An early
report lacked a stationary matching camera sample for generation 2; additional
activity/sampling supplied the evidence without changing report tolerances.
There were no reported guest/script, split, radar or transport failures.

Diagnostic-off PID 27352 reached the ordinary title screen with input preparation
only and no idle/camera/transport/split flags. `idle-off-check.json` passed with
zero experimental diagnostic messages and zero guest access violations. The
owned process continued presenting and was stopped. `git diff --check` passed.

## Reproduction

```powershell
./tools/coop/probe.ps1 -Action Start -SplitPreview
# Load copied save slot 2 (15%) and enter Metropolis.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
# Let both ports idle for longer than the native threshold; observe fade.
./tools/coop/probe.ps1 -Action Input -Port 1 -Forward 32767 -Milliseconds 1500
# Observe native wake with no P1 input. Repeat P2 input across >10 seconds.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/despawn_p2.lua
# Verify ordinary P1 idle/wake, then rejoin and verify the new binding.
./tools/coop/probe.ps1 -Action Stop
```

Remaining: independent Flash stamina/status data and layout, physical controllers,
ground/flight/combat, menus/cutscenes, transitions/death and distant streaming.
The 1ms negative-duration tolerance was validated for the observed short runs;
many-hour sessions and suspend/resume have not been tested.
This milestone does not claim performance or complete co-op gameplay readiness.
