# M1: source-qualified single-instance vehicle draws

## Evidence and boundary

The rotated runtime log graphics_vehicle_callers_reserved_20261106/game.1.log
records DF1EB50388CC2563/F2BB7B6551646033 under a live 8230F8E0/820211C0
owner, returning through 8261CFB0/82310280. Generated .67.cpp shows that
8231027C calls 8261CED0 synchronously for owner mode byte +68 ==1. It copies
owner+128 into caller SP+256, passes counts SP+120 containing1, a matrix-pointer
array SP+144 pointing to SP+256, group count1 and four rows. The model/part
selection is the same owner+88 -> +12 -> +12, model+12 + slot*4 cursor already
audited for 823102B0. Constructor/cleanup and dispatch lifetime remain unchanged.

8261CED0 resolves its packet descriptor through 82614A08, then immediately
calls 8261B050. The latter uploads the copied four rows verbatim to VS c100,
sets c4 to {descriptor vertex count,0,0,0}, and draws that count from vertex0.
Do not confuse the resolved descriptor with root packet+12. Compare the actual
draw count with the uploaded c4 instead of guessing that descriptor's address.

DF1 main fetches vertex/index data, transforms local XYZ/W1 through c100+a0,
and projects with camera c232..235. For a nonindexed [0,count) draw, exact
integer c4.x==count, c4.yzw==0, count<=65536 ensures the source expression
trunc((vertexID+.5)*rcp(c4.x)) remains0 (conservative floating envelope checked
in the pure validator). Actual c100..103 must bit-match the frozen live owner
world. Require an affine finite owner matrix, exact uploaded banks, source
hash and all existing actual VB/fetch/version/topology/order certificates.
The command must positively classify shader-driven vertex fetch: the static-IA
signature intentionally ignores inactive SRV fields and cannot certify DF1.
The explicit live-capture diagnostics reserve two eight-sample DF1 buckets
(14 buckets total); they do not change identity or production readiness.

## Hook and identity

Add paired manifest midasm hooks immediately before call 8231027C and after its
return 82310280. They retain and restore the existing packet/part scope and
never modify guest registers, memory, call arguments or control flow. The
entry validates stack-pointer relationships, counts/groups/rows, mode and
copied matrix before granting distinct part origin8. Unreadable data, nested
scope misuse or allocation failure disables qualification without fatal errors.
Provide no-op hook implementations in non-native builds.

The worker's new pure helper accepts only DF1 + origin8, the exact range and
palette certificate, live token/generation and base dispatch. It produces an
instance key separated from direct rigid origins4/5 and common domains6/7.
Projection is world-to-clip camera232, while world changes are geometry inputs,
not identity. Do not use this per-object camera as the global scene camera;
the later audited 2C45 camera and exact depth seal remain mandatory.

Queued EA88 and DF1 flushes are outside this first route. Their ownership may
be observed but cannot acquire identity from an instance slot or ordinal.
Generic batching would require exact source append/flush slot certificates and
ordered live member tokens, which are not part of this change.

## Verification

CPU RED/GREEN: malformed stack pointers, mode/count/rows, copied matrix,
model/cursor/packet, wrong origin/source, range/count rounding boundary,
nonfinite inputs and actual palette mismatch. Verify moving owner matrix keeps
the identity while changed generation/part/source/bindings cannot match.
Inspect original PS owner silhouettes, production validity and camera-subtracted
rigid motion on real moving vehicles. Repack actually observed CNPS variants,
repeat cold validation and three benchmark rounds under the final exe hash.

## Executed validation and class correction

Build a59fdc500bdfcd52d819b633aff5e2a384e50211a280a08e31bbac492fbd68b0
passed708 CPU cases and CTest6/6. The source reviewer required the positive
dynamic_vertex_fetch gate; it is included in this build. Actual s2028 captures
show part8, world snapshot, shader_fetch=true and fetch_valid=true for DF1.
31 F2BB draws match, with1,747 valid visible pixels. Their actual silhouettes
are lamp posts, not the visible car. They therefore establish this source
route, not vehicle coverage. The remaining6 ADAF DF1 draws and queued EA88 stay
unverified. See the subsequent visible-draw attribution design before any
broader eligibility is added.

The linear gameplay full-frame NGX comparator reports95.841% within2/255 and
remains a failed full-frame check. The amplified difference image identifies
the separately rendered HUD/minimap/crosshair. A fixed central scene rectangle
[330,130,900,500] is99.899% within2/255, mean error .117/255; this is bounded
scene-composition evidence and does not override the full-frame result.
