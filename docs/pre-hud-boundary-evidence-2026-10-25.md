# City-path postprocess to HUD boundary

Date: 2026-10-25. Bounded static shader audit plus inspection of five existing
PNGs and their existing readback log. Only this report was written. No code,
build, game execution, or commits. Scope is the captured city gameplay path;
loading, pause/menu, alternate cameras, other scenes and Vulkan are unqualified.

## Decision

**Use the point immediately before the first actual
VS69523DC2B02555D3 / PS40746066C58DA8D5 sprite draw, after the same frame's
actual VS815DF1B3CCBFAA4E / PSB397B05F4CE5D5BB final spatial filter has written
the same final color resource/subresource.** In this capture that is the
checkpoint named `d3079`, after `d3078`. Require the sequence, resource/content
identity, full preparation/actual draw issuance, and no intervening invalidation.
A pair hash, output size, PSO creation, font draw, or a bind alone is insufficient.

The evidence proves the **candidate ordering and shader meaning**. It does not
already prove a reusable same-resource observer: the saved files/log do not
contain per-draw COM resource/view identity or the numerical sprite matrix.
Those are runtime guard inputs for the proposed observer, not facts invented
from these images. Keep qualification fail-closed until those inputs agree.

## Evidence references and snapshot identity

All shader names below refer to `artifacts/shaders/hlsl/<name>.hlsl`; references
are one-based. Renderer paths are under `port/src/native_renderer/` unless stated.
Renderer sources are under concurrent parent edits; function names anchor their
line references. Shader SHA256 values pin this audit:

| Shader | SHA256 |
| --- | --- |
| `0408D37D5D96E869.vs` | `827F974309BAA59AA62228FEF048F7183C431B0315814516B56BB02594EF420F` |
| `84FDB8B3BB1596B3.ps` | `1BEDBEDEC98BB6E544F1E244B195DBEE5438832B27C411538DA595E85D9A662A` |
| `815DF1B3CCBFAA4E.vs` | `2F010D20ECB66BFCE889379977423424D870C62C5DEDC77A62F443E30FFC2653` |
| `B397B05F4CE5D5BB.ps` | `AAF79AC6F2CD54E5392E4FC8A69372FB5B48A2F8C790B96CB63218532B2B61BE` |
| `69523DC2B02555D3.vs` | `A4C7F82A964FE33F759581B9C45129B991F22C81CA2F40CA6594266FDF4BDA29` |
| `40746066C58DA8D5.ps` | `9D9FDD3637E76105B26C225B17908CEB739B1AE37EC706AE414F2E467C9458E0` |
| `73208454D8616B19.vs` | `DA1E3837CAF1D6692F7A232FA666ABAEA1C2C06F5BBD024B7580EA188818CECF` |
| `F047F7F01CA481C6.ps` | `6350F5A1F4CA2E64E52583B13A57B806EF903E0E52F1480554F2690921D53B43` |

Capture directory: `logs/motion_vertex_range_2510/`. Each image uses the complete
filename `candidate_color_d<draw>_vs<VS>_ps<PS>_s1908.png` and corresponding `.raw`.
The `d` values below are **capture labels**, not stable game draw indices.
`PrepareDraw` increments `draw_calls_in_frame_` at `native_renderer.cpp:4783`,
before the diagnostic names it at 5014; other debug logs print counter minus one.
Do not hardcode 3077/3078/3079 or compare different counter conventions.

| BEFORE draw label / pair | Inspected destination image | Meaning |
| --- | --- | --- |
| 3077: 0408 / 84FD | garish reinterpretation resembling normal/packed data | destination has not received this tonemap draw; not scene color |
| 3078: 815D / B397 | ordinary lit city, no crosshair/bar | tonemap has run; final spatial filter is still pending |
| 3079: 6952 / 4074 | ordinary lit city, no crosshair/bar, spatially filtered | selected candidate: final filter has run, first observed sprite is pending |
| 3083: 7320 / F047 | crosshair and outlined top-left bar already present | too late for a clean scene image |
| 3100: FCB6 / 9841 | full blue top-left bar and crosshair present | first font checkpoint is too late |

The images are copies of the **bound destination before the named draw**, not
the shader's input texture or output after it. `PrepareDraw` calls
`BindRenderTargets` at 4998, then queues the checkpoint at 5004–5014.
`native_motion_capture.inc:217–233` transitions/copies subresource 0 and restores
state; 236–262 maps after the slot fence and writes the raw image. The existing
log `logs/bench_motion_vertex_range_2510.log:2341,2343,2345,2346,2348` reports all
five saved 1280x720 fmt28 copies with `fence_completed=true`. fmt28 is the captured
RGBA8 UNORM view; it does not establish a new HDR/color-space contract.

