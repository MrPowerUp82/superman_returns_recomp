# Scene-only DLSS resolution and native HUD: bounded source audit

2026-10-25. Documentation only; no implementation, build, gameplay, or DLSS
enablement. User-reported current HEAD: `54e573b`. Findings below describe the
working-tree source inspected during this task; concurrent edits can move line
numbers, so function names are also supplied. Existing CPU work and the parent's
depth investigation are outside this audit.

## Decision supported by the inspected source

Introduce an independent, explicitly sized scene resource domain; retain the
existing output domain for tone/filter destinations and HUD. **Do not implement
DLSS by lowering `render_scale_` or changing a surface's scale when HUD begins.**

The approved design requires upscale before HUD and UI at output resolution:
`docs/superpowers/specs/2026-10-23-native-renderer-parity-design.md:65–67,89`.
The existing evidence identifies a city-path **post-tonemap, post-filter LDR**
boundary immediately before the first qualified sprite. It does not identify a
fully qualified pre-tonemap HDR NGX input:
`docs/pre-hud-boundary-evidence-2026-10-25.md`, sections “Decision”,
“One qualified SceneColorBoundary observer”, and “Integration limits”.

## Verified allocation change points

All renderer references in this table are to
`port/src/native_renderer/native_renderer.cpp`.

| Source | Observed behavior and consequence |
| --- | --- |
| `GetSurface:2139–2148` | Guest geometry is decoded, enlarged for tiling and minimum attachment dimensions. Guest dimensions and physical host dimensions must remain distinct. |
| `GetSurface:2162–2166` | The surface cache key includes EDRAM base, depth/format, samples and guest size. It has no independent scene/output resolution domain or host extent; a hit returns immediately, before scale selection. Changing a global or current-pass scale cannot resize that cached allocation. |
| `GetSurface:2180–2189` | Tiny reduction targets stay 1:1. Shadow-specific branches use separate settings, including an existing square-target heuristic. These branches are existing behavior, not authoritative scene/HUD classification. |
| `GetSurface:2192–2202` | Remaining surfaces take `render_scale_`; non-output-sized targets may additionally receive `full_scene_resolution_`, and small targets may receive `bloom_scale_`. This policy also reaches postprocess surfaces. It does not isolate opaque 3D/HDR/depth. |
| `GetSurface:2204–2227` | Allocation dimensions are `HostPx(width, scale)` and `HostPx(height, scale)`. Color is render-target capable; depth uses R24G8/D24S8. These allocations are not automatically NGX output resources. |
| `Initialize:1192,1241–1242` | `render_scale_` is clamped to 1–4 and also determines `output_width_`/`output_height_`. It is the existing global supersampling/output control, not an independent DLSS input-size control. |
| `HostPx:1068–1078` | One scalar and rounded pixel edges govern both axes. Exact queried NGX width/height need an explicit extent contract; a single rounded scalar is not a general representation of two independently specified dimensions. |

Supporting representation:
`port/src/native_renderer/native_renderer.h:370–382` keeps `HostSurface` width
and height in guest pixels and stores a scalar host/guest scale.
`ResolveTexture` does likewise at `native_renderer.h:404–409`.

Counterexample: one guest surface first allocated as scene cannot later become
a native HUD allocation merely by setting a different scale before the HUD
draw. The existing cache hit returns the earlier physical resource. A new
resolution domain needs explicit allocation identity, lifecycle and content
lineage, including EDRAM aliases; it cannot be a late viewport-only switch.

## Verified resolve change points

References again use `port/src/native_renderer/native_renderer.cpp`.

| Source | Observed behavior and consequence |
| --- | --- |
| `ExecResolve:6371–6375` | Resolve obtains the source through `GetSurface`; it must find the same scene/output allocation that was actually rendered. A change limited to draw binding is insufficient. |
| `ExecResolve:6430–6432` | Destination logical width/height come from guest texture fetch words. Preserve these logical dimensions and fetch semantics. |
| `ExecResolve:6446–6452` | The legacy resolve key omits scale. The optional complete identity includes source scalar scale, dimensions and other properties, but no independent host X/Y extent or resolution domain. Both identity paths need consideration. |
| `ExecResolve:6457–6484` | Reallocation compares destination scale to source scale; `dst.scale = src->scale`. Physical destination size is `HostPx(dest_w, dst.scale)` by `HostPx(dest_h, dst.scale)`, with one sample. A scene resolve therefore follows the scene source's resolution. |
| `ExecResolve:6546–6578` | Source rectangle and destination placement are decoded/clamped in guest coordinates, then **both** source edges and destination offsets are multiplied by the source scale. This assumes source and destination share one host/guest mapping. |
| `ExecResolve:6593–6608` | Depth uses `CopyDepthRegion`; multisample color uses an average resolve; single-sample color uses `CopyTextureRegion`. The single-sample copy is not a spatial upscale. Merely allocating a native-size destination leaves an incorrect copied region rather than creating native-resolution scene content. |
| `ExecResolve:6622–6624` | Resolved inputs end in combined pixel/nonpixel shader-read state. A future NGX call must satisfy its adapter's explicit state contract rather than assuming the existing resolve state is sufficient. |
| `ExecResolve:6653–6665,6670–6689` | Resolve-triggered color/depth clears use the same scale/rectangle machinery and color alias iteration. They belong to the resource-domain change, not only to the draw path. No depth fix is proposed here. |

