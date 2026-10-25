# Original reflection inputs and next D3D12 stage

Date: 2026-10-25. Bounded static audit of the local translated shader pair and
the current extraction/RT consumer. No code edits, build, game run, or commits.
This is an implementation contract, not evidence that reflections execute.

## Scope and evidence identity

- **PS** = `artifacts/shaders/hlsl/8A6DD1D52752FAD6.ps.hlsl`.
  SHA256 `138075674039DA5CA8F00058ACF8051E6C702650B81FAD793CAB3F3C3FAD7BF8`.
- **VS** = `artifacts/shaders/hlsl/2C45BFFE76D77398.vs.hlsl`.
  SHA256 `0CC94A6E0D3822AEC042A55421A27A45AE6E0B9B384C4182D54A26ADCD1DBB81`.
- Line references below are one-based in these files or explicitly named C++/HLSL
  files. The renderer is being edited concurrently by the parent; function names
  and anchors accompany its line references. The shader hashes above pin the
  algebra. Coverage is restricted to this pair; names are not actor identities.
- Approved intent: `docs/superpowers/specs/2026-10-23-native-renderer-parity-design.md`,
  sections “Dados de geometria e materiais” and “RT reflections”: resident/offscreen
  geometry, real materials, original reflection fallback, no duplicate environment.

**Result:** the original environment sample has a precise replacement boundary.
Receiver position, eye, shading normal, reflection direction, material weights,
and specular shape can be recovered. No native PBR roughness or hit-material
payload exists in the inspected RT integration. The next useful milestone is
unbiased geometry plus per-triangle attributes and an audited, lit hit evaluator.

## Receiver coordinate contract

| Quantity | Exact source evidence | Consumer expression |
| --- | --- | --- |
| World position `P` | VS 934–946 project `(r1.y,r1.x,r1.z)`; 1034–1035 export it | `iTexCoord2.xyz`; no extra permutation |
| Eye `C` | VS constants 592–593; 993–999 | `(VS_c5.w, VS_c6.w, VS_c7.w)` |
| Surface-to-eye `V` | VS 993–999 export `C-P` in TC4; PS 1022–1036 normalize it | `r11.xyz = normalize(iTexCoord4.xyz)` before later overwrites |
| Transformed base normal `Ng` | VS fetch 48 at 893; exports 1028–1030, 1043–1047; PS 994, 1044–1050 | `normalize(iTexCoord0.xyz)` |
| Transformed tangent `T` | VS fetch 96 at 895; exports 989, 1020–1024; PS 1001, 1037–1043 | `normalize(iTexCoord1.xyz)` |
| UV0 and UV1 | VS fetches 80/81, 897–899; export TC3, 1039 | `iTexCoord3.xy` and `.zw` |
| Light varying `L` | VS 983, 1052–1065 | `iTexCoord5.xyz`; VS normalizes before interpolation |

Fetch semantics are `usage*16+index`: NORMAL=3 and TANGENT=6 (renderer
`UsageSemantic`, `port/src/native_renderer/native_renderer.cpp:905–914` and
vertex metadata assembly at 4334–4339). The VS rotates XZ components with the
same instance constants as position. TC0/TC1 are therefore world-space basis
vectors for this pair, not tangent-space texture normals. No tangent handedness
attribute is consumed by this PS; preserve its cross-product order.

The **ray origin is the reflecting surface `P`**, with a bounded geometric-normal
offset; `C` provides the incident view direction. `SV_Position` is raster space.
For a secondary hit at `Ph`, evaluate view-dependent hit shading toward the
reflection-ray origin (`normalize(rayOrigin-Ph)`), not the primary game camera.

## Normal and direction: resolved swizzles

The following is the ordinary finite/nonzero-input algebra of the original
shader. Production code should preserve its guarded reciprocal/rsqrt behavior
or fall back on invalid/degenerate inputs; these equations do not authorize
dropping those guards.