`inspection.json:15–37` still marks all five `boundary_verified:false`; it was
not modified. The five PNGs support the visual ordering, not a decoded normal
buffer semantic for 3077. Its appearance alone must not name the aliased data.

Also keep frame IDs distinct: log line 2332 says **host frame1909,
guest_swap1908**; suffix `s1908` is the swap number, not host frame1908.

## Draw3077: exposure, bloom and tone-map composition

VS0408 takes POSITION0 and TEXCOORD0 (595,601). At 841–850 it copies input XYZ
to clip XYZ, forces clip W=1, and copies UV XYZ to TC0. Lines 853–855 apply the
host screen transform and half-pixel offset. There is **no world, view, camera
or orthographic matrix multiplication**, vertex color, normal or light term.
Input geometry determines viewport coverage; passthrough clip positions alone
do not prove the draw is fullscreen.

PS84FD declares PS c0=`exposure`, c1=`bloomIntensity`, c2=`toneMapContribution`
(593–595). Bindings 599–610 establish slot0=`RenderTargetSampler`,
slot1=`LuminanceSampler`, slot2=`bloomSampler`. It samples luminance at fixed
UV=(0.5,0.5), bloom at TC0.xy, and scene RGB at the same UV (852–861).

Resolved swizzles, for ordinary finite inputs:

```text
S = RenderTargetSampler(uv).rgb
B = bloomSampler(uv).rgb
L = LuminanceSampler(0.5,0.5).r
X = S + bloomIntensity.x * B
a = exposure.x / (L + 0.001)
T = (a*X) / (1 + a*X)                // componentwise
out.rgb = X + toneMapContribution.x * (T-X)
out.a = 1
```

Evidence: 865 adds bloom; 869–880 computes exposure/luminance scale;
885–910 computes the rational tone curve with register permutations;
914–915 mixes tone-map contribution and writes alpha1. Preserve guarded
reciprocals from the source; the equation is not a proposed rewrite.
There is no per-surface lighting in this pass; it processes already lit color.
Generic translator tails at 917–922 can add soft-particle/alpha behavior only
if runtime specialization enables it, which must be checked, not assumed.

The pair is therefore a real tonemap/composite, not a normal-map shader. A
normal-looking **pre-draw destination** is consistent with aliased prior contents
being overwritten. It is not a reason to publish that destination before3077.

## Draw3078: adaptive nine-sample color postprocess

VS815D copies **all four** POSITION components to clip and UV XY to TC0
(841–849), then applies screen/pixel fixups (852–854). In contrast to VS0408,
it preserves input W. Again there are no world/view/orthographic constants,
colors, normals or lighting in the original VS body. The actual declaration
can supply a default W: this shader does not force it itself.

PSB397 has only slot0=`SceneSampler` (585–588) and no used game float constants.
Lines 830–862 sample the eight surrounding texels plus the center:
`(-1,+1),(+1,+1),(+1,0),(0,+1),(0,-1),(-1,-1),(-1,0),(+1,-1),(0,0)`.
The shared `tfetch2D` helper at 246–256 expresses offsets in the guest texel grid,
including descriptor scale/gamma rules; these are not arbitrary UV displacements.

Let `C` be the center RGB, and `Ci` the eight neighbor RGB values. The resolved
ordinary finite/nonzero-denominator expression is:

```text
wi = saturate((20/3) * dot(Ci-C,Ci-C) / max(dot(C,C),dot(Ci,Ci)))
W = sum(wi)
A = sum(wi*Ci) / W
k = 0.66 * saturate((W-2)/4)
out.rgb = C + k*(A-C)
out.a = 1
```

The constants are emitted float approximations at 690. Evidence: color/delta
squared lengths 866–1025; guarded reciprocals and normalized contrasts
976–1039; saturation 1044–1049; weighting/accumulation 1054–1153; normalization,
threshold and blend 1158–1180. For zero weights/black regions the shader's
guarded arithmetic matters; do not implement literal division by zero from
this compact equation. Existing `docs/vulkan-m3.md:73–81` independently records
the zero-weight edge-AA translation issue, but the original expressions above
are the source of classification here.

This is an adaptive color-contrast spatial/edge filter. It does not read normals,
light, world matrices or scene depth in its original body; it is not HUD artwork
or another exposure stage. The before3078 image is clean but **not after this
last filter**. The before3079 image is the stronger final-color candidate.
Generic specialization tails still require runtime checks (1182–1187).