The immediate implementation seam is consequently **domain-aware allocation
and identity plus same-domain resolves**, followed by a distinct, explicit
cross-resolution operation. Do not repurpose an ordinary guest resolve as NGX
output publication or silently relabel `resolve_latest_` content.

## Minimal staged proposal; not authorization to enable gameplay DLSS

1. **Represent and observe, without changing rendering.** Preserve output extent
   separately from exact queried scene input extent. Record guest dimensions,
   actual host dimensions, samples, format, resolution epoch/domain, resource
   lifetime and actual content producer. Establish which HDR/color/depth
   resources and all contributing 3D draws belong together. Do not select them
   solely by format, size, a pair hash, or first touch.
2. **Make allocation and resolves domain-safe behind a disabled experiment.**
   Change `GetSurface` identity/allocation and the `ExecResolve` identity,
   reallocation, edge/offset and clear sites listed above together. Preserve
   guest fetch sizes; explicit host dimensions/mappings must survive resolves.
   Keep tone/filter outputs and HUD in the output domain. Include matching
   viewport/scissor, shared clip fixups, texture-size metadata and cache
   invalidation in the implementation gate; this abbreviated audit does not
   supply complete patches or re-audit those paths. Allocation decisions must
   precede the first affected clear/draw, not wait for the later HUD boundary.
3. **Qualify an explicit upscale seam.** A candidate layout is scene color,
   depth and motion at the internal extent → private NGX output at output extent
   → existing native tone/filter destinations → native HUD. For that layout,
   the NGX operation must precede consumption of the qualified HDR scene by
   tonemap, with only its proven scene input redirected. The existing pre-HUD
   LDR boundary is useful for sequence/contamination validation, but does not
   prove this earlier HDR seam. An after-filter LDR NGX alternative would need
   its own lower-resolution post chain and native HUD handoff; it is not a
   drop-in implementation of this proposed HDR layout.
4. **Validate before application.** Keep original Off behavior intact. Require
   exact input/output dimensions, scene/depth/motion association, applied scene
   jitter, exposure/color-space proof, retained resources and submission fences,
   state restoration, and one qualified HUD handoff. Preserve bloom/luminance
   inputs and original post constants unless separately audited; destination
   resizing alone does not justify rewriting source texture sizes or texel
   offsets. A late qualification failure after rendering at reduced resolution
   needs a deliberate raster upscale fallback; it cannot retroactively restore
   a native-resolution 3D frame. Keep requested, resolution-plan and applied
   DLSS states distinct.

## Known limits and counterexamples

- **The nominal scene size is not settled by a renderer comment.**
  `native_renderer.cpp:2193–2200` mentions inherited 1024×576 behavior, whereas
  `port/src/native_renderer/game_profile.h:277–280` explicitly calls that Conan
  behavior and marks SR's `kSceneWidth = 1280` unconfirmed. Neither is a substitute
  for actual bound source/resource extents. The frozen 1280×720 post/HUD data
  prove that captured output layout, not every earlier HDR allocation.
- **No universal scene/pass classifier is available from these findings.**
  `game_profile.h:298–303` leaves the shadow/opaque/upscale/HUD roles at −1.
  Scaling all non-output-sized targets also scales bloom/other offscreen work;
  scaling all HDR-format targets does not establish ownership or scene closure.
- **A shared guest destination does not separate scene from HUD.** The city
  boundary contract requires actual resource/version continuity and consumption
  before the first qualified sprite writes it. Retaining the pointer until
  present would observe subsequently modified HUD-containing contents.
- **Source texture grids are a separate obligation.** Resolves preserve logical
  guest fetch dimensions while changing physical dimensions. Any future binding
  override must preserve that distinction, original swizzles/gamma, and post
  offsets/constants. Exact per-axis metadata and all read paths remain to be
  audited for arbitrary queried internal dimensions; this report does not claim
  the current scalar metadata is sufficient.

**Unknown / not completed:** the fully qualified HDR scene-input seam and final
3D contributor set; complete viewport/scissor and sampling-metadata change map;
safe cross-domain alias semantics; camera/object/deformation motion and actual
jitter; depth correctness under the parent's current investigation; numerical
GPU error bounds and gameplay NGX execution. No timing/performance or DLSS
readiness conclusion follows from this source audit.