```hlsl
float2 uv = iTexCoord3.xy * UV_freq_offs.xy + UV_freq_offs.zw;
float2 q = nrm_map(uv).xy;
float3 materialRGB = mat_map(uv).rgb;
float3 noiseRGB = noise_map(P.zyx * Kwarp_scale_Nscale.y).rgb;
float s = Kwarp_scale_Nscale.z * gNormScale.x;
float u = s * (2*q.x - 1);
float v = s * (2*q.y - 1);
float h = rsqrt(1 + u*u + v*v);
float3 N = normalize(h * (Ng + u*T + v*cross(Ng,T))
                   + Kwarp_scale_Nscale.x * materialRGB.r * (2*noiseRGB-1));
float3 V = normalize(iTexCoord4.xyz);
float3 R = -V + 2*dot(V,N)*N; // reflect(-V,N), outgoing world XYZ
```

Evidence: PS 1097–1109 choose coordinates and samples; 1118–1188 decode normal
XY, normalize `(u,v,1)`, and form the cross-product; 1193–1205 combine tangent,
bitangent, and base normal; 1210–1229 add world noise and normalize. At **1229**,
`r13.xyz` is exactly the final `N` in canonical XYZ. At **1036**, `r11.xyz` is `V`
and stays unchanged through direction construction. At **1720**,
`r10.y=dot(-V,N)`; 1904–1924 compute **`r4.xyz=R` immediately before `cube()`**.
The separate `r10.x` uses `dot(N,-L)` for the direct-light reflection lobe.

Critical helper detail: PS 525–531 `cube()` saves the original direction in
`cubeMapData.cubeMapDirections[index]`, then returns `(0,0,0,index)`. PS 375–377
`tfetchCube()` samples that saved direction, indexed by `texCoord.z`. Thus
**`r6.xyz` at 1944 is not a world-space ray direction**, nor are its XY useful
reflection UVs. Save `r4.xyz` before PS 1929, or retrieve the saved cube direction
using the same index as the helper. There is one cube operation in this PS.

## Texture and material provenance

All samples below use the draw's real descriptor/sampler bindings, not guessed
assets. PS 707–737 declares their SharedConstants indices; PS 184–202 masks
descriptor metadata and applies the original piecewise gamma conversion.
`tfetch2D` (246–256), `tfetch3D` (357–359), and `tfetchCube` (375–377) consume
those bindings. Preserve resource generation, resolved subresource, swizzle,
descriptor metadata, and sampler behavior in any hit evaluator.

| Slot | Resource | Coordinates / consumed channels | Evidence |
| --- | --- | --- | --- |
| 0 | `col_map` 2D | transformed UV0; sample `.yxzw` assigned to `r10.xyzw` | PS 1386–1388; tint/multiplication 1397–1520 |
| 1 | `nrm_map` 2D | transformed UV0; XY decode as above | PS 1104–1105, 1118–1229 |
| 2 | `mat_map` 2D | transformed UV0; `.zxy` into `r7.xyw` | PS 1108–1109, 1140, 1238 |
| 3 | `env_map` cube | saved world direction `R`; RGB into `r12.yzw` | PS 1929–1944 |
| 4 | `noise_map` 3D | `P.zyx*Kwarp_scale_Nscale.y` for normal warp | PS 1097, 1106–1107, 1134–1229 |
| 4 | same noise | dirt branch: `P*(gKdirt_scale_inv.y*Kdirt_scale_inv.y)` | PS 1270–1284, 1291–1367 |
| 4 | same noise | additional conditional lighting noise; preserve branch | PS 2174–2439 |
| 5 | `ao_map` 2D | raw UV1=`iTexCoord3.zw`; `.xzyw`, alpha feeds AO | PS 1945–1946, 2090–2098, 2138–2143 |
| 15 | `samp_depth` 3D | original shadow coordinates from TC6 + atlas constants | PS 1548–1649 |

Renderer binding provenance: `native_renderer.cpp:4342–4358` reads each six-word
guest fetch, resolves it to a host SRV; 4437–4452 selects dimension-specific
heaps and sampler indices. The guest texture address alone is not a lifetime
or material identity. Snapshots must retain actual resources/views to the fence.

Let `m=mat_map(uv).rgb`. The four original blend weights are:

```hlsl
float4 W = float4(m.rgb, 1-saturate(m.r+m.g+m.b));
// Internally r7 = (W.b, W.r, W.a, W.g), not RGBA.
```

Only the residual alpha weight is clamped this way. RGB are not normalized to
sum one. This is a four-way parameter blend; it does not establish metalness,
glass, building ownership, actor class, or roughness in a specific texture channel.