Log `bench_motion_vertex_range_2510.log:1666–1671` records these two pairs' PSO
variants: blend0, color maskF, depth0/write0, rtv0=28, samples1. This supports
overwrite postprocess use. It is a PSO-creation observation, not the complete
state or resource lineage of draw3077/3078 at swap1908; recheck live draw state.

## Draw3079: unlit vertex-colored textured sprite path

VS6952 reads POSITION0, COLOR0 and UV0 (597–609; actual fetches 849–851).
`WorldViewProjection(i)` maps directly to VS c0..c3 (579). Lines 854–866 compute:

```text
clip = float4(dot(position,VS_c0), dot(position,VS_c1),
              dot(position,VS_c2), dot(position,VS_c3))
color = input COLOR0                 // 870
uv = input TEXCOORD0                 // 874
```

The `.wzxy` on both dot operands is the same permutation, so it does not reorder
the mathematical dot. Host screen/pixel correction follows at 877–879.
There is no separate world transform, skinning, normal output or light vector.
**Static source does not prove that c0..c3 are orthographic.** They can contain
an arbitrary transform. This capture/log does not dump the actual four rows,
vertex positions, or sprite texture contents. An observer should inspect the
already captured draw constants/geometry: finite affine/no-perspective W for
the sprite plane, screen-space placement consistent with the active viewport,
valid color/UV input layout, nonempty scissor, appropriate overlay depth/blend
state. Do not assert a guessed numerical orthographic matrix or recognize HUD
from `WorldViewProjection` by name.

PS4074 initializes `r0=TC0`, `r1=COLOR0` at 687–688, samples slot0 at 825 and
writes **`texture(uv).rgba * vertexColor.rgba`** at 828. There is no lighting,
normal mapping, tonemap, scene-color filter, or game float constant evaluation
in this body. Generic alpha/soft-particle specialization can modify the tail
(830–835), so include those bits in qualification.

That unlit sprite mechanism plus the observed sequence explains the first HUD
contribution. It is not a universal semantic HUD classifier: the same mechanism
can draw world sprites, menus or textured overlays elsewhere. The capture only
brackets visible crosshair/outline insertion between before3079 and before3083;
it does not individually identify which of draws3079–3082 paints each element.

As corroboration, VS7320 c0..c3 transforms POSITION and passes COLOR0
(579,843–863); PSF047 reads COLOR0 at 679 and copies it at 817. It is an unlit
solid vertex-color path. Its **before3083** destination is already contaminated;
do not move the boundary to it. Font detection is still later and cannot undo
earlier HUD writes.

## One qualified SceneColorBoundary observer: contract

Implement the observation at a shared **committed before-draw** point used by
indexed, nonindexed and inline draws, after targets/aliasing, PSO, constants,
streams/indices, viewport/scissor and all preparation checks succeed, and before
the first actual sprite GPU command. Record successful postprocess draw issuance
separately, after the real command is emitted; `PrepareDraw`/PSO creation can
precede a failed or skipped draw. Examples of actual draw gates are
`native_renderer.cpp:5963–5965,6066`; include equivalent inline/quad paths.

Proposed narrow state machine (a contract, not a new global pass table):

1. Begin a **new primary city-scene epoch** with distinct host frame, guest swap,
   device/resource epoch and scene ownership. Clear previous arming and emitted
   state; do not carry a “saw final shader” flag across frames.
2. Observe the first *issued* VS0408/PS84FD tonemap for that epoch with valid
   scene/luminance/bloom source provenance and viewport-covering overwrite state.
   Track its output content and any explicit resolve/copy feeding the next pass.
   Destination and source can be distinct resources: validate lineage rather
   than inventing an in-place relationship from similar pictures.
3. Observe the first *issued* VS815D/PSB397 final filter consuming that current
   tonemapped content. Require audited bindings/specialization, full valid rect,
   all RGB write channels, nonblended overwrite and nonoccluding depth state.
   Store its **actual output resource, allocation/device lifetime, RTV format,
   subresource/plane, viewport/valid rect, postprocess draw serial and content
   epoch**. Output size/sample count are compatibility checks, not identity.
4. On the **first following actual VS6952/PS4074 sprite** in the same scene epoch,
   require that fully bound destination to be this exact retained final-color
   resource/subresource and content, with matching rect and no intervening write.
   Validate the runtime sprite transform/layout/state described above. For this
   strictly bounded path, accept no unclassified intervening draw to that output;
   if other-target work occurs, require that it cannot alias/change this content.
