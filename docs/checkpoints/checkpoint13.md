# Co-op: APT stamina bytecode and draw ownership — 2026-11-04

Continues checkpoint12 (`0b19e50`). This is a validated research/tooling
milestone toward independent native stamina graphics. The two panes still
share the existing APT widget state. No native renderer behavior changes in
this commit.

## Result

Added `tools/analysis/coop_apt_probe.py`, a read-only inspector for the game's
big-endian, decoupled 1:7:4 APT/constant pairs. It emits typed constants,
function parameters/bodies, exact byte offsets, dictionary operands, branch
targets and root/sprite timeline placements, including clip/init/frame actions.
It records affine matrices, clip depths, instance names and input hashes.
It validates offsets/counts, function extents and branch boundaries, enforces
shared decoding/nesting limits, and rejects unsupported action/control records.
Non-timeline character payloads are explicitly outside its coverage.

The tool processed the owner's three previously extracted local pairs:

| Component | Characters, including import/empty slots | Action streams | Instructions including function bodies |
| --- | --- | --- | --- |
| Main_Hud | 67 | 23 | 2678 |
| StaminaMeter | 8 | 6 | 454 |
| GenericMeter | 47 | 10 | 1095 |

Outputs are ignored under `artifacts/coop-research/apt-hud/*-evidence.json`.
`artifacts/coop-apt-assets-check.json` passed all three components and checks
selected method offsets, the float near 0.01 in StaminaMeter.SetPercent and
the signed-byte representation in GenericMeter's fill/notch direction paths.
These are static checks, not runtime interpreter observations.

The reusable finding ledger is `docs/coop/APT_STAMINA.md`. It identifies the
import/instance chain from Main_Hud to StaminaMeter to GenericMeter and the
separate fill, mask and notch pieces. It records method offsets and the native
draw context/packet candidates, distinguishing observations from inferences.

## What the investigation established

`Main_Hud.setStaminaValue` calls `mStaminaMeter.SetValue`. StaminaMeter forwards
to the generic meter and its notch, and also evaluates low-shield state.
GenericMeter uses tweens and translates fill pieces through `_x`; the notch
has its own update. Whole-widget scaling is not established as a valid visual
substitution.

The native drawing path `826D1D60 -> 826A0BE8 -> 8269F118 -> 82690360` uses
context `829A17C0`, separate from interpreter context `829A17BC`. However,
display traversal resolves versions and rewrites child/sibling links. A live
display-node patch around P2 drawing is therefore not justified by this evidence.

The candidate adapter `826D1648`, reached through callback table+112 from
`826ACD18`, passes a packet and current 80-byte transform-slot pointer to a
native object's vtable+32. Private packet/transform substitution is a candidate,
not an implemented solution. The bridge from HUD group 23 to that adapter,
stamina node/packet identity and consumer retention duration remain unresolved.

The native investigation used a bounded read-only worker and existing generated
PPC sources. The available Windows REA provider's unsupported PPC target was
already established in checkpoint12; this milestone claims no new REA/native
runtime observation. Generated game code was not edited.

## Validation

- Initial synthetic tests were observed red against the unimplemented inspector
  (`artifacts/coop-apt-tests-red.log`). The final tools suite passed **168 tests
  and 71 subtests** (`artifacts/coop-apt-tools-final.log`).
- Eleven new tests cover typed constants/registers, function bodies/parameters,
  immediate endianness, branches, placed-instance matrices/clip events,
  missing roots, decoding budgets, malformed/truncated records and CLI behavior.
  They use synthetic bytes, not extracted game assets.
- A reviewer identified missing root validation and uncharged pool expansion;
  these were fixed during parallel implementation. The reviewer also identified
  assignment-name lookahead crossing a function extent. That case was reproduced
  red (`coop-apt-assignment-red.log`) and fixed with a bounded lookahead.
- Review caught an insufficient budget regression. The final pool/parameter
  fixtures use distinguishing budgets of 13/16. Isolated AST mutations removing
  each respective charging rule allowed those fixtures through, confirming the
  regressions distinguish the missing validation.
- All three local asset pairs decoded successfully after the fixes. CLI refuses
  an output path resolving to either input asset; corrupt input exits before
  creating output. `git diff --check` passed.

No C++/native hooks, game data or dependencies changed, so no new Release build
or gameplay run is claimed. No game process was launched. The previous runtime
stamina handoff evidence remains in checkpoint12.

## Continue here

First add a bounded opt-in observation of `826D1648` inside `CurrentSplitHud()`.
Prove whether it sees both consumed HUD views, then correlate source/transform
records with stamina fill/notch identity. Establish packet retention lifetime
before attempting a private per-view packet. Preserve shared city-health state
and avoid calling shared APT interpreter setters on the render thread.

Independent bar pixels and the broader physical-controller, combat, lifecycle,
transition, distant-streaming and performance validation remain open.