| Original parameter | Register / expression | Constant index and evidence |
| --- | --- | --- |
| Ambient coefficient | `dot(W,KaS)` -> `r12.w`, later `r17.z` | PS c229; 666, 1741, 1769 |
| Specular strength | `dot(W,KsS)` -> `r13.x` | PS c230; 670, 1955 |
| Specular shape | `n=dot(W,sS_exp)` -> `r20.x` | PS c231; 703, 1962 |
| Environment strength | `kEnv=dot(W,Kenv)` -> `r22.x` | PS c5; 668, 1974 |
| Diffuse coefficients | `W.r*R_KdL + W.g*G_KdL + W.b*B_KdL + W.a*A_KdL` | PS c6/c1/c8/c11; 1728–1764 |
| Specular RGB | `W.r*R_sS_col_exp.rgb + W.g*G_sS_col_exp.rgb + W.b*B_sS_col_exp.rgb + W.a*A_sS_col_exp.rgb` | PS c7/c2/c9/c12; 1956–1991 |
| Environment tint/global strength | `gEnvMapTint.rgb * gKSEsD.y` | PS c29/c42; 682, 688, 1950 |

The dirt multiplier `d` is `r9.x` just before PS 1996. It starts at 1 (1217).
In the dirt branch it becomes `1 - scale.x*abs(scale.z-saturate(dot(freq,noise)))`,
where `scale=gKdirt_scale_inv*Kdirt_scale_inv`, `freq=gdirt_freq_sel*dirt_freq_sel`,
and noise is the RGBA sample at 1284 (1270, 1291, 1323–1365). It is **not clamped**
at 1365. Dirt also modifies diffuse tint through 1299–1375; do not replace that
branch with only this scalar. Original specular strength later includes `d`
and `gKSEsD.x` (2096, 2103).

### Roughness is unresolved; the original specular shape is resolved

The name `sS_exp` does not imply a `pow` exponent here. Define
`c=saturate(dot(V,2*N*dot(N,L)-L))` (PS 2006–2025). PS 2032–2059 computes:

```text
shape = c / (n*(1-c)+c)
direct specular lobe multiplier = shape * (1-F)
F = r15.x selected at PS 1876–1877
  = max(r1.z as read at 1862, gKSEsD.z*r0.x as read at 1855)
```

The source has guarded reciprocals and additional light/ramp/color factors
(2064–2109); `F` is an original modulation, not an established PBR Fresnel term.
The environment sample uses implicit texture sampling: no explicit roughness
LOD or roughness-to-ray-cone conversion appears at 1929–1944. There is no
evidence-backed unique conversion from `n` to GGX perceptual roughness. Keep
the rational parameter and original lobe first. Any later BRDF/cone/LOD mapping
is an explicit approximation requiring image validation, not a recovered fact.

## Safe integration boundary and avoiding duplicate environment

At PS **1944**:

```hlsl
r12.yzw = tfetchCube(env_map_TextureCubeDescriptorIndex,
                    env_map_SamplerDescriptorIndex, r6.xyz, cubeMapData).xyz;
```

Immediately after this statement, replace this RGB sample with incident, lit
hit radiance **in the same sample domain**, blended with the original only by
coverage/confidence. Preserve the statement for fallback. Save `R` before
1929; at this point `N=r13.xyz`, `V=r11.xyz`, and `P=iTexCoord2.xyz` are available.
Off/miss/unsupported nearest hit/invalid input must keep the original sample.

```hlsl
float3 Eoriginal = r12.yzw; // RGB, already through original gamma helper
// LitHit must return outgoing radiance toward ray origin, not base color.
// confidence=0 for misses, unsupported nearest hits, or stale/incomplete data.
r12.yzw = lerp(Eoriginal, litHit.radianceRGB, litHit.confidence);
```

PS **1950, 1974, 1996–2032** gives the environment term, reordered to RGB:

```text
Eweighted = E.rgb * gEnvMapTint.rgb * gKSEsD.y * dot(W,Kenv) * d
// r11.wzy is Eweighted RGB; r11.wyz is R,B,G for the later swizzled add.
```

