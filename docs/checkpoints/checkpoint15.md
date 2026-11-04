# Co-op: private P2 stamina fill/notch preview — 2026-11-04

Continues checkpoint14 (`54a689d`). P2 now has an opt-in independent native
stamina fill/notch position in its split pane. The implementation admits two
10000-point capacities. It retains the original effect frames, shield/tired
state, colors and visibility; it is not a complete independent APT VM/widget.

## Instance selection

The read-only ancestry investigation resolved native transform pushes at
`8268C568` and alternate `8268C670`, with pops at `82685290`. A bounded host
stack validates node identity and stops selection after mismatch or overflow.
Checked reads resolve source roots, character tables and exported symbol
pointers for each HUD pass; no recorded guest address is used as a constant.

Selection requires the exported Main_Hud -> StaminaMeter at placement depth
172 -> GenericMeter at depth 1, then the exact mask/fill/notch role. Descendants
must remain in that GenericMeter resource. This excludes the balance meter
that shares body character 21 but uses placement depth 13 instead of 34.
The initial read-only owned run (PID 28836) verified the exported chain,
balanced push/pop counts and all five roles in both views before substitution.

The evidence and bytecode-derived formulas are in `docs/coop/APT_STAMINA.md`.
The current Windows REA provider still excludes PPC. REA session inspection
confirmed no open target; existing generated PPC, raw owner assets and owned
runtime observations supplied this milestone's evidence. No provider
decompilation/runtime Evidence IDs or new native REA session are claimed.

## Private preview

Default-off, restart-required `sr_coop_stamina_preview` requires the APT probe,
co-op input and native split dependencies and excludes benchmark mode.
`probe.ps1 -StaminaPreview` supplies those flags. Ordinary `-AptProbe` stays
read-only; the standard launcher still enables input preparation only.

For P2's consumed frame/generation, `StaminaForHud` copies the existing native
host snapshot. Missing state, unmatched generation, unsupported geometry or
either maximum differing from 10000 falls back to the original consumer.
The stencil's live interior supplies width, original named placements supply
fill/notch starts, and the original eight-unit cap geometry is checked.

The preview follows native percentage mapping `(scaled - 500) / 9500`, clamped
to 0..1. Thus the 100%/25% labels below describe raw actor stamina fractions;
the visible interval retains the original 500-point minimum. It reconstructs
absolute role positions without reading P1 percentage or APT tween variables.

Only the selected P2 fill-right, body, fill-left and notch leaves use a private
packet. A 96-byte SystemHeap allocation contains a cloned 16-byte packet and
80-byte transform, protected through the examined synchronous consumer call
by a mutex and thread-local recursion guard. The private matrix pointer and
four translation components change; original packet state, all other matrix
bytes and color/effect bytes are preserved. GenericMeter's captured X basis
projects the role-local displacement into the final leaf transform.

The original packet/slot and private scratch are read back after `826E08F8`.
P1 and other widgets use the original consumer. There are no APT interpreter,
model or tree writes. The stencil, frame/background and shared city-health
state are not retargeted. The bounded synchronous-consumer lifetime evidence
from checkpoint14 remains applicable to this exact consumer only.

## Runtime acceptance

Release passed (`artifacts/coop-build-stamina-preview-final.log`). An initial
preview run, PID 18596, later closed normally through its window and was
archived as `stamina-preview-first-run.log`. Its verifier passed 72 composed
preview pairs and 3730 private packet samples with zero skips. That run alone
was not accepted as visual/lifecycle proof.

Final owned PID **11040**, copied save slot 2 (15%), Metropolis, native D3D12:

| Case | P1 native current/max | P2 native current/max | Observed pixels |
| --- | --- | --- | --- |
| A | 10000/10000 | 2500/10000 | P1 full red bar; P2 reduced red bar |
| B, change P2 only | 10000/10000 | 7500/10000 | P2 red bar lengthened; P1 stayed full |
| C, change P1 only | 5000/10000 | 7500/10000 | P1 red bar shortened; P2 retained its longer bar |
| Rejoin | 10000/10000 | 2500/10000 | The replacement P2 again had the reduced bar |

