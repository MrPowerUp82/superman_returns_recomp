# Observed renderable lifetime and draw association

The existing loaded-image dump `logs/rt_menu_loaded_image_2410.bin` and the
project's `find_missing_funcs.py` PE parser locate these `.rdata` entries:

| Table candidate | Slot 0 | Slot 2 |
| --- | --- | --- |
| `820211C0` | `8231B608` | `8230F8E0` |
| `82021930` | `8231B720` | `823103C8` |

This is data evidence, corroborated by the following generated-source paths:

- `822E8BD0` (`superman_returns_recomp.69.cpp`) preserves incoming this in
  r31, writes `820211C0` to this+0 and initializes members/subobjects.
- `822F6F68` (`.60.cpp`) calls that constructor with the same this, then writes
  `82021930` and initializes this+256. These are a base/derived chain.
- `8231B608` (`.133.cpp`) calls `822F54D8`, conditionally freeing this through
  `8213BBC8` when the deleting flag requests it.
- `822F6FB0` (`.130.cpp`) cleans the derived member at +256 and calls
  `822F54D8`. `8231B720` (`.132.cpp`) includes that cleanup and conditional
  deletion. `822F54D8` (`.93.cpp`) resets the base vptr and cleans members.

The probe observes normal return from the base constructor and entry into
base cleanup, once per known chain. It increments a host observation generation
for every observed reconstruction, including address reuse; it does not use
an address, table, mesh hash or draw order as cross-frame identity alone.
Non-deleting cleanup does **not** establish whether storage was stack or heap.
Copy/pool paths outside these known functions remain unaudited.

The two slot-2 bodies preserve incoming this and use this+128 for a transform.
The optional probe brackets them with thread-local scopes, verifies actual
dispatch through slot 2, checks the observed lifetime and samples original
D3D draw shader captures. Unknown nested scopes hide the enclosing owner;
normal unwinding restores it. Observation failures disable the probe while
original guest calls continue. No generated source is edited.

This does not classify a renderable as Superman, a character or a rigid actor.
It does not prove one renderable equals one instance in an instanced draw,
or that deferred submissions preserve this association. Runtime diagnostics
must establish those points before any `kVerifiedInstance` history is supplied.
The probe never supplies that attestation or changes RT/DLSS feature status.

Gemini supplied bounded source reviews and a proposed lifetime registry.
The orchestrator checked constructor/cleanup call chains and strengthened the
registry tests to avoid dereferencing failed optional results. Raw briefs,
results and table words are retained in `logs/*owner*2610*` for this audit.

## First runtime observation (26 October)

`motion_instance_probe_2610` completed normally at09:28:50 with harness exit0,
including the pause/panel/return checks. At the end of the two-frame capture:
2710 constructors,1118 cleanups,0 unreadable constructor vptrs; all771748
observed render-scope entries had a live observed generation. The armed draw
window contained6016 original calls,2032 within a verified dispatch/lifetime
scope. These are frontend-window totals, not a GPU-frame coverage percentage.

Examples: C5E8551EFDE01162 had96/146 scoped draws; C7C0E381A3C9B79E
1523/2081. Conversely C20921D0DBBD2F1F had0/394 and caster57A00D4EB3BF4C31
0/4. A derived shader9A62E7C65051447A had only3/48. Thus the two renderable
classes do not cover all moving/deformed draws; no Character Shadows or
complete DLSS-motion claim follows. Multiple draws for the same live token
are expected but do not establish one-instance-per-draw or vertex correspondence.

The scene-jitter experiment also executed in this run; phase2 recorded999
static matches and actual pixel offsets(-.25,.16666669), previous(0,-.16666666).
It remained a candidate scene capture with DLSS Off.

A further opt-in diagnostic records the original draw entry LR and at most
eight validated PPC backchain return addresses, bounded to readable increasing
16-byte-aligned frames (maximum1MiB step) and the loaded guest text range.
The hook preserves LR/SP before calling the original. Stack addresses never
supply temporal identity. Its purpose is locating the missing C209/57A paths.

C209 caller trace refinement: motion_instance_callers_2610 captured actual
frontend chains via82612BE4 ->8261CDF8 ->82301068 ->82783AA8.
Generated82300328 (.111.cpp) contains82301068;82783948 (.68.cpp)
dispatches incoming object in r31 through vptr+8 immediately before82783AA8.
Existing image .rdata82021D18 has slot0=8231C460,slot2=82300328.
Constructor823000C8 (.113.cpp) calls822F5C98 (.126.cpp), which calls
822E8BD0 with the same this; cleanup823002C0 (.27.cpp)/deletingdtor8231C460
(.82.cpp) reach822F5D48 (.80.cpp), then822F54D8 on the same this.
Thus lifecycle observations already include this class; its render entry
was missing from the TLS probe. Added it to the single existing CPU-profile
wrapper (index9); no duplicate guest symbol or second lifecycle generation.
The second caller-trace harness failed its later pause-entry assertion and
closed normally09:32:38; accept source/capture diagnostics only, NOT a menu
regression approval. DLSS remainedOff and jitter was recorded as applied.