PS 2109 adds it to direct specular in `r8.xzw`. PS 2114 also routes that sum
through `c`, and 2121–2172 routes it through the original diffuse/shadow/AO/ramp
and overbright composition. PS 2439 adds conditional light; 2491–2515 applies
the conditional distant/LOD mix; 2520 writes `oC0=r10.yxzw`. Consequently a
post-color additive reflection is not equivalent to replacing this sample.
Do not pre-apply the receiver's environment weight/tint to `litHit.radianceRGB`,
leave the original environment alongside it, or replace the whole specular sum.

The existing inline shadow patch is independent: `rt_shadow_shader.cpp:7–12,
131–136` inserts after PS 1649 and preserves the later fade/color logic. A
reflection specialization should compose with it using exact unique audited
anchors, pair/signature checks, independent enable flags, and its own cache
identity. Do not reuse shadow's accept-first-hit query for closest reflection.

## What the current scene actually carries

- `port/src/graphics/d3d12/shaders/post_vertex.hlsl:20–55` emits only `float3`
  world per expanded vertex. `post_vertex_capture.h:15–20` records only offset,
  reserved vertex count, VS, and an unproven/default character flag. There is no
  PS/material snapshot, UV, normal, or tangent payload.
- `ray_tracing_scene.h:13–24` accepts float3 triangle lists and optional constant
  linear albedo. `ray_tracing_scene.cpp:269–292` builds one opaque BLAS per input
  geometry, identity instance transform, `InstanceID=i`, mask 1/2 by character
  flag, and a constant float4 material per instance. These IDs are build-local.
- `rt_scene.hlsl:108–117` traces with a depth-reconstructed geometric normal and
  writes **unlit constant hit albedo**. That diagnostic is not the audited
  normal/material/lighting path and is not finished RT reflections.
- Snapshot discrepancy with the task briefing: at audit time
  `native_renderer.cpp:5761–5764` captures only casters outside diagnostic mode;
  `BuildRayShadowScene` at 6956–6974 rejects non-caster ranges, combines the
  arena into one geometry, and supplies empty material. It runs at the initial
  scene-target bind (2875–2877), then stops capture (6964). The independent
  diagnostic combines available world geometry into another empty-material
  BLAS (6673–6677). The briefing describes a newer world+caster TLAS; that may
  be parent work in progress. Either form still needs the material contract.
  Recheck these functions before implementing; do not overwrite parent changes.

### Geometry bias limits

`shadow_caster_geometry.h:15–18,27–38` and `post_vertex.hlsl:27–45` undo exported
depth bias, atlas transforms, and shadow projection. They do **not** undo the
normal/thickness displacement. Original caster evidence:

- `57A00D4EB3BF4C31.vs.hlsl:897` offsets the position by a decoded normal times
  `shadowMapCasterPass_Thickness_Bias.y`, before skin-palette dots at 902–912.
- `C7C0E381A3C9B79E.vs.hlsl:875–885` offsets by normalized normal times the same
  `.y`, before object transformation. Both add `.z` during depth export
  (1015/972); subtracting that scalar cannot undo `.y` displacement.

Use unbiased visible/world geometry for reflection intersections. Keep biased
casters as shadow-only provenance, with a separate reflection TLAS or distinct
instance masks/BLAS groups; one combined opaque geometry cannot mask individual
caster triangles. If displacement is reconstructed later, audit the pre-skin
position/normal/palette contract rather than subtracting a guessed world normal.
No shader or skinning family proves Superman/cape/actor ownership.

The current shadow `bias=.05`, max distance 10000
(`native_renderer.cpp:6992`) and origin-plus-TMin bias
(`rt_shadow_shader.cpp:49,73`) are not validated reflection settings. Establish
reflection offset from unbiased geometric normal/position error and actual
scene scale; keep shading `N` for direction. Avoid carrying shadow light-axis
bias, double offset, or a large TMin into thin/nearby reflection geometry.
No numeric safe reflection bias range is established by this static audit.

## Specific implementable next stage

1. **Capture one auditable hit family with complete provenance.** Start with
   VS2C45/PS8A6D for both receiver and supported lit targets. Extend the GS output
   with world position and the original TC0..TC6 components used by this PS:
   normal+density TC0, tangent+ramp TC1, UV0+UV1 TC3, TC4 distance/LOD data,
   TC5 light+seed, TC6 shadow coordinates. Retain TC4 XYZ for reference but
   recompute its view vector for secondary shading. Preserve per-draw PS hash,
   immutable PS/VS/shared constants, specialization/alpha/cull state, and real
   texture/view/sampler snapshots. Do not infer material from the VS alone.
