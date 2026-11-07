# M1: close the observed vehicle coverage gap

## Observed failure

The s5849 draw capture in graphics_rigid_pixels_gameplay_20261106 contains a
large red car in the foreground. Its pixels are invalid in the production MV
mask. The diagnostic rigid-family replay has zero visible pixels, although32
C5E8/CD5A draws have verified instance correspondence. Those draw counts cannot
be promoted to evidence for that car. The car currently rejects temporal history;
it has not demonstrated valid per-object MVs.

## Source evidence and proposed scope

Two effect-capable rigid VS families remain unqualified in that capture:
8E5232F5E5E6760E and1023A9B52C1407C1. Their generated main computes position
directly from the vertex XYZ and literal W1 against c233..236. All four world
rows are c0..3; TEXCOORD1 exports XYZ/W after an explicit xzyw permutation.
Burn/freeze/transparency inputs affect shading varyings, not SV_Position. Final
XY receives only the already-audited screen-transform/half-pixel fixups.

Do not infer vehicle class or identity from these shader names. First capture
the existing bounded frontend PPC caller/owner diagnostics with DLSS disabled,
then trace the real render scope and asset-part source in generated PPC. Admit
only an observed live instance/generation and a validated part/binding route.
If a new guest hook is necessary, document its source and add it via the manifest.
No ordinal, nearest-position, palette-address or shader-only identity fallback.

After the route is established, extend the pure dependency classifier with
world0/clip233 for these two source hashes. Preserve their separate source
domains and exact finite-word checks. Correspondence still requires the current
VB/IB/layout/content/fetch, topology and vertex-order certificates.

The observed PS pairs are 8E52 with A828328313FF36B9/1A6669D4867145BC/
FC11F956CA7B42A2, and1023 with ADAF9F96B596F6EF/3A7A1A60DE39C81F/
F2BB7B6551646033. Their alpha is variable. Extend only exact audited pairs in
the original-PS ownership specialization, retaining its structural main/tail
checks and final-alpha1/mixed protocol. Any new preprocessor initializer must
be pinned explicitly, never accepted as general arbitrary main control flow.
Prewarm derived shaders before gameplay and repack observed CNPS descriptions.

The initial frontend probe exhausted its256-sample global log budget on32
earlier families before these two appeared. Reserve at most16 additional samples
(eight per target family, total bound272) for these two already-identified
unqualified sources. This changes diagnostics only, not lifetime tracking or
motion eligibility; it avoids increasing logging for all shaders.

This per-pixel proof does not make an entire vehicle triangle opaque for DXR.
Keep M2's whole-draw alpha certificate restricted to its separately audited
literal-alpha body shader. Unknown/partial vehicle layers continue to reject
history; they must not inherit the camera vector of the surface behind them.

## Acceptance

Runtime owner observations (graphics_vehicle_callers_reserved_20261106) put both
VS families in the existing 8230F8E0 / 820211C0 lifetime scope. 1023 submits
through the already audited packet caller 823102B0. 8E52 submits through
82613100, returning to 8230FFB8. Generated default/superman_returns_recomp.67.cpp
shows that second route iterates an owner+224 material array and the aligned
packet list derived from the selected part. It does not use the existing
8261CD90 part scope. First test the existing exact owner-world/camera numerical
certificate; do not infer a stable part from that loop's draw ordinal. Reserve
separate bounded diagnostic buckets for these two sources (12 buckets total,
eight samples each). This diagnostic expansion grants no production identity.

The expanded dependency and six actual generated PS corpus cases passed all
703 CPU tests after the RED run. Runtime pixel coverage remains the next gate.

Run RED/GREEN CPU cases for new matrix ranges, both exact source-pair sets and
their real generated PS bodies, including negative source/alpha/identity cases.
Inspect the original production mask plus the diagnostic rigid-only mask on a
visible moving car. Subtract the same-frame frozen camera reference to separate
object motion from camera motion; retain raw buffers and presented images.
Then repeat three benchmark rounds for affected D3D12 configurations, outside
all diagnostic readbacks. Keep prior measurements under their original hashes.

## First pixel result and next diagnostic

Build e84aed8d, capture s1995: both new source pairs now match actual owner
parts, but their 3,792 rigid-only valid pixels belong to static street props
(camera residual below .005 pixels). The visible cars remain unproven. Their
image region contains pixel-owner IDs 1..28 absent from the retained scene draw
manifest, whereas the first retained owner is 29. Record at most the first32
owner IDs in an explicitly requested live capture, including source hashes and
existing capture rejection gates, to distinguish another surface epoch from
an unsupported car route. This bounded diagnostic must not grant identity or
change the production mask. The gamma-enabled capture is unsuitable for the
linear-only NGX pixel comparator; retain that negative result as such.

## Confirmed order bug and correction

Build ef34431d, the early-owner diagnostics identify the background IDs1..28 as the already
audited 4F17209D364D5B51/559F422BEF5E5714 and 18B4F69400B7F0AF/220353EB37491B13
routes. Every capture gate passed except camera=false / scene_depth=false.
The 2C45 scene-camera draw occurs later: UploadConstants publishes the current
scene depth and camera only then. Rejecting early post-VS draws on that later
publication loses early scene draws even though their own current object-to-clip transform
is already available. This is independent of the additional effect coverage. The later pixel result
shows these first IDs are background, not sufficient identification of cars.

Capture eligible geometry provisionally before the scene camera is published,
retaining the actual color/depth resource pair as before. Once a camera is
published, reject draws on another depth. At scene seal require the observed
current-frame camera and exact selected depth equality before any valid motion
correspondence or readiness. Missing camera, another surface epoch, camera cut,
resource change and identity ambiguity still fail closed. No previous-frame
camera is substituted and no new object identity fallback is added. Add a CPU
regression for cars-before-camera, wrong-depth and never-published sequences,
then inspect the real pixel mask and camera-subtracted vehicle motion.

The original color-write accounting must use the same provisional-depth gate
as the post-VS capture. Otherwise the early captured draw would have serial0
and correctly fail ObjectMotionFrame as uncaptured. The integration regression
checks both early/late serials followed by exact scene seal, and a wrong-depth
seal remains a camera rejection. The initial order RED had704 tests /1 failed
check; the extended GREEN has705 tests.

## Remaining instanced vehicle route

After the order correction, s2058 has200,064 valid background-owner pixels for
4F17/18B4, but no per-instance proof for those draws (owner token0); they must
not be called moving-car vectors. The cars still have no rigid attribution.
Observed unsupported instanced pairs are DF1EB50388CC2563 with
ADAF9F96B596F6EF/F2BB7B6551646033 and EA88A5BB6930F942 with A828328313FF36B9.
The PS bodies are already structurally audited; allow these exact pairs only
for original pixel ownership, retaining unknown geometry validity0. This does
not admit their matrix palette or grant moving-instance identity. Record the
per-pixel silhouette to associate the visible cars with actual shader draws.

The rotated game.1.log already contains DF1 caller and owner observations.
Reuse that evidence; no extra probe budget or arming is required. The source
trace and the separately documented single-instance design establish the next
restricted route. Palette slots and draw ordinals alone remain forbidden.
