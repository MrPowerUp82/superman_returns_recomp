# Blended depth writers: C209 / 9A62 motion dependency audit

2026-10-25. Bounded local source/log audit only. This new document is the sole
write; no helper, renderer, test, build, game or history changes. References
describe the inspected working tree; function names anchor concurrent edits.
Context: `docs/static-world-family-dependencies-2026-10-25.md`.

## Findings

- **C209 is conditional rigid/skinned, not unconditionally static.** Uploaded
  VS boolean 0 selects skinning. Its false branch permits a small rigid
  unchanged-position proof; its true branch requires actual palette, indices,
  weights and branch inputs. No current boolean value was established here.
- **9A62 is remapped, affine-instanced geometry, without a VS skin/boolean/loop
  branch.** Its source position can have an unchanged-input proof, but the
  actual remap/position buffers, normalized vertex IDs, constants and fetch
  metadata are dependencies. Its instance layout is NOT the existing two-row
  2C45/29C1/C79C contract.
- **Blend alone does not suppress depth writes or geometric coverage.** A
  specifically audited blended depth writer may be replayable for final-depth
  position motion. That is different from proving the motion of every layer
  contributing blended scene color. Do not remove the global blend guard.
- The requested log identifies 9A62's pixel pair, but **does not identify a
  C209 pixel pair**. C209 pixel coverage qualification is therefore incomplete.

## Pinned sources and runtime observations

VS/PS paths are `artifacts/shaders/hlsl/<hash>.<stage>.hlsl`. SHA-256:

```text
C20921D0DBBD2F1F.vs 005A90869E42E100D5562F8D11F9D2E4DAD72857C6D580159C2F76C19C46372F
9A62E7C65051447A.vs 59DCD61C12275626E28C6554FBAA68E943D1C458F0219C5BA633940E056BAD81
04CC5C7EC9FDFD7E.ps BB85D31DCEF39B08CB99C2BC8EDDC3D405BE4F92043BBCFEDBAC3BA224071225
```

`logs/bench_motion_frozen_scene_2510.log:1544,1587` reports C209 in both phases:
160 draws, zero eligible/matches, zero stencil/no-Z-write/alpha-test counts,
160 blended draws. Lines 1552 and 1595 report the analogous 24 blended 9A62
draws. These are VS-family aggregates, not per-pixel semantic labels.

Log lines 596–598 explicitly pair **9A62 / PS04CC5C7EC9FDFD7E**. The PSO record
reports blend enabled, source 5/destination 6, RGB mask7, A2C0, depth enabled,
write1, function7, cull3, RTV format10, samples1. Repeated variants at 683–697
retain the pair; declaration/UV layouts can differ. Line 2169 also records an
actual geometry sample with this pair and two streams. PSO creation is not
proof of all subsequent issued draw state.

A case-insensitive search of the complete specified log finds C209 only at
1544 and 1587: neither family line carries a PS hash. Do not assign PS04CC to
C209 by analogy. Capturing its actual VS/PS binding and specialization is an
explicit missing input to the coverage audit.

## C20921D0DBBD2F1F: position dependency chain

All line numbers in this section refer to its VS file.

| Dependency / sink | Source evidence |
| --- | --- |
| Object/world and clip aliases | 589–598: bounded `CONST_REL`, world c0–3, WVP c8–11, world-view c4–7, palette base c22 |
| Actual position and skin attributes | 895: `srVertexInput(0, iPosition0).xyzw`; 899: blend indices; 900: `srVertexInput(16, iBlendWeight0).xyzw` |
| Main selector | 609: `RwSkinUsedWeights = BOOL_BIT(0)`; branch at 908 |
| Palette addressing | 912–955: first address `clamp(floor(index.x+.5),-256,255)` and rows `22+a0`, `23+a0`, `24+a0` |
| Extra influences | Nested BOOL bits1/2/3 at 957/1048/1122; index y/z/w addresses at 979/1053/1127; actual weights select the accumulated rows |
| Deformed local position | 1202,1227,1232,1247 build/assign `r2.xyz` from original position and the selected blended palette; original input W contributes at 1247 |
| Canonical local homogeneous vector | 1253, with literal c255 at 737: `r9 = (r2.z,r2.x,r2.y,1)` |
| Clip XYZW, including final Z | 1257/1261/1265/1269 dot local `r9` with WVP rows' `.zxyw` |
| World XYZW and export | 1339/1345/1352/1359 produce world W/Z/Y/X in `r1.w/y/z/x`; 1442 exports `r1.xzyw` as TEXCOORD1, canonical world XYZW |
| Final clip fixups | 1505–1508 change XY with actual shared screen transform and half-pixel offset; no later Z/W rewrite |