Gemini's bounded assembly-comment review identified r25=inputr3, pose+288,
outputpalette+256 and dispatcher slot2. Parent verified slot2/r3 assignment,
image entry and constructor/cleanup chain. Reject its stronger claim that
this MUST be a single actor or grouping occurs exclusively in dispatcher;
submesh/attachment/vertex correspondence still needs evidence. A match is
not a Superman class identification. One unrelated addi7448 match in825CC2A8
uses highword8207, not8202; it is NOT a write of82021D18.

Next transport freezes an ObservedRenderInvocation POD into WorkCmd on the
frontend, with generation validated under the registry mutex; worker logs
C209 provenance from the captured command, not live TLS. It never supplies
GeometryHistoryProof. Build/review/runtime of this transport still pending.

2026-10-26 owner transport verified: main build_owner_transport exit0,
native443tests/CTest3/3PASS; Gemini transport review bounded no-defect.
Runtime motion_instance_c209_scope_2610 exited0/menuPASS/normalclose09:37:37;
all372C209 frontend draws scoped/tracked. Runtime motion_owner_transport_2610
also exit0/menuPASS/normalclose09:42:10. Worker capture logs both actual
frames2070/2071 with frozen owner0273F890/generation36/render82300328/vtable82021D18;
207/207 armed frontend C209 calls tracked. Worker does not consult live TLS,
read guest this or grant kVerifiedInstance. Pixel coverage now being extended
to actual captured PS E6C740EAC16B70D4 (SHA256AAAA06C70906EE44C12E1ACE019B8E7C039917E6EAC48D5056E346B8982506D7)
paired only with C209; identical whole emitter signature/tail enforced by
existing structural validator. All original PS bytes survive inverse-patch test,
including discard/alpha tail. RED443tests10failedchecks->GREEN443tests0failedchecks.
Build_c209_pixel_coverage succeeded; runtime validation pending. This produces
actual fragment ownership only, never a cross-frame actor identity by itself.

2026-10-26 E6 coverage runtime motion_c209_pixel_coverage_2610 PASS/menu and
normalclose09:46:12. Both phases issued actual original PS MRT variants,
phase2 C209212/212position-eligible but0historymatches (correctly unknown).
Fenced R32_UINT owner texture had60/212 issued C209 IDs with77667 pixels;
report logs/c209_actual_pixel_coverage_2610.json. This is last-audited fragment
ownership, not global nearest writer or motion acceptance.
Packet probe motion_instance_packet_probe_2610 PASS/menu/normalclose09:54:46;
actual C209 uploaded bool0 masks included7 and1 (skinning enabled); worker
received actual packet pointers and finite float+bool input signatures.
CORRECTION: packet+16 is a header word, NOT the palette pointer. Source writes
palette at82609500's returned subblock+16, after querying the aligned descriptor
command block at+116. The earlier palette label was wrong (runtime values0/6).
Renamed to packet_word16; actual actor_palette read at this+256, source-confirmed.
No prior palette/identity claim accepted. Native tests now445GREEN after
removing redundant generic history test that could suggest owner input
records alone qualify kVerifiedInstance; CTest3/3PASS.
Gemini part-source investigation identified asset stream selectors/callsites;
parent verified returnsites82300F2C/82301068 and raw table provenance. At these
sites r28 is already an inner-loop counter, NOT the original model pointer;
use saved model at callerSP+208, verify model+64==r24/model+12==r21 and actual
ushortstream->descriptorarray mapping to incomingr3. Record source branch,
LODslice, base group(r20/2) or attachment group(r19/2), actual attachmentID,
stream-relative offset and selected submesh. No rawpointer/ordinal attestation.
Nested render scopes reset packet/part TLS and restore previous context on
unwind. Newpart-scope main build and boundedGemini review pending.

