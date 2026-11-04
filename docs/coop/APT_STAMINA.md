# Stamina APT evidence — 2026-11-04

This ledger continues checkpoint12. Static asset/native observations below do
not establish execution, packet lifetime or independent pixels in either pane.

## Asset chain

`tools/analysis/coop_apt_probe.py` decodes the owner's extracted constant/APT
pairs. It supports the observed **big-endian, decoupled 1:7:4, 32-bit** format.
It preserves typed constants, function bodies/parameters, dictionary references,
branch targets and file offsets, plus root/sprite timeline placements and
clip/init/frame actions. It rejects unsupported opcodes/controls/characters
rather than treating their payload bytes as instructions. Geometry, font,
text, bitmap, sound, video and morph payloads are not decoded.

Format layouts were cross-checked against
[LibreSAGE/AptTools](https://github.com/LibreSAGE/AptTools/tree/7b6d499340181d15e484b58c0fc78b14215a5455/crates/apt/src).
That implementation targets little-endian data. This game's bytes require
big-endian words **including unaligned immediate operands**: StaminaMeter
`0xCE4` is `B4 3C 23 D7 0A`, a float near 0.01. GenericMeter's fill/notch
direction paths use `B5 FF`; the probe reports its signed interpretation -1
and retains `raw=ff`. These are decoded representations, not a runtime trace
of the interpreter's numeric operations.

Observed local pairs from `game/DATA/ui.AST`, retained only under
`artifacts/coop-research/apt-hud`:

| Component | Constant / APT files | Characters | Action streams | Instructions including function bodies |
| --- | --- | --- | --- | --- |
| Main_Hud | main_hud-26.bin / main_hud-27.bin | 67 | 23 | 2678 |
| StaminaMeter | staminameter-2.bin / staminameter-3.bin | 8 | 6 | 454 |
| GenericMeter | genericmeter-20.bin / genericmeter-21.bin | 47 | 10 | 1095 |

The character counts include empty/import slots; they are not draw counts.
Streams include clip actions and scripts in child sprite timelines, not just
the root frame. The JSON records input SHA-256 hashes and each stream context.
An `assigned_member` label means the adjacent dictionary-push / anonymous
function / SetMember pattern; it is not general symbolic name recovery.

Main_Hud imports StaminaMeter as character **53**, placed as `mStaminaMeter`.
StaminaMeter imports GenericMeter as character **1**, placed as `mMeter`.
GenericMeter itself places a sprite `mMeter` (character **45**), with
`mMask` (13), `mFillFill` (21), `mFillRight` (26), `mFillLeft` (27) and
`mNotch` (40) among its timeline instances. These are file-local character
indices, not guest object addresses.

Selected function body offsets and extents in the corresponding APT blobs:

| Component / assigned method | Body offset | Bytes | Observed operations |
| --- | --- | --- | --- |
| Main_Hud.setStaminaValue | 0x2E18 | 28 | Resolve mStaminaMeter; call SetValue |
| Main_Hud.setStaminaMax | 0x2E60 | 148 | Get width/max; compute proportional width; set max and width |
| StaminaMeter.SetValue | 0xBD0 | 162 | Set mMeter value/notch; evaluate percent for mShield low state |
| StaminaMeter.SetPercent | 0xCD8 | 80 | Multiply argument by ~0.01; call meter percent/notch-percent setters |
| GenericMeter.SetValue | 0xFF48 | 202 | Bound value to min/max; CalculatePercent; SetPercent and SetNotchValue |
| GenericMeter.SetPercent | 0x10078 | 214 | Construct tween state and update fill/notch |
| GenericMeter.CalculatePercent | 0x10648 | 90 | Divide (value - min) by (max - min) |
| GenericMeter.UpdateFill | 0x106CC | 430 | Update mValue, move fill pieces through _x; animate fill/notch clips |
| GenericMeter.UpdateNotch | 0x108A4 | 377 | Update notch value and position through _x |

**Inference:** a correct independent visual state must account for translated
fill pieces, masks, notch/tweens and low-shield state. A single whole-widget
scale or native cache-word replacement is not established as sufficient.

## Native render boundary

The generated PPC code supplies the following static evidence. Generated code
remains unmodified. Addresses identify functions in this executable build.

| Question | Observation / source | Status |
| --- | --- | --- |
| Separate drawing entry? | `826D1D60` in recomp.109.cpp calls `826A0BE8`, then `8269F118` / `82690360`. Movie+224 supplies a context. The draw path switches `829A17C0`; interpreter commands use `829A17BC`. | Static path identified |
| Is the display tree immutable during drawing? | `82690360` (recomp.136.cpp) resolves versions and replaces child/sibling links. `826857C0` (recomp.78.cpp) follows version links at +40. | No; temporary live-node mutation is not justified |
| Which fields affect traversal? | Node source +4, affine pointer +8, color pointer +12, depth/clip-depth +20/+22, flags +24, version +32, reference count +36, next version +40, sibling +44, child +48. Copy/constructor paths: `826916D0` (recomp.52.cpp) and `82685320` (recomp.42.cpp). | Field map from static worker analysis; runtime ownership unverified |
| Where are transforms consumed? | `8268C568` (recomp.66.cpp) composes six-float affine/color state around a node. Alternate `8268BAC8` (recomp.43.cpp) uses 80-byte slots: matrix +0..63, packed colors +64/+68; pool base `829A158C`, index `829A1598`. | Static candidate |
| Is there a packet adapter? | `826ACD18` (recomp.33.cpp) forwards source+32 through callback table+112; initialization in recomp.17.cpp assigns `826D1648`. That adapter (recomp.63.cpp) builds a packet at manager+84 and invokes object vtable+32. Packet+4 is a transform-slot pointer; +8/+12 receive incoming r4/r5. | Static candidate |
| Is that adapter inside each consumed split HUD pass? | HUD group 23 dispatches participant vtable+8 through `82783948` (recomp.68.cpp). Its bridge to the packet adapter was not established in this bounded analysis. | Unresolved |
| Which packets belong to stamina? | Asset-local names/character IDs have not been tied to live nodes or packet sources. | Unresolved |
| Can a packet/transform be replaced privately? | Separate host/guest scratch packet and transform could avoid live-tree/interpreter mutation. Consumer retention duration is unknown. | Design candidate only |

The prior Windows REA provider did not support this PPC target. This pass reused
the existing generated disassembly and local assets; no new REA execution or
native runtime observation is claimed. The worker's concrete source locations
and a focused instruction extract are retained locally as
`artifacts/coop-research/apt-render-boundaries.txt`.

## Verification and next experiment

The synthetic tests cover typed operands, anonymous function assignment,
big-endian unaligned float/signed-byte values, branch boundaries, placed-instance
matrix/clip-depth/event offsets, missing roots, bounded decoding and corrupt
headers/pointers/counts/opcodes/function extents. CLI tests protect input files
and reject bad input before creating output. No game assets are test fixtures.

Run the probe on each extracted pair, for example:

```powershell
python tools/analysis/coop_apt_probe.py --apt artifacts/coop-research/apt-hud/staminameter-3.bin --const artifacts/coop-research/apt-hud/staminameter-2.bin --output artifacts/coop-research/apt-hud/staminameter-evidence.json
python -m pytest tests/tools/test_coop_apt_probe.py -q
```

The next runtime experiment should first observe whether `826D1648` executes
inside `CurrentSplitHud()` for both views, then correlate its source/transform
with the identified stamina fill/notch nodes. Keep that experiment opt-in and
read-only. Only after packet identity and retention lifetime are established
should a private per-view substitution be attempted. Native city-health state
and the complete interpreter state remain shared until explicitly separated.
