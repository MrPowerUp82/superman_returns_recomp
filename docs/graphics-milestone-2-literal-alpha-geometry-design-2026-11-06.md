# M2: opaque geometry from an audited literal-alpha export

The original Superman body uses VS C687EB65D635C5D7 and, for some parts,
PS3EF6A4A3C740ADB1. Its audited generated main writes oC0.w=1.0 after its
lighting arithmetic. The only subsequent alpha modifiers are the known soft
particle, alpha test/coverage and color-output normalization tail. Gameplay
ownership readbacks already show this body's original pixels have alpha1.

The current BLAS gate rejects every blend state except ONE/ZERO, excluding
these otherwise opaque body parts. M1's per-pixel certificate cannot make a
whole BLAS triangle opaque: E83A and E6C7 retain variable alpha and stay outside
this extension. Do not generalize the certificate to those materials.

Freeze a whole-draw certificate in MotionRaster from the actual shared
constants uploaded for that draw. It requires the exact C687/3EF6 pair,
original audited coverage variant bound, empty custom shader directory,
single-sample depth-writing coverage, all RGB channels written, RGB blend
SRC_ALPHA/ADD/INV_SRC_ALPHA, spec0 or only the audited normal-decode bits0/6
(therefore no soft particle/test/coverage modifier) and color output scale1.
Bits0/6 only change normal decoding before the unconditional alpha1 export.
The initial runtime attempt admitted zero body draws: DEC3N declarations set
bit6. vertex_layout.cpp, render_packet.cpp and shader_common.h (the two normal
decode helpers) establish its meaning; unknown bits must still be rejected.
Preserve all existing opaque eligibility.

Store the PS hash and a false-by-default certificate with each raster. The
certificate is reset when selecting a pipeline and set from the actual spec
and normalization values when uploading shared constants. BuildVisibleRayScene
may admit a blended span only with this certificate. Report certified draw and
vertex counts in the periodic DXR build message, so a generic character/cape
flag is never mistaken for proof of body inclusion. Vulkan remains unaffected.

Validation: a CPU regression first exercises the current rejection and then
positive/negative boundaries (unknown/variable-alpha PS, custom shader,
nonzero spec, partial RGB, no owner/depth and normalization). The existing GPU
fixture validates character BLAS masks independently. Runtime RT gameplay must
show positive body certificate counts and successful fenced execution. Run
three fresh DXR benchmarks after rebuilding; keep previous evidence under its
original executable hash. No change to alpha/discard in the original renderer,
no new fatal, and no ray opacity inference from draw names or alpha samples.

## Executed regression and runtime evidence

The original predicate failed both positive CPU checks. After extension,703
tests passed; the DEC3N-specific positive checks then reproduced the too-narrow
bit0-only rule (two failures) and passed with the audited bits0/6 mask. Unknown
bit7 remains rejected. The hardware fixture passed separate character shadow
inclusion/exclusion without removing character reflections.

In graphics_literal_alpha_body_final_20261106, the current-frame CSV s2143
records five3EF6 body draws with ray_opaque=1 and19,446 vertices total. The
threeE83A parts retain ray_opaque=0. Repeated renderer messages and positive
sun/reflection counters completed behind their fences. A732-frame gameplay
route had zero runtime PSOs, shader compiles and misses. SHA256 of this build:
244ea9da75af48a0112a909dfebb9b5ad7973d812e16cddb15c3fe17428a9945.
The numerical certificate does not grant variable-alpha NPCs/body parts whole
triangle opacity. The per-pixel M1 output remains unchanged for those draws.
