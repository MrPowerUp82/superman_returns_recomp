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

## Runtime continuation — checkpoint14

The experiment above ran in owned PID 20652. `826D1648` executes inside both
consumed HUD passes: 9422 verified packet observations across 62 sampled passes
and 31 composed frames, covering `C0000001` and replacement `C0000002`.
All captured packets used consumer `826E08F8` and caller `826AD0DC`.
Carrier+32 matched the render object; packet+4 matched the predicted 80-byte
slot; packet+8/+12 matched the incoming arguments; transform bytes were
unchanged after the original consumer. This establishes the bridge previously
marked unresolved. It does not assert that every packet produced visible pixels.

`826ADD40` (recomp.102.cpp:22384,22438,22453) writes the owning root from
character-table slot zero into carrier+4. Type-1 shape loading passes the
character index through callback table+96, storing its render object at +32.
The observed carrier layout is type +0, owning root +4, reference count/flags
+8/+10, retained movie/file handle +12 and four bounds floats +16..31.
`826AE6D0` (recomp.135.cpp:25894) initializes handle fields +16/+20/+24;
the runtime handle contents were not captured and remain unverified.

The seven changed carriers shared owning root `050741DC` and handle `051151A4`.
Subtracting GenericMeter's root offset `11C` yields asset base `050740C0`.
All seven runtime addresses, type words and bounds matched the extracted
GenericMeter character records (`artifacts/coop-test/apt-identity-check.json`):

| Carrier | Render object | Character / file offset | Geometry role |
| --- | --- | --- | --- |
| 05074308 | 05107EA8 | 9 / 248 | Mask descendant |
| 050743B0 | 05107F10 | 15 / 2F0 | Fill body |
| 050743F0 | 05107F78 | 17 / 330 | Fill/cap line |
| 05074498 | 05107FE0 | 23 / 3D8 | Cap shared by left/right |
| 05074540 | 05108048 | 29 / 480 | Notch glow |
| 050745E8 | 05132EA8 | 35 / 528 | Notch descendant |
| 0507463C | 05132F10 | 38 / 57C | Notch star descendant |

Addresses above identify this recorded run only; they are not runtime constants
in the implementation. Captured nodes also matched GenericMeter sprites
`mFillRight` (26, source 050744F4, node 0515A248, depth 30), `mFillFill`
(21, source 05074468, node 05159898, depth 34), `mFillLeft` (27, source 05074510,
node 0514F6F8, depth 39) and `mNotch` (40, source 0507467C, node 0514F744,
depth 43). Only the first 256 nodes per sampled pass were captured.

**Identity limit:** character 21 also supplies `mBalanceFill`; left/right caps
share character 23 and line geometry is reused. Geometry identity therefore
does not uniquely select the stamina widget. Ancestor/instance identity must
be established before substituting any packet.

**Bounded retention result:** `826D1048` (recomp.126.cpp:23791) returns
packet+4. The examined `826E08F8` consumer (recomp.105.cpp:26419,26553) reads
transform vectors +0/+16/+48 and writes transformed vertices into a separate
batch buffer. `826DE670` (recomp.35.cpp:25152) copies packet+8/+12 values into
batch state. No packet/matrix pointer retention was found in that examined
path: storage is needed through the synchronous call and its helpers; batched
vertices persist separately. Other consumer methods and reentrant execution
remain outside this result. Render objects live longer: `826A3180`
(recomp.122.cpp:25054) releases through callback+100 and clears carrier+32.

The next step is now **stamina instance/ancestor selection**, followed by a
private packet/transform limited to that instance and the examined consumer.
Do not use the geometry IDs alone to retarget every GenericMeter in the HUD.

## Instance selection and private preview — checkpoint15

The native stack protocol was resolved: `8268C568` and alternate `8268C670`
push a node's transform; `82685290` pops. A bounded host stack mirrors these
calls, validates pop identity and rejects further selection after mismatch or
overflow. It does not change the native tree or its version traversal.

Checked reads resolve each source's owning root, character table and exported
symbol pointers. Selection requires adjacent exported Main_Hud -> StaminaMeter
(placement depth 172) -> GenericMeter (depth 1), then the exact role character/
depth pairs already decoded: mask 13/24, right 26/30, body 21/34, left 27/39,
notch 40/43. Descendants must remain in that GenericMeter resource. Thus the
shared balance body 21/13 and mask 13/7 are excluded. These are asset character
indices/depths, not hardcoded guest pointers.

Runtime observation confirmed the exported chain and balanced stack in both
HUD views. The private preview (`sr_coop_stamina_preview`, probe
`-StaminaPreview`) then uses this chain every active split HUD pass, not just
the sampled diagnostic frames. Ordinary `-AptProbe` remains read-only.

The following relationships were checked against the raw component actions:

```text
p = clamp((scaledValue - 500) / 9500, 0, 1)
W = localX(mMask.mRight) - localX(mMask.mFill)
F = W + 8 + 8
S = original placement X of mFillLeft
N = original placement X of mNotch

leftX  = S - F*(1-p)
bodyX  = S + 8 - F*(1-p)
rightX = S + F - 8 - F*(1-p)
notchX = N + F*p
```

The constructor default LEFT direction applies to the imported stamina symbol;
the standalone GenericMeter root's RIGHT/width/scale demonstration is excluded.
GenericMeter onLoad records S/N before SetWidth (body F7B4–F83D); SetWidth and
SetFillWidth update the live mask interior (F868–FAA3 and 10528–1061D).
UpdateFill translates the complete fill pieces (106CC–10879), while
UpdateNotch moves the notch (108A4–10A1C). Stamina's initialized width is 180,
with W=148 and F=164; S=8 and N approximately -2.1. Live geometry supplies W,
and the original named placement records supply S/N. Cap character 23 must
still have an eight-unit horizontal extent. No P1 percentage or APT tween
variable is needed to reconstruct the desired P2 positions.

The native minimum 500 matters: raw scaled value 5000 corresponds to
4500/9500 of the rendered interval. Its predicted 86.315789-unit shift matched
the earlier observed 86.316 packet displacement.

For each selected moving role, the consumer hook computes
`deltaX = desiredRoleX - currentRoleLocalX`. It adds deltaX times the captured
GenericMeter X basis to the leaf matrix's four translation components. The
original packet's other fields and matrix/color/effect bytes are copied exactly
into one private 96-byte allocation. Only the private matrix pointer and its
translation differ. The examined synchronous consumer `826E08F8` receives that
packet; original packet/transform and private-storage readbacks are checked
after the call. A mutex and thread-local recursion guard protect the scratch
call. P1 and nonselected widgets call the original consumer unchanged.

The flag defaults off. The initial preview admits two native maxima of 10000,
positive supported geometry and matching consumed generation; unsupported or
missing state falls back to the original consumer. It separates fill/notch
**positions**. Original effect frames, low-shield/tired state, colors and
visibility remain shared; city health remains shared-world state. Capacity
changes, full independent UI animation/state and performance are not claimed.