Windows Computer Use inspected the actual game window for each case. Both
blue city-health bars stayed alike, and both ordinary circular maps remained.
The native idle fade was allowed to operate; P2-only input restored the HUD
before visual inspection. No screenshot cropping or overlay bar is used.

Disposable Lua fixtures disabled regeneration temporarily and used explicit
native actor setters/getters. Original values/powers were restored before
despawn. P2 **17267**, generation `C0000001`, left at 08:02:19.387 in the log;
status/transport cleared. A P1 input restored the ordinary full single view,
verified visually. Rejoin created P2 **17279**, generation `C0000002`; case A
was repeated. Fixtures were restored again before stopping the owned process.

The final stopped log is `artifacts/coop-test/stamina-preview-final-run.log`:

- `stamina-preview-check.json`, `--require-preview`: **55** composed preview
  pairs, **110** balanced ancestry/pass summaries and **2743** verified private
  packet samples, across both generations. P1 had zero overrides; P2 had zero
  sampled skips. Right/body/left/notch samples were 715/220/715/1093. Every
  sampled HUD pass stayed below the packet-detail limit. Node records remain
  capped and are not a complete tree dump.
- `stamina-preview-split-check.json`: existing `--require-target 1
  --require-secondary --require-radar --require-status` passed with **55**
  world/capture/composition pairs, **17** radar pairs and **55** status pairs.
  Both generations matched consumed-camera/native-radar evidence; no sampled
  primary-meter mismatch was recorded.
- `stamina-preview-values-check.json`: A/B/C/rejoin had 6/10/9/27 matching
  sampled frames, native values and private packet targets. Binding sequence
  was `C0000001 -> 0 -> C0000002`; no old-generation preview appeared after
  clearing. No guest/script or experimental verification failures were found.

Diagnostic-off PID **25380** reached the ordinary title screen with input
preparation only. `stamina-preview-off-check.json` passed with zero experimental
messages and zero guest access violations. Both final owned processes stopped.

## Automated checks and review

The final native suite passed **655 tests** (`coop-apt-stamina-tests.log`).
Ancestry policy was observed red before implementation (90 failed checks in
`coop-apt-ancestry-red.log`). Tests cover exact instance/role selection, shared
balance geometry rejection, stack mismatch/overflow, native minimum mapping,
absolute positions, projected translation and unchanged packet/color bytes.

The tools suite passed **182 tests and 248 subtests**
(`artifacts/coop-stamina-preview-tools-final.log`). `--require-preview` implies
ancestry and verifies private bytes, desired-position math, projected basis,
pointer bounds, native targets and reciprocal packet/pass/composite joins.
Unexpected P1/non-role overrides and modified color/effect bytes are rejected.

A bounded read-only review found no actionable correctness issue in instance
scoping, guest reads, scratch lifetime/locking, transform math, generation
binding or cleanup. Pixel correctness was separately established by the owned
visual checks above. `git diff --check` passed. No generated code, extracted
assets, third-party tools, saves or runtime fixtures enter the commit.

## Reproduction and remaining work

```powershell
./tools/coop/probe.ps1 -Action Start -StaminaPreview
# Load copied slot 2 (15%) and enter Metropolis.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
./tools/coop/probe.ps1 -Action Script -Script tools/coop/stamina_status.lua
python tools/analysis/coop_apt_render_report.py --log artifacts/coop-test/probe.log --require-preview --output artifacts/coop-test/stamina-preview-check.json
./tools/coop/probe.ps1 -Action Stop
```

Independent shield/tired/KO state, colors/effect timing, variable capacities,
physical controllers, complete flight/combat, transitions/death, automatic
join/leave, distant streaming and performance remain open. This preview does
not claim complete co-op readiness.
