# Co-op: observed APT draw bridge and GenericMeter identity — 2026-11-04

Continues checkpoint13 (`fc5bd80`). The candidate APT adapter is now observed
inside both consumed split HUD passes. Geometry identity and a bounded consumer
lifetime analysis are recorded. No visual stamina substitution is implemented:
shared geometry still needs an unambiguous stamina-instance/ancestor selection.

## Implementation

Added default-off, restart-required `sr_coop_apt_probe`. It requires co-op input
and split preview and excludes benchmark mode. `probe.ps1 -AptProbe` enables the
split/native dependencies; ordinary `-SplitPreview` does not enable this probe.

`coop_apt_probe.cpp` keeps host-only, thread-local scopes around the existing
`82775EF8` HUD entry. Nested scopes restore their parent. It samples the first
three and every 120th consumed split frame:

- `8268C568`: checked reads of display-node and optional affine bytes.
- `826ACD18`: restores the previous carrier scope after the original call.
- `826D1648`: captures carrier/object/method/caller and the predicted transform
  slot, invokes the original adapter/consumer exactly once, then reads its
  original packet and transform again. It adds no guest-memory or PPC-register
  writes. All logs explicitly record `override=false`.

Packet and node details independently cap at 256 records per sampled HUD pass;
counters continue, and pass summaries disclose truncation. Transform readback
compares 80 bytes before/after the original call. Packet readback checks +4
against the predicted slot and +8/+12 against the incoming arguments. This is
post-call/equality evidence, not proof that native code performs no transient
writes.

`coop_apt_render_report.py` joins packets, pass summaries and composite frames
in both directions. It requires both HUD summaries for every sampled composite,
at least three positive pairs and a positive pair for each composed generation.
It validates source/transform pointer readback, finite matrix values, limits,
ordinals, unsigned owned generations and original-consumer verification flags.
It reads numbered log rotations oldest-first and resets at the latest startup.
Its scope explicitly excludes unique stamina identity and pointer retention.

## Owned runtime evidence

Release build passed (`artifacts/coop-build-apt-probe.log`). Owned PID **20652**
loaded copied save slot 2 (15%), Metropolis, native D3D12 split preview. P1 was
actor 1; P2 was 7348 in generation `C0000001`.

Temporarily disabled regeneration in disposable Lua fixtures, then used the
native explicit-actor setters/getters to establish 10000/2500, 10000/7500 and
5000/7500 stamina pairs (both maxima 10000). The first P2-only change preserved
P1, and the P1-only change preserved P2. Values were restored and powers enabled
before despawn. P2 left at 00:33:38.674; status/transport cleared. Rejoining at
00:33:41.891 created P2 **7361**, generation `C0000002`. The 10000/2500 case was
repeated and restored before stopping the owned process.

The retained run is `artifacts/coop-test/apt-final-run.log`:

- `apt-render-check.json`: **9422** verified packet observations, **62** sampled
  HUD passes and **31** composed two-view pairs across both generations. All
  captured packets used consumer **826E08F8**, caller **826AD0DC**, matched
  carrier+32/object and native packet/slot readback, and retained equal transform
  bytes before/after. No packet pass exceeded 256; all 62 node passes exceeded
  that limit. The **15872** recorded nodes are therefore a partial traversal.
- `apt-split-check.json`: passed `--require-target 1 --require-secondary
  --require-radar --require-status`, with **31** world/capture/composition pairs,
  **14** radar pairs and **31** status pairs. Both generations had matching
  consumed-camera and native radar producer evidence. There were zero sampled
  primary-meter mismatches. Early generation-2 evidence lacked a stationary
  camera/radar match; further sampling supplied it without changing tolerances.
- `apt-identity-check.json`: seven GenericMeter shape address/type/bounds matches,
  all three distinct-value cases and binding sequence `C0000001 -> 0 ->
  C0000002` passed. No packet from the cleared generation appeared afterward.
  No guest/script or experimental verification failures were found.

Visual inspection confirmed two world views, ordinary native bars and circular
maps after rejoin. Adapter observations do not imply every packet produced
visible pixels; ordinary native idle fading remains active.

Diagnostic-off PID **30764** reached the title screen with input preparation
only. `apt-off-check.json` passed with zero experimental messages and zero guest
access violations. Both owned processes were stopped.

## Identity and lifetime result

The detailed static/runtime evidence is in `docs/coop/APT_STAMINA.md`.
`826ADD40` ties carrier+4 to the owning movie root and carrier+32 to the loaded
shape render object. Captured headers plus file offsets and bounds identified
GenericMeter shapes 9, 15, 17, 23, 29, 35 and 38, including mask/fill/cap and
notch descendants. Captured nodes also matched the expected fill/notch sprites
and instance depths. Runtime addresses are evidence for this run, not constants
in the implementation.

The examined `826E08F8` path reads the packet's transform synchronously, writes
transformed vertices into a separate batch buffer, and copies packet state
values. No retention of packet/matrix pointers was found in that path. This
bounded static result excludes other consumer methods and reentrant execution.
Render objects have a separate, longer lifetime.

The geometry remains shared by multiple widget instances: character 21 also
supplies `mBalanceFill`, and cap/line shapes are reused. Do not identify stamina
from these geometry IDs alone. The next step is proving the stamina ancestor
scope, then limiting any private packet/transform to that instance and consumer.

## Checks and review

The final tools suite passed **173 tests and 89 subtests**
(`artifacts/coop-apt-render-tools-final.log`). The five new report tests include
invalid readbacks, missing/duplicate records, orphan joins, generation bounds,
latest-startup isolation and numeric rotation ordering. Review's orphan-record
and signed/oversized-generation findings were reproduced red before their fixes
(`coop-apt-render-review-red.log`). A bounded reviewer found no guest-state or
PPC-context mutation issue in the native probe. `git diff --check` passed.

No native packet replacement, interpreter/model/tree mutation, extracted assets,
generated game code, copied saves or runtime fixtures are included in this
milestone commit. Physical controllers, complete gameplay/lifecycle, distant
streaming and performance remain unverified.

```powershell
./tools/coop/probe.ps1 -Action Start -AptProbe
# Load copied slot 2 (15%) and enter Metropolis.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
# Collect sampled frames; preserve the latest run before another startup.
python tools/analysis/coop_apt_render_report.py --log artifacts/coop-test/probe.log --output artifacts/coop-test/apt-render-check.json
./tools/coop/probe.ps1 -Action Stop
```
