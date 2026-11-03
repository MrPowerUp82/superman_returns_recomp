# Co-op: two native world draws and side-by-side composition — 2026-11-03

## Scope

Continues checkpoint8 (`b6c7d7d`). This experiment adds two native world draws
within one guest render frame and queues two owned image copies for side-by-side
composition. It is gated by `sr_coop_split_preview`, requires co-op input, owned
P2 camera/transport and native D3D12, and excludes benchmarks, Xenos A/B and DLSS.
The normal co-op launcher still enables input preparation only.

The probe option is `./tools/coop/probe.ps1 -Action Start -SplitPreview`. It uses
the isolated copied game/save, selects D3D12, disables DLSS and fixes capture
scale at 100% for 1280x720 captures. Presentation may scale to a larger 16:9
window, as in the tested 1920x1080 output. Physical pads, full gameplay,
independent HUD ownership and temporal effects remain outside acceptance here.

## Draw and image lifetime

- The normal original view-0 application and P1 render preparation run once.
  Split preparation keeps the original matrix but uses aspect 8/9 for each
  eventual 640x720 pane. The native render target remains 1280x720 per view;
  each whole image is scaled into its own pane. No P1 image cropping is used.
- At the existing `sub_82776038` world-draw boundary, the original P1 world draw
  runs once. The additional path sets the consumed P2 matrix on world camera 0,
  reruns render-list preparation `sub_82783B48` with zero delta, draws the native
  world effect path again, and restores P1's matrix. It does not call main-world
  simulation, native camera production or state-buffer consumption again.
- The complete PPC context is restored after the additional calls. Readbacks
  check the consumed packet, selected world/HUD identities, world matrix and
  HUD transform. Mode changes, distant streaming and all render participants'
  side effects still need wider investigation; zero delta alone is insufficient
  evidence that arbitrary callbacks are safe to repeat.
- Native effect cadence (`effect+300`, `8277A2D8`) and fixed-increment clock
  (`effect+80`, `8277B998`) advance independently of delta. The second draw uses
  the pre-P1 clock/cadence of the three native effect assets, then restores the
  post-P1 values and graphics frame-time bytes. A checked readback verifies
  restoration. This avoids advancing these known shared effect phases twice.
- New D3D12-only `kCoopSplit` worker commands bracket begin/end P1 and begin/end
  P2. They carry a common frame ID and binding generation. The command ordering
  remains the same in direct and worker modes; no guest-thread D3D calls are made.
- `SplitFrame` accepts only the four ordered markers from one frame/binding.
  Invalid order, failed capture, cancellation or swap invalidates the pair.
  Completed images from a previous frame/binding cannot satisfy a new pair.
- Each end marker copies the current full-size color target, or the most recent
  complete full-size resolved texture from that pass. A small bound target does
  not suppress the resolved fallback. Resolved textures remain available even
  when the original surface is cleared by a resolve. A cleared surface is not
  accepted as the source without a subsequent write.
- Runtime discovery: the guest's swap occurs inside world effects, before the
  outer world-draw call returns. The native executor defers presentation during
  each view, retaining the resolved front-buffer address. The second completed
  image marker presents the composed pair. Cancellation flushes normal output.
  Guest swap bookkeeping still occurs for both effect traversals; this experiment
  does not establish unchanged pacing/performance of those callbacks.
- Two owned GPU resources retain the images before the next view overwrites
  shared guest targets. Resource replacements use the existing GPU retirement
  mechanism. Composition uses two separate SRV descriptors per fenced frame slot,
  the existing blit/gamma path and disjoint left/right viewport/scissor rectangles.
  A swap consumes the ready pair; absent/invalid pairs use normal presentation.

## Review and tests

Review identified unconditional effect-phase advancement, capture from a cleared
surface, a small-target fallback gap, and a report that could accept unrelated or
missing transport data. The fixes have negative tests observed failing first.
The report now requires transport, supported extents, both captured views and
matching world/composite frame IDs/generations, successful phase/camera/HUD
restoration, and sampled camera positions matching consumed P1/P2 data.

Focused packet/split tests: 10 passed. Full portable native suite: 640 passed,
zero failed checks (`artifacts/coop-native-suite-reviewed.log`). Tools suite:
155 passed, 34 subtests passed. Reviewed/deferred-swap Release builds passed
(`artifacts/coop-build-split-reviewed.log`, `artifacts/coop-build-split-deferred.log`). Shader/compiler deprecation warnings
from existing native code were emitted; no build errors occurred.