**When actual uploaded BOOL0 is false:** the skin block is skipped. Position
depends on decoded POSITION0 XYZ and c0–3 world rows; input W is replaced by
one for the projection/world dots. Capture all 16 world words conservatively,
the source domain and the branch selector. Return c8–11 separately as
**object-to-clip WVP**, not a world-to-clip camera. Skin weights/indices,
palette and BOOL1–3 do not reach the position sinks in this branch. The
`dFreeze`, `dBurn`, `dTransparency` and blend-control constants feed shading
varyings, not a position deformation in this false branch.

**When BOOL0 is true:** positions may deform. Track actual POSITION0 XYZW,
blend indices, required weights, BOOL0 and reachable nested BOOL1–3, and every
actually selected palette word. First influence rows are unweighted if BOOL1
is false; do not assume the four-weight path always executes. Addresses come
from the supplied indices directly, not an invented `3 * boneID`. Bounded
`CONST_REL` returns zero for out-of-bank rows; negative/clamped addresses can
also reach below c22, so blindly hashing only c22–255 is not a general proof.

There are no main-body loop-constant reads. The generic helper loop in the
shared preamble is not a skin animation loop. Palette equality plus unchanged
actual fetch inputs could establish an **unchanged pose/position** case; a
different pose requires separately verified previous deformation/identity.
Mesh identity, a shader hash or a guessed instance/bone ID cannot provide it.

## 9A62E7C65051447A: remap and six-row instances

All line numbers in this section refer to its VS file.

| Dependency / sink | Source evidence |
| --- | --- |
| Aliases | 583–589: c4=`instance_data`, transforms relative to c100, camera VP c232–235, view c0–3, inverse-view c229 onward |
| Vertex ID and literals | 712: `SV_VertexID & 0xFFFFFF`; 687: c254.x=6 and c254.w=.5 |
| Instance selection | 849–861: guarded reciprocal of c4.x, multiply `(ID+.5)`, then truncate |
| Mesh/remap index | 866–879: subtract instance*c4.x, add c4.y, truncate/floor, then fetch semantic1.x |
| Actual position fetch | 880–882: floor the remap value, fetch semantic0 XYZ, set local W=1 |
| Transform address | 895–901: six times the selected instance, rounded/clamped into a0 |
| Homogeneous world construction | 906/911/916/921 use c100+a through c103+a |
| Camera clip | 925/929/933/937: four world-vector dots with c232–235 |
| Exported world | 1114: TEXCOORD1=`r5.xyzw` |
| Final clip | 1153–1156: shared screen/half-pixel XY fixups only; Z/W remain the VP dots |

Writing A/B/C/D for rows `100+a`/`101+a`/`102+a`/`103+a`, and local position
`(x,y,z)`, the resolved mathematical construction is:

```text
world.xyzw = x*A.xyzw + y*B.xyzw + z*C.xyzw + D.xyzw
clip = VP(c232..235) * world
```

This is an affine position construction, not a skin-weighted VS branch, and
does not itself establish a static instance. **Keep all 16 XYZW transform
words.** Although world XYZ uses only their XYZ lanes, the W lanes produce
world W, which participates in every final clip dot. The older two-row
family's lighting-W exclusion does not apply. c232–235 is world-to-clip here;
c0–3 is a view-varying bank, not this family's projection bank.

c104+a supplies freeze/burn/transparency-related shading (952,967–969);
c105+a.zw supplies another shading output (1094). They do not feed world/clip
position. c5, inverse-view and normals/tangent/color/UV fetches likewise feed
shading, not position. No main-body BOOL or loop-constant read was found.