2026-10-26 source-part runtime motion_instance_part_route_2610 PASS/menu,
normalclose10:00:18. All32sampled first16/phase routes checked=true;9distinct
routes in eachphase,9common. Examplemodel02434700/sectionsBA873E94,
origin2,lod_slice166,attachmentgroup2/select3,streamoffset2000/submesh52;
othergroups/selectors remain separate. Actual actorpalette027423E0.
Report logs/part_route_observations_2610.json. Parent confirms asset-backed
selection fields, NOT observed draw ordinals. New pure ObservedSkinnedPartKey
includes owner,model/section namespace,base/attachment branch,LODslice,
section/attachment slot,actualattachment selector,stream-relative entry and
submesh index; host constructor generation remains separate. No packet,
palette or draw-order identity.448tests3RED->448GREEN; CTest3/3PASS.
Opt-in sr_native_debug_observed_skin_motion now integrates ONLY audited
C209/E6 + checkedpart route + live-generation frozen token + canonical
mesh/VB/IB/fetch versions + actualfloatCBV mirrorparity + actual uploadedbools
+ originalPScoverage MRT/depth EQUAL. Withinframeposition/projection duplicates
remain ambiguous, framegap/resize/dropped resources remain unqualified. It
supplies kVerifiedInstance for that source-qualified part only; no character
classification, fullscene coverage or DLSS readiness. CanonicalpostVS clips
pair actualsame vertex sequence; all1024floatwords+4actualboolwords included
conservatively in input ambiguity signature. Pose can changebetweenframes.
Diagnostic matched-only upperbound renamed candidate_matched_only_motion_*
(inspector supports both newname and oldstatic-only names); normalactual
motion_velocity/validity includes unknowninvalidators asbefore.
Two boundedGemini part-code reviews returned empty0tokens, rejected (no
independent approval inferred); prior successful source reviews and parent
checks documented. Build_observed_part_motion exit0; newruntime launched,
resultpending. Nativebench --ObservedSkinMotion requires --InstanceProbe.

2026-10-26 actual C209 part motion: build_observed_part_motion exit0,
runtime motion_observed_part_motion_2610 PASS/menu/normalclose10:05:29.
Phase2frame2155: C209205draws/205matches, overall1886draws3546960verts,
1212matches420unverified254missing,0ambiguity/conflict/drops. Actualfenced
wholemotion750659validpixels81.4517% (differentcamera; no comparison claim).
C209 last-auditedowner intersection143valid/149pixels is NOT uniqueattribution
because latercoplanarstaticwriters might qualify. Added third independent
TemporalMotion instance for family attribution: preserves ALL other geometry
aszero-validity invalidators in originalorder withsamefinaldepth/coverage.
Build_observed_skin_attribution exit0; runtime same-name PASS/menu/normalclose
10:11:56. GPU213observed-skin matches and actual234attributedvalidpixels,
allfinite/nonzero(19.18..23.71px/frame), unknowngeometrynotomitted. This proves
pixels for this family, not allcharacters or completeDLSS. Report in
logs/motion_observed_skin_attribution_2610/inspection.json.

Frozen postVS/depth replay on current actualcapture identified cape VS A5232A3184AA617C
(25121EQUALpixels, allinplayerROI) and exposedbody C687EB65D635C5D7
(5429pixels,5180playerROI). Inspectedmasks align cape/body respectively.
This is geometry/depth evidence, not a general actorsemanticclassification.
StandaloneRTX2060replay syntheticcontrols passed, noinputmodification. Outputs
logs/player_family_replay_2610/report.json/player_roi.json/family_*.png.
Gemini-orchestrate read-only batch .gemini-tasks/20261026-player-read succeeded
2/2 with flash-low, cape14s23425tokens/body21s22193tokens. Parentcheckedcallers
andvptrdata, acceptedbodyarrayselection fields, rejected capeclaimprimitive13
isTriangleList (it is QuadList; original countfaces<<2) andclaim822E8BD0allocates
clothbuffers (cape ctor823011E0 calls827796F8, different hierarchy). Cape ctor
andcleanup82301368/deletingdtor8231C4E8 verifiedsameclass table82021DA8.
Body table820217E0->82306BB0 alreadycoveredby822F5C98/base lifecycle.
Newbody observer validates actualcaller82307298/r30this, rootmodelr16viaowner
88->12->12, originalpartsarraymodel+12 andcountmodel+8 ==r17, cursorr26==base+4*r24,
incomingr3==*cursor; source origin3 includespartslot asgroup/submesh, noordinal.
C687actualshader WVPc8..11 (NOT c4..7), palettebase18 distinctfromC20922.
NeworiginalPS3EF6/E83A pair variants preservefullsignature/body/fragmenttail.
450native tests23RED->450GREEN; CTest3/3PASS. Build_body_motion exit0;
runtime body-motion launched pending. Cape lifecycle observationadded but
itsdynamicring vertexcorrespondence remainsUNQUALIFIED/nohistoryproof.
