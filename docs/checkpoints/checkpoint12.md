# Co-op: per-player native stamina handoff — 2026-11-03

Continues checkpoint11 (`9c931b8`). This milestone supplies distinct native
stamina records to each split HUD pass. It is an opt-in diagnostic foundation:
the existing APT stamina graphics still use the shared native model.

## Native evidence and implementation

- Actor lookup `821C23E0`, actor vtable+36 with **ACTR (`41435452`)**, ACTR+104
  power set, and `82226C50` resolve each actor's `StaminaPoints` independently.
  Current magnitude is power+16; maximum is definition pointer power+4, then +16.
  The native minimum HUD capacity is 500 and its display scale is 10000.
- The producer runs after the existing owned-camera simulation update at
  `821808F8`. It preserves the complete PPC context, validates both input slots
  and the owned camera binding, then publishes one complete host snapshot under
  a short mutex. Invalid bindings clear it. No guest packet padding is used.
- The original model at `8296EE7C` is read before/after sampling: cached stamina,
  city health and capacity at +48/+52/+56 must remain byte-identical. The
  `primary_meter_matches` field is diagnostic, since native update order can
  differ from this sampling point.
- HUD entry `82775EF8` selects only host data for the consumed frame generation
  and view. It never invokes actor lookup or APT commands on the render thread.
  Logs explicitly record `apt_override=false`; city health remains shared.
- `sr_coop_status_probe` defaults off and requires co-op input/owned camera.
  `probe.ps1 -StatusProbe` supplies that dependency; `-SplitPreview` also enables
  the status probe. Benchmark mode excludes the producer.

The initial frontend-only hook at `822B5DB0` did not publish during gameplay
and was removed. Its frontend return site is `821513A4`; generated switch
pseudo-comments make instruction-list indexes unsuitable for address arithmetic.
An initial ACTV tag also prevented lookup; the final ACTR value was checked
against the actual `lis 16707; ori 21586` native sequence.

Local UI archive research found `Main_Hud`, `StaminaMeter`, `GenericMeter` and
`CityHealth` APT assets in `game/DATA/ui.AST`. Extracted data stays ignored under
`artifacts/coop-research/apt-hud`. The available Windows REA provider does not
admit this PPC target, so existing generated PPC and local archives supplied
the evidence. APT component/interpreter ownership is still unresolved.

## Automated checks

Native tests cover normalization, minimum capacity, missing/invalid actor IDs,
invalid powers, distinct players, consumed view selection and stale/replaced
generations. The policy was observed failing before implementation. The native
suite passed **650 tests** (`artifacts/coop-status-tests-green.log`).

The report's `--require-status` requires matching native producer values and
both HUD selections in sampled composite frames for every published binding.
It validates scaling, actor separation and native-model preservation, and
rejects a draw contradicting its explicitly referenced sampled source. An
unsampled source is corroborated by equal sampled values; this is not proof
of every intermediate frame. The source contradiction was reproduced red
before its fix. The tools suite passed **157 tests and 59 subtests**
(`artifacts/coop-status-tools-final.log`).

A bounded read-only reviewer found no remaining issues after the source
validation fix and simulation-boundary relocation. Runtime acceptance remains
the responsibility of the owned probe run below.

## Runtime acceptance

Final Release build passed (`artifacts/coop-build-status-actr.log`). Owned PID
24780 loaded copied save slot 2 (15%), Metropolis, native D3D12 split preview.
P1 was actor 1, initial P2 8024, owned generation `C0000001`.

Disposable Lua fixtures used the native explicit-actor power setters and
disabled regeneration temporarily. They established these native values:

| Fixture | P1 current / maximum | P2 current / maximum | Matching HUD frame(s) |
| --- | --- | --- | --- |
| A | 10000 / 10000 | 2500 / 10000 | 120, 240 |
| B, change P2 only | 10000 / 10000 | 7500 / 10000 | 360 |
| C, change P1 only | 5000 / 10000 | 7500 / 10000 | 480, 600, 720 |
| Rejoin, new P2 8033 | 10000 / 10000 | 2500 / 10000 | 840, 960 |

Each row has matching native producer samples and both HUD selections for the
same composite frame. `tools/coop/stamina_status.lua` independently queried the
native Lua getters. The sampling preserved the model's three cached words;
sampled city health remained `3F800000`, and no sampled primary-meter mismatch
was recorded. These checks establish data selection, not independent bar pixels.
Visual inspection confirmed two world views, native bars and circular maps.

The fixtures restored saved magnitudes and enabled powers before despawn.
Removing P2 at 23:16:04.467 cleared the status binding to zero and the transported
secondary view. No later ready status sample reused `C0000001`. Rejoining at
23:16:17.553 created P2 8033 and generation `C0000002`; native actor values and
both HUD selections matched that replacement. Fixture values were restored again
before stopping the owned process. No guest/script or experimental verification
failures were found in the retained run.

Evidence is archived as `artifacts/coop-test/status-final-run.log`.
`status-values-check.json` passed the four value cases and the binding sequence
`C0000001 -> 0 -> C0000002`. The report passed `--require-target 1
--require-secondary --require-radar --require-status` (`status-split-check.json`):
**12** world/capture/composition pairs, **10** radar/composition pairs and
**12** status/composition pairs, covering both generations. It includes 36
native status producer samples and 24 ready HUD selections, with zero sampled
primary-meter mismatches. HUD samples can be absent during the native idle fade.

Diagnostic-off PID 19148 reached the ordinary title screen with input
preparation only. `status-off-check.json` passed with zero experimental
diagnostic messages and zero guest access violations. The owned process was
stopped. `git diff --check` passed. No generated code, extracted assets, save
files or runtime fixtures are included in the milestone commit.

## Reproduction

```powershell
./tools/coop/probe.ps1 -Action Start -SplitPreview
# Load copied save slot 2 (15%) and enter Metropolis.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
./tools/coop/probe.ps1 -Action Script -Script tools/coop/stamina_status.lua
# Exercise independent values only in the disposable probe; restore afterward.
python tools/analysis/coop_camera_report.py --require-target 1 --require-secondary --require-radar --require-status
./tools/coop/probe.ps1 -Action Script -Script tools/coop/despawn_p2.lua
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
# Collect enough stationary awake samples for both generations, then report again.
./tools/coop/probe.ps1 -Action Stop
```

## Remaining work

Independent APT stamina graphics and layout, physical controllers, complete
flight/combat, menus/cutscenes, automatic join/leave, death/respawn, transitions,
distant streaming and performance remain open. Separate status records do not
establish independent Flash widget state or complete co-op readiness.
