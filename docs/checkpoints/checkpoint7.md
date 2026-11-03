# Co-op: secondary camera state-buffer transport — 2026-11-03

## Current state

Continues checkpoint6 (`44370b9`). The owned P2 camera now has opt-in transport
through the guest's existing camera state buffer to the render-thread consumer.
P1 continues using the original rendering route. **A second viewport is still
not drawn.** This milestone transports camera data; playable split-screen needs
per-view preparation, world drawing and composition.

## Implementation

- `sr_coop_camera_transport`, off by default, requires both co-op input and the
  separate camera option, and excludes title benchmarks. Probe option
  `-CameraTransport` enables all three diagnostics/options together.
- After the owned native controller update, `coop_second_camera.cpp` copies
  finite transform and perspective parameters into a mutex-protected host
  snapshot. The transport takes a value copy, not native controller pointers.
  Invalid updates and missing/dead targets invalidate that snapshot.
- `coop_camera_packet.h` encodes the view in guest big-endian order. It rejects
  nonfinite values, invalid projection and zero generations. Generations use
  `0xC0000000 | binding_counter`, with a nonzero 30-bit counter incremented on
  every new P2 binding, including reuse of the same owned camera graph.
- The hook on `sub_82782200` admits only the packet pointer at
  `[ [0x8296D8C0] + 16 ]`, the producer used by `sub_82176698`. The generic method
  also serves unrelated packet types; those bypass transport. Its original
  implementation runs exactly once, with the original guest context.
- The first view occupies bytes 16..111; the second occupies 112..207. Only the
  latter 96 bytes are written. A readback compares all bytes 0..111 with the
  pre-write copy and checks the complete secondary block against its encoding.
  Fields after byte 207 are outside the write. The native serializer remains
  responsible for the rest of the packet.
- The secondary block contains matrix +0, near/far/FOV/aspect +64, the original
  1280x720 packed viewport +80, generation +88 and padding +92. This is transport
  at the original aspect, not final split-screen geometry. Padding does not carry
  metadata because the native copy routine does not preserve it.
- Ownership tracks the producer packet address and last written generation.
  A nonzero slot with different ownership is refused, never overwritten. With
  no valid snapshot, only an owned slot is cleared. Errors/refusals are reported
  and fail the analysis report.
- Existing native `sub_82779D68` serialization and `sub_8277A168` consumption
  carry both blocks. `sub_8277A048` compares generations before interpolation:
  a changed generation copies the new view instead of blending from a previous
  actor. No consumer hook, HUD camera replacement, additional simulation update
  or render pass was added.

## Validation

Release build passed with `.tools/build_reference_release_incremental.cmd`;
log: `artifacts/coop-build-camera-transport.log`. Four portable C++ packet tests
passed: byte order and write extent; rebind/clear; occupied/foreign ownership;
short buffers, nonfinite values and invalid projection. The initial missing
codec and missing report argument were observed failing before implementation.
Tool suite: `python -m pytest tests/tools -q`, 152 passed and 13 subtests passed.

Isolated native-renderer run PID 23064 used only the copied 15% save in Metropolis:

1. P2 ID 7481 bound to the owned manager `0x3003B000`; primary manager
   `0x0063ADC0` retained target 1.
2. Publication and render-thread consumption both showed generation
   `0xC0000001`, finite secondary position and projection
   `0.5,18000,1.5,1.7777778`, viewport `00000000,050002D0`.
3. Synthetic port 1 forward input for two seconds moved the received secondary
   position from approximately `(-2153,1202,-159)` to `(-2152,1202,-70)`;
   the primary stayed approximately `(-2156,1202,-148)`. This is an observed
   camera-follow check, not a new general control-isolation test.
4. Despawn suspended the controller and published a zero secondary block at
   17:40:41.588. Consumption at 17:40:46.454 showed zero secondary position,
   projection, viewport and flags. This confirms clearing after departure,
   separately from the report's boot-time inactive samples.
5. Replacement P2 ID 7488 reused the graph and published generation
   `0xC0000002`, also observed by the consumer.
6. Primary readback remained unchanged and secondary readback matched. The
   normal single viewport continued displaying P1 and both actors.

Stopped the owned process and analyzed retained log segments before starting
the separate diagnostic-off smoke test:

```powershell
python tools/analysis/coop_camera_report.py --require-target 1 --require-secondary --require-handoff --output artifacts/coop-test/camera-transport-check.json
```

Passed: 22 active publication samples, 13 active consumption samples, both
generations and no unknown received generation; 12 sampled positions matched a
publication within 0.05 game units. There were 21 secondary-controller samples,
41 primary update/submission pairs and 43 consumption/application pairs, all
primary position deltas zero. No guest access violation, nonzero mailbox Lua
status, refused slot or failed readback occurred in this run. These sampled
checks do not establish every-frame identity or full matrix correctness.
Code review found that aggregate generation checks could miss a stale rejoin or
an unconsumed clear. A negative test reproduced that false pass before the
reporter was fixed to require consumption in every publication interval, with
position correspondence for each active interval and fully zero secondary data
for the clear. Reanalysis passed: 5 first-generation samples, 2 post-leave zero
samples and 8 second-generation samples. Boot-time zero samples cannot satisfy
the leave check. A very fast leave/rejoin without a sampled clear is insufficient
evidence and fails this diagnostic report; reproduction must wait for consumption.
The final run was also isolated in `artifacts/coop-test/camera-transport-run.log`
and rechecked with `--log` pointing to that archive, so later smoke-test startup
markers do not replace its evidence. The saved JSON now references that archive.

Separate diagnostic-off smoke PID 14316 reached the title screen and presented
at least 1920 frames, with no transport/secondary-camera messages or guest access
violations in that run. The owned smoke process was stopped afterwards.

## Reproduction and next work

```powershell
./tools/coop/probe.ps1 -Action Start -CameraTransport
# Load copied save slot 2 (15%), select Metropolis, wait for gameplay control.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
# Observe publication and stage=consume secondary_flags=C0000001.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/despawn_p2.lua
# Wait for consumption with zero secondary flags.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
# Observe a new generation; then stop and run the report above.
./tools/coop/probe.ps1 -Action Stop
```

Next: route each transported view through preparation/culling and the active
world effect path (`sub_82776038` / `sub_82776EB0`), then compose viewports with
correct aspect, depth, post effects and HUD ownership. `sub_82775AA0` still
applies only view 0. The render camera used by HUD group 23 remains separate;
its camera array slot must not be repurposed for P2. Do not replay the main
simulation frame to obtain another image.

Ground/flight mode routing, physical controllers/right stick, collisions,
death, cutscenes, level transitions and complete teardown remain unvalidated.
All logs, extracted assets, copied saves and executable evidence stay ignored
under `artifacts/`; no game assets are committed.
