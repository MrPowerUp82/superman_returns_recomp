# Superman Returns: local co-op in one instance

## Decision (2026-11-03)

The requested architecture is **two players in one process, one shared world,
with split-screen views**. The earlier two-process/localhost route is retired.
No networking is required for this local mode.

**Status: experimental second-actor creation and controller binding, exercised
through an isolated development probe, plus an opt-in separate native camera
controller for P2 and optional transport to the render thread. There is no finished two-player mode or
split-screen rendering yet.** `run_coop.cmd` still enables input preparation only.

## Current implementation

- `run_coop.cmd` starts one executable, with `--sr_coop_input=true`, and writes
  `logs/coop.log`. It does not resize the window, copy configs or launch player 2.
- `port/src/coop_split.{h,cpp}` installs the SDK's `SlotAssignment`: physical
  controller ordinal N feeds guest port N; keyboard/mouse emulation feeds port 0.
  The first two controllers are the intended P1/P2 inputs. The SDK retains
  ordinals across disconnects instead of shifting P2 into P1's slot.
- The flag is off by default. `run.cmd` retains normal input behavior. Title
  benchmark mode retains its isolated input behavior even if the flag is set.
- Both players will use the same save/world. Story progression belongs to P1.
  There is no player-2 user-data folder in the new launcher. Existing `coop/p2`
  data is retained on disk, ignored by Git, and unused by this launcher.
- Removed `--sr_coop_pad`, `--sr_coop_keyboard`, `--sr_coop_window`, background
  joystick overrides and per-copy config relocation. Use `run_keyboard.cmd` or
  `--mnk_mode` when keyboard/mouse emulation is wanted.
- Startup explicitly warns that player 2 and split-screen are not implemented.
  Exposing guest port 1 alone cannot make the original game use it.

## Development probe (2026-11-03 continuation)

`tools/coop/probe.ps1` creates/uses a separate copy of the game and save in
`artifacts/coop-test`. It only stops its own recorded PID after checking its
executable and command line. It refuses to start alongside an existing game.
The normal launcher does not execute these scripts.

```powershell
python tools/analysis/coop_ast_probe.py
./tools/coop/probe.ps1 -Action Start
# Navigate the title/load menus. Input uses decimal Xbox button masks:
# Start=16, D-pad down=2, A=4096. Wait for each screen before the next command.
./tools/coop/probe.ps1 -Action Input -Buttons 16
# After loading gameplay (currentControl=2):
./tools/coop/probe.ps1 -Action Script -Script tools/coop/status.lua
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
./tools/coop/probe.ps1 -Action Script -Script tools/coop/inspect_p2.lua
# On foot, in open ground: pauses enemy FSMs in this disposable probe.
./tools/coop/test_movement.ps1
# Optional lifecycle probe: remove P2, then create/bind a replacement.
./tools/coop/probe.ps1 -Action Script -Script tools/coop/despawn_p2.lua
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
./tools/coop/probe.ps1 -Action Status
./tools/coop/probe.ps1 -Action Stop
```

- The opt-in `sr_coop_script` mailbox executes one Lua chunk per file edit on the
  guest's own thread, preserving PPC registers and the Lua stack. UTF-8 BOMs are
  stripped for Lua 5.0.2. Errors/results are written to the log.
- A leading `-- sr-coop-context: player1` or `player2` waits until the VM is
  processing the actor bound to port 0 or 1. File edits are polled every 250 ms;
  a cached pending chunk checks context on each valid callback and executes once.
  A newer edit replaces a pending chunk. `SpawnEntityActive` derives the template's world-flow
  ownership from this context. A game Entity pointer cannot substitute for a
  ScriptEntity interface; an earlier such experiment was removed after crashing.
- The mailbox-only overload `SetActorToDefaultController(actor, port)` binds a
  zero-based port through the game's `sub_8229F380` routine. It validates 0..3,
  refuses to replace another live actor occupying the requested port, and returns
  a boolean. A removed actor's stale slot can be reclaimed through the native
  binding routine. Original single-argument calls retain their original behavior.
- `spawn_p2.lua` uses the game's `SupermanEnt` template, sets its control mode to
  gameplay, and binds it to port 1. A session-local guard prevents repeated
  execution from creating more actors. This guard is **not** a transition/join
  lifecycle implementation; stale IDs after level changes remain to be handled.
- `despawn_p2.lua` restores the camera to P1, removes P2 and clears the local
  guard; `bind_p2.lua` retries binding without spawning. `movement_p1.lua` and
  `movement_p2.lua` sample inputs, speed and desired speed in each actor's context.