The first reviewed test PID 1572 rejected marker pairs because the internal
swap consumed the first view too early. Its 162 sampled groups / 8 techniques
per outer frame confirmed two effect traversals. It was stopped; it is not
acceptance evidence. A failing test reproduced the required deferred-swap
sequence before that policy was implemented.

## Runtime evidence

Final isolated run PID 16436 used only the copied 15% Metropolis save. P2 ID 8780
joined with generation `0xC0000001`; both panes were visually inspected and
showed different native world-camera perspectives simultaneously. P1 was left,
P2 right. Captures were 1280x720 RGBA8 (DXGI 28), selected from resolved textures;
the native output was 1920x1080. Both sampled image markers preceded each
sampled composed pair. Packet, world/HUD selection and effect-phase restoration
checks passed.

Two-second synthetic input checks used stable sampled split-world camera
positions before/after each port, with active displacement >2 game units and
inactive displacement <0.25:

| Input | P1 camera displacement | P2 camera displacement |
|---|---:|---:|
| Port 1 only, samples 240→480 | 0.00248 | 88.60026 |
| Port 0 only, samples 480→960 | 87.28847 | 0.01247 |

The corresponding pane moved while the other world view remained stable in
visual inspection. Full positions, sample IDs and automated threshold results
are in `artifacts/coop-test/split-motion-check.json`. These are camera-follow
checks in this scene, not general ground/flight/combat isolation guarantees.

Despawned 8780 at 19:23:03.743. The consumer's secondary block became zero,
and presentation returned to the ordinary single world view. Rejoined as P2
9633 at 19:23:26.423, generation `0xC0000002`; two distinct panes returned.
The generation-2 world/capture/composite samples were checked and visually
inspected. The owned run was stopped and archived in
`artifacts/coop-test/split-run.log` before subsequent smoke testing.

```powershell
python tools/analysis/coop_camera_report.py --log artifacts/coop-test/split-run.log --require-target 1 --require-secondary --require-split --output artifacts/coop-test/split-check.json
```

Passed: 17 complete world/capture/composition sample pairs across both P2
generations, both correlated with sampled consumed P1/P2 positions; 88 primary
update/submission pairs, 56 consumption/original-application pairs and 59 owned
secondary-camera samples. Transport had 60 active publication samples, 15 active
consumption samples and no unknown generation. The lifecycle intervals had 10
first-generation, 5 post-leave zero, and 5 replacement-generation samples.
No split/capture failure, preview/transport refusal, failed readback, guest access
violation or nonzero mailbox Lua status occurred in the final run. The report
also rejects a new published binding with no split-composition evidence.

Known visible limitation: each pane includes the original P1 HUD, compressed to
the half-width pane. P2 health/map ownership, correct HUD sizing and menu/cutscene
composition are not implemented by this milestone. No FPS improvement or pacing
equivalence is claimed; world effects and guest swap bookkeeping are traversed
twice, and the additional GPU image copies consume bandwidth.

Diagnostic-off isolated startup PID 26308 reached the normal title screen with
no split/camera diagnostic messages or guest access violations in the latest
run. The owned smoke process was stopped. Its retained startup evidence is in
`artifacts/coop-test/split-off-check.json`. `git diff --check` passed.

## Reproduction

```powershell
./tools/coop/probe.ps1 -Action Start -SplitPreview
# Load copied save slot 2 (15%), select Metropolis, wait for gameplay control.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
# Inspect both panes; exercise ports separately and allow stationary samples.
./tools/coop/probe.ps1 -Action Input -Port 1 -Forward 32767 -Milliseconds 2000
./tools/coop/probe.ps1 -Action Script -Script tools/coop/despawn_p2.lua
# Wait for consumed clear and normal single-view presentation, then rejoin.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
./tools/coop/probe.ps1 -Action Stop
python tools/analysis/coop_camera_report.py --require-target 1 --require-secondary --require-split --output artifacts/coop-test/split-check.json
```

The reporter's checks are sampled state/capture/composition evidence. Visual
inspection and movement checks are still needed to establish what each pane
shows; the report does not prove every pixel, large-distance culling/streaming,
HUD correctness, arbitrary effect history or performance. All runtime evidence,
copied saves, dumps and game assets remain ignored under `artifacts/`.