The position signature requires **c4.xy plus every reachable four-row world
transform**, with source/range domains. Reachability needs the verified actual
normalized ID envelope and the exact source arithmetic, including reciprocal
rounding boundaries, truncation, address clamp and zero out-of-bank behavior.
Do not delegate to the existing two-row range proof or assume ordinary
positive integer stride without validating it. Transform addresses can also
overlap other named banks; exclude a camera word from world dependencies only
when it is genuinely outside the reachable transform read set.

Constants alone are insufficient. Semantic1's remap buffer and semantic0's
position buffer need actual bound resource identities, generations, content
versions, ranges and fetch metadata. `srVertexFetchImpl:398–441` uses SRV,
offset, stride and type, performs bounds checks and typed conversion; UINT
remaps are numerically converted to float at 431–432. Stream layout alone is
not a snapshot of those bytes. Require proven address ranges without uint
address wrap, finite remaps and the actual typed-fetch semantics. Comparing
raw transient SRV indices or guessing instance identity is insufficient.

## Pixel coverage and the blend gate

For PS04CC, line 920 initializes `r10 = iColor1`; 4329 rewrites only RGB and
4333 exports RGBA. Therefore original-body alpha is interpolated COLOR1.W.
The paired 9A62 VS produces that alpha through 952,1012,1019,1136: for ordinary
finite inputs it is `clamp(1 - c[104+a].z, .1, 1)`. It is **not proven one**;
the source-alpha/inverse-source-alpha PSO is real color compositing.

The PS has no declared SV_Depth/SV_Coverage output or original-body
discard/clip found in this audit. Its generic tail at 4335–4340 can enable
soft-particle alpha changes, alpha-test discard or A2C behavior. The actual
uploaded specialization, alpha threshold, actual PS/PSO, sample mask and
sample count must still be qualified; “no alpha test” from a VS-family
aggregate is not that complete proof. C209's unknown PS remains a hard gap.

`port/src/native_renderer/native_renderer.cpp:3123–3125` currently requires
single sample, enabled/written Z, no stencil, no debug Z override, RGB blend
bits equal ONE/ZERO and no effective alpha test. `native_motion_capture.inc:89`
uses that `opaque` gate for static correspondence. Consequently the logged
families are rejected by blend **and** lack an accepted position dependency
contract; changing only one condition cannot qualify them.

For a color-blended draw that writes depth, RGB blending does not inherently
alter the triangle coverage or depth sample. A separately qualified final-depth
motion replay can therefore be valid even when its original color blended.
Keep exact current clip, viewport/scissor, cull/winding, depth clip/bias,
depth surface/version, draw ordering and fragment survival identical. No
discard, alpha-to-coverage, stencil, custom depth/coverage or unsupported
specialization may be silently omitted. Equal-depth/coplanar competing writers
must not select an arbitrary history.

The current consumer uses EQUAL/no-depth-write and overwrites its velocity/
validity targets (`port/src/graphics/d3d12/temporal_motion.cpp:73–92,280–299`).
Its header's `Render` contract at `temporal_motion.h:74–87` explicitly excludes
unsupported alpha/translucency/custom coverage emulation. A narrowly audited
depth-writer qualification would need to distinguish geometric depth motion
from that unsupported composited-color motion; it must not just relabel all
blending as opaque. Invalid correspondence clears validity, but does not
reproduce original fragment coverage.

## Minimal next proof, with no implementation in this task

1. Obtain C209's actual issued PS pair and uploaded BOOL0..3. First consider
   only its proved BOOL0=false rigid branch; otherwise retain rejection until
   palette/fetch/pose dependencies are complete.
2. For 9A62, independently prove actual dynamic fetch/remap ranges, c4.xy
   addressing and reachable four-row XYZW dependencies. Preserve actual
   uploaded shared fixups/fetch metadata for both families. Do not infer
   moving or deforming correspondence from mesh/resource identity.
3. Add a separate, fail-closed depth-writer coverage qualification for the
   specific audited PS/state, then demonstrate pixel agreement against the
   actual final depth. Keep all other blends unsupported. Distinguish valid
   depth-position motion from unresolved transparency/color-layer motion.

Nonzero-Z regions with black/invalid motion are consistent with missing
qualified contributors, but this audit does not assign those pixels to either
family or prove they are ground. No universal ground/material/actor classifier,
complete scene coverage, deformed-object history or DLSS readiness is claimed.