- `test_movement.ps1` measures both actors before/after port 1, port 0 and
  simultaneous commands, then checks release/idle. It requires a running loaded
  probe with P2 and P1 on foot in open ground. It pauses enemies, repositions P2
  and follows P2 with the single camera. Stop the disposable probe afterward.
  Results, including raw positions, go to `artifacts/coop-test/motion-check.json`.
- `SR_BENCH_INPUT_P2` enables a second synthetic pad only alongside
  `SR_BENCH_INPUT`, co-op input and no title benchmark. The probe sets both.
  Its assignment routes only that device to port 1, leaving normal physical-pad
  ordinals and keyboard routing under the SDK's SlotAssignment. To exercise P2:
  `./tools/coop/probe.ps1 -Action Input -Port 1 -Forward 32767 -Milliseconds 2000`.
- `coop_ast_probe.py` recursively indexes local BGFA archives, validates RefPack
  output sizes and archive offsets, and extracts Lua/XML and compiled XML string
  tables to ignored artifacts. A string table is not a reconstructed XML tree.
  Its field layout follows [Luigi Auriemma's BGFA parser](https://aluigi.altervista.org/bms/nbajamfire.bms).

## Evidence and reverse-engineering leads

### Camera/render handoff probe

Start the isolated probe with `./tools/coop/probe.ps1 -Action Start -CameraProbe`.
This enables `sr_coop_camera_probe` only for this run. It is off by default,
requires co-op input, and is disabled during title benchmarks. Its hooks call
each original exactly once and observe checked copies of guest memory; they do
not add a camera, change a viewport or replay a simulation/render pass.

After loading gameplay and spawning P2, send `tools/coop/camera_p2.lua` through
`-Action Script` to make the **existing** camera follow P2; `camera_p1.lua`
restores P1. Wait for samples (first three calls, then every 120 calls). Stop the
owned probe before collecting the final report so log rotation cannot race it:

```powershell
./tools/coop/probe.ps1 -Action Stop
python tools/analysis/coop_camera_report.py --require-target 1 --require-target P2_ID
```

Replace `P2_ID` with the numeric ID returned by spawn. The report reads the
active log plus numbered spdlog rotations in chronological order and evaluates
only the latest startup. Missing startup/samples, unreadable data, non-finite
positions, changed projection/viewport or inconsistent position copies fail the
check. This checks sampled position propagation at two boundaries, not full
matrix correctness, producer/consumer packet identity, two views or input timing.
It also records the most recent sampled render-group/technique sequence.

`docs/checkpoints/checkpoint5.md` records verified camera submission/consumption,
the active effect route, and the remaining work for a second view. In the tested
run, the packet's second transform block was inactive; the renderer's separate
camera used for group 23 must also be accounted for before reusing a view slot.

### Separate P2 camera controller (development)

`./tools/coop/probe.ps1 -Action Start -CameraProbe -SecondCamera` additionally
enables `sr_coop_second_camera`. It constructs an owned native camera graph
alongside the primary camera's initialization, when its parameter resources are
available. After P2 is bound to port 1, it binds that actor to the new graph and
updates only the active native controller. The primary camera's singleton is
never replaced. The complete manager update, global camera side effects and
world simulation are not run a second time.

At join, if the guest selected P2 as the primary camera home, the probe returns
that home to the actor on port 0 through the native setter. Other scripted home
targets are preserved. When P2 disappears, secondary updates stop; the parameter
graph is retained for rebinding. Primary-manager destruction calls the secondary
native destructor, retaining the small reusable outer SystemHeap reservation.

After stopping the probe, add `--require-secondary` to the camera report command
to require at least three finite snapshots from distinct camera owners and actor
targets. The implementation and validation limits are in
`docs/checkpoints/checkpoint6.md`.

**This option computes a second camera transform.** Enable the separate transport
option below to send it to the render thread; neither option draws a second
viewport. Mode transitions, right-stick camera control,
collision, cutscenes and full level/shutdown lifecycle still need validation.

### P2 camera packet transport (development)

`./tools/coop/probe.ps1 -Action Start -CameraTransport` enables the camera probe,
secondary controller and `sr_coop_camera_transport` together. All remain off by
default. The transport writes only the unused second 96-byte view in the exact
native camera producer packet before its original serialization. It refuses an
occupied slot, verifies the primary bytes after writing, clears its own slot
when P2 disappears, and changes the view generation on every new actor binding.
The native consumer carries the secondary transform, projection and generation
across its existing state-buffer boundary.

After stopping the probe:

```powershell
python tools/analysis/coop_camera_report.py --require-target 1 --require-secondary --require-handoff --output artifacts/coop-test/camera-transport-check.json
```

This checks sampled publication/consumption and primary preservation; it does
not prove full matrix correctness or render the second view. Every exercised
publication interval must reach the consumer, including an all-zero block after
leave and a matching position for each rejoin generation. Wait for a sampled
clear before respawning; startup zeros do not count as leave evidence. The isolated
leave/rejoin evidence and remaining rendering work are recorded in
`docs/checkpoints/checkpoint7.md`.

| Item | Finding |
|---|---|
| Title | Superman Returns (Xbox 360, 2006) |
| Executable | `game/default.xex`, 9,973,760 bytes |
| SHA-256 from earlier triage | `c8f243acd99de9a91f5ae4f409721c0e954e3d5eb96861419d3da07b8106db2b` |
| Runtime | ReXGlue static recompilation |

Earlier triage found no multiplayer UI strings in `game/DATA/text.AST`; that is
evidence of a single-player game, not proof that every multiplayer-related code
path has been excluded.

- Controller reads: `sub_828A7A38` calls `XamInputGetState`; the known caller at
  return site `0x825FFDBC` passes r24. Input manager `0x8296E8C8` holds the actor
  IDs by port at `manager+4`, with count at `manager+8` and empty ID 255.
  `sub_8229DDD8` (CheckInputEvent) finds the calling actor's ID in that table.
- `sub_821D96E0` (SpawnEntityActive) calls `sub_821D8A10`; template lookup uses
  the script-context stack `0x8296E114`. `sub_82262B38` resolves an explicit actor
  argument or the current script's ACTR interface. `sub_8229F778` is the original
  SetActorToDefaultController Lua binding; `sub_8229F380` binds an actor to a port.
- The `SupermanEnt` asset is in the unnamed parent of `objtiv.AST`, under
  `SUPERMENACTORWORLDFLOW_OTHER`. It includes the ACTOR, physics proxy, animation,
  cloth, powers, targeting and FSM systems. This is a gameplay template.
- `.gemini-tasks/20261026-player-read/results/02-body.out.md` traces the model
  instance used by `sub_82306BB0`. This is rendering provenance, **not** a confirmed
  player constructor, gameplay object, or safe cloning interface.
- `port/src/graphics/camera_candidate_probe.h` observes a candidate view/projection
  shader constant. It explicitly does not establish a gameplay camera object,
  matrix convention, or a safe second-camera rendering path.

## Implementation milestones

1. **Input preparation (implemented):** independent guest controller ports in one
   process. Verify with two real controllers, disconnect/reconnect and rumble.
2. **Player lifecycle (experimental spawn exercised):** P2 was created from the
   gameplay template and registered as a distinct ID. Explicit despawn/respawn
   with stale-slot recovery passed. Singleton references, transitions and
   automatic join/leave remain open.
3. **Independent control (partial):** isolated ground movement, release/idle and
   simultaneous synthetic inputs passed six automated checks with enemies paused.
   The earlier displacement during combat remains unlocalized.
   Validate guest ports 0 and 1 across separate actors, including
   movement, flight, combat, animation and collision. Verify that P2 input never
   drives P1. Keep missions and saving under P1's ownership.
4. **Cameras and renderer:** identify camera update and world render boundaries;
   render the same world for two cameras into distinct viewports with correct
   aspect ratios, culling, depth, HUD and post effects. Update simulation once per
   frame, then render both views. Duplicating/cropping P1's image is not this mode.
5. **Shared-world rules:** define P2 distance limits, cutscenes, level transitions,
   death/respawn, menus and joining/leaving without corrupting the save.
6. **Validation:** two physical controllers; ground/flight/combat; mission and
   level transitions; controller reconnects; renderer regression and performance
   measurements. No performance improvement is assumed before measurement.

## Validation

Checks after the route change (2026-11-03):
- `build.cmd`: passed, including the game executable and launcher build.
- `run_coop.cmd --sr_window_offscreen=true`: exactly one game process observed;
  its command line contained the new input flag without per-copy options.
- `logs/coop.log`: confirmed single-instance input preparation and the warning
  that second-player gameplay and split-screen are not implemented. The game
  continued presenting frames. The smoke-test instance was then stopped.
- `git diff --check`: passed.

These original launcher checks did not verify real-controller routing, a second
actor or independent views. The earlier two-window test only validated the retired launcher and is not
validation of the requested local co-op mode.

Continuation evidence is recorded in `docs/checkpoints/checkpoint3.md`,
`docs/checkpoints/checkpoint4.md`, `docs/checkpoints/checkpoint5.md` and
`docs/checkpoints/checkpoint6.md`, and `docs/checkpoints/checkpoint7.md`. These cover movement checks, despawn/rebind,
the camera/render handoff, the separate native P2 controller and secondary packet transport. Physical controllers, full combat and
independent views remain unverified. A previous development script enumerating
`_G` with `pairs` and `string.find` triggered guest access violations; that failure
is not localized. The latest isolated movement run had no guest/script errors.