5. Emit **one** SceneColorBoundary with the resource/version and both draw serials,
   after final postprocess and before that sprite. Consume it in queue order:
   snapshot into private immutable storage or run the intended consumer there,
   then draw HUD. Mark the epoch emitted; later sprites/fonts cannot emit again.
   Recording order on the same queue establishes GPU order; no per-draw CPU wait
   is required. Cross-queue consumers need explicit synchronization.

Do not retain only a pointer to final color and read it at present/end-frame:
HUD subsequently mutates that same texture. A boundary event needs a usable
version/snapshot or a consumer executed immediately before the HUD write.
The snapshot and descriptors must survive the exact submission fence.

### Invalidation before publication or reuse

- Missing/skipped/failed final postprocess, fallback/debug shader substitution,
  bad sources/constants/layout, zero geometry or empty viewport/scissor: no arm.
- A second matching tonemap/filter or a new competing scene after arming makes
  the narrow sequence ambiguous: invalidate for the epoch; do not choose whichever
  output appeared last or rearm after already emitting once.
- Any prior sprite, font, solid-overlay or unclassified write to final color
  before the selected sprite: reject. A first sprite on another target cannot
  silently become the selected city HUD boundary later without a new qualified
  scene epoch. Never fall back to the font checkpoint.
- Target resource replacement, changed RTV/subresource/format, resize/render-scale/
  MSAA or viewport/valid-rect change, device reset, resource generation mismatch,
  or lost source resolve lineage: invalidate. Same EDRAM base/size is insufficient.
- Clear, copy, resolve, reinterpretation or overlapping EDRAM write that changes
  the armed content invalidates it. A read-only resolve may preserve content only
  when its alias and source/destination effects are explicitly established.
  `ResolveEdramAliasing` at `native_renderer.cpp:2704–2767` can reinterpret or clear;
  `BindRenderTargets:2867–2873` calls it on rebind and advances `last_write` even
  for bind bookkeeping. Thus **`last_write` is not an actual-draw-issued stamp**.
  Inspect alias effects and update a boundary-specific content/version record.
- New frame/swap/scene, pause/loading/menu/alternate path, command recording abort,
  dropped/missing submission or unavailable resource provenance: clear state.
  Use host-frame and swap fields separately; neither inferred draw count nor GPU
  slot index is a frame identity. Camera cut also invalidates associated temporal
  history even if the new frame later qualifies a fresh color boundary.
- After emission, prevent duplicate publication. Before later consumption, reject
  stale mutable content; use the preserved snapshot/version instead. Retain
  resources until the slot/queue fence, and do not let a reused COM address or
  recycled guest surface identify a previous lifetime.

There is no evidence in the raw checkpoints of the specific resource equality,
source lineage, blend/depth state, numeric matrix, or coverage guard at swap1908.
These conditions must be checked in the runtime observer using state already
available to the renderer. They need not be guessed or discovered from asset
names, and failure should leave the boundary unqualified for that frame.

## Integration limits and minimal evidence to retain

`port/src/graphics/temporal_frame.h:79–82,94` separately exposes `ObserveScene`
and `ObserveHudBoundary`; setting only the latter does not replace a stale scene
color. Publish the qualified postprocessed color/snapshot while preserving the
independently validated current scene depth/motion/camera association. The sprite's
depth attachment is not proof of that association. This is **post-tonemap,
post-spatial-filter LDR scene color**; it does not prove an HDR pre-tonemap input
or a complete DLSS contract. `inspection.json:8–9` reports zero valid motion
pixels, and log2333 reports object motion/jitter unverified and DLSS Off.

The current pass-ID hook at `native_renderer.cpp:8368–8371` depends on a verified
`kPassRenderHud`; `game_profile.h:303` still has `kPassHud=-1`. Do not fill that
global constant from this single pair/scene. A local qualified observer is the
appropriate next step, consistent with `surface_sequence.h` explicitly treating
surface/hash observations as nonsemantic metadata.

For each first qualification, retain a compact trace: host frame/guest swap/scene
epoch, tonemap and filter issued serials, first sprite serial, actual source and
destination lifetime/subresource identities, alias/copy provenance, valid rect,
blend/depth/color-mask/specialization, sprite matrix rows/geometry bounds, and
rejection/one-shot status. Confirm the copied boundary image remains HUD-free
and the later final image contains HUD. Expand scope only with equivalent
evidence for another path. This report provides no universal HUD classifier.