2. **Make hit lookup deterministic.** Each reflection primitive maps to the
   expanded triangle's three attribute records and a draw-material record.
   For a single combined triangle-list geometry, `3*primitiveIndex` locates
   vertices, with a primitive-to-draw table or validated range lookup. If split
   BLASes are used, include instance and geometry indices plus local offsets.
   Obtain committed barycentrics `(b1,b2)` and interpolate attributes using
   `(1-b1-b2,b1,b2)`; normalize/evaluate afterward exactly as the PS does.
   Preserve cuts, degenerates, SO capacity/validity, generations, and fences.
   Reusing guest addresses or current draw constants is insufficient.
3. **Schedule a complete reflection scene before consuming it.** The present
   caster-before-opaque build cannot contain all later opaque draws. Capture
   the verified scene submissions first, build reflection TLAS, then replay
   the audited receiver shading/composition with correct depth/blending before
   post/HUD, or explicitly stage an equivalent two-pass contribution. A previous
   frame TLAS needs validated motion/lifecycle rules; it is not a substitute
   for scheduling. Keep offscreen residency as a separately measured coverage
   requirement; caster submission alone does not supply unbiased offscreen
   shading geometry or textures.
4. **Implement lit hit shading, then enable the replacement.** Factor/reuse the
   audited original PS equations in a callable hit evaluator with the target's
   own attributes/constants/textures. Preserve diffuse tint, actual game light
   terms, specular shape/strength, AO, emissions/conditional light, and original
   local environment fallback. Use the incoming secondary view vector. Disable
   further RT recursion for an initial one-bounce implementation. Raster
   `Sample`/`ddx`/`ddy`/alpha-to-coverage are not automatically valid at arbitrary
   ray hits: add an explicit footprint/LOD contract (ray differentials/cones or
   another validated method), use SampleLevel/SampleGrad as appropriate, and
   mark any fixed-LOD experiment provisional. Returning sampled albedo is not
   this stage's success criterion.
5. **Trace nearest eligible geometry and retain original fallback.** Reflection
   queries must complete closest-hit traversal. Keep unsupported nearest opaque
   hits as blockers with confidence zero; skipping them would reveal geometry
   behind. Exclude known displaced shadow proxies by provenance. For audited
   cutouts, remove force-opaque/opaque build treatment and evaluate alpha before
   committing candidates; otherwise report that coverage unsupported. Add
   counters for supported lit hits, misses, unsupported/invalid hits, displaced
   proxies excluded, drops, and missing residency. Bind descriptors/resources
   with slot-fenced lifetime and preserve the existing shadow namespace/state.
6. **Verify this narrow milestone before expanding families.** Off and confidence
   zero must reproduce original output; a fully lit supported nearest hit must
   replace only the environment sample. Inspect `P,N,V,R,W,kEnv,n`, primitive/draw
   ID, UVs, hit radiance and confidence. Compare textured lit targets, thin/contact
   surfaces, misses, unsupported blockers, streaming and camera/object motion.
   Expand to verified dynamic/cape/material families and resident offscreen
   geometry, then temporal rejection/denoise and measured cost, before calling
   the approved reflection scope complete. Menu state must follow actual shader
   execution/coverage, not a capability bit or this report.

## Verification and remaining unknowns

Read-only algebra check: a deterministic in-memory scalar/swizzle transcription
with 1,024 inputs (LCG seed `0x2c458a6d`) compared the original normal-building
order, direction, four-weight blend and environment RGB permutation against the
equations above. Maximum absolute differences: normal `5.56e-16`, direction `0`,
weight `2.23e-16`, environment `1.12e-16` in double precision. This checks the
algebra only; it does not validate GPU precision, texture content, shader binding,
or render appearance. Shader SHA256 values pin the inspected source.

Answered: exact receiver/view/direction/normal, UV and sample provenance,
four-way parameter blends, rational specular shape, environment replacement
boundary, missing hit payload, displaced-caster restriction, concrete next stage.
Unresolved: numeric runtime constants/textures for a particular draw, PBR roughness
mapping, safe reflection bias, actor ownership, resident offscreen completeness,
hit footprint/LOD policy and GPU/visual/performance validation. No build or game
execution was performed under this sidecar's scope.
