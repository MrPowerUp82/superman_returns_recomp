# Superman Returns: local co-op in one instance

## Decision (2026-11-03)

The requested architecture is **two players in one process, one shared world,
with split-screen views**. The earlier two-process/localhost route is retired.
No networking is required for this local mode.

**Status: experimental second-actor creation and controller binding, exercised
through an isolated development probe. There is no finished two-player mode or
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
./tools/coop/probe.ps1 -Action Status
./tools/coop/probe.ps1 -Action Stop
```

- The opt-in `sr_coop_script` mailbox executes one Lua chunk per file edit on the
  guest's own thread, preserving PPC registers and the Lua stack. UTF-8 BOMs are
  stripped for Lua 5.0.2. Errors/results are written to the log.
- A leading `-- sr-coop-context: player1` waits until the VM is processing P1's
  actual script context. `SpawnEntityActive` derives the template's world-flow
  ownership from this context. A game Entity pointer cannot substitute for a
  ScriptEntity interface; an earlier such experiment was removed after crashing.
- The mailbox-only overload `SetActorToDefaultController(actor, port)` binds a
  zero-based port through the game's `sub_8229F380` routine. It validates 0..3,
  refuses to replace another actor occupying the requested port, and returns a
  boolean. Original single-argument calls retain their original behavior.
- `spawn_p2.lua` uses the game's `SupermanEnt` template, sets its control mode to
  gameplay, and binds it to port 1. A session-local guard prevents repeated
  execution from creating more actors. This guard is **not** a transition/join
  lifecycle implementation; stale IDs after level changes remain to be handled.
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
   gameplay template and registered as a distinct ID. Destruction, singleton
   references, transitions and repeatable join/leave remain open.
3. **Independent control (partial):** separate actor binding and synthetic pad
   movement responses have been observed. P2 stop/idle still needs investigation.
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

Continuation evidence is recorded in `docs/checkpoints/checkpoint3.md`. The
second-actor creation is now observed, but physical controllers and independent
views remain unverified. A development script enumerating `_G` with `pairs` and
`string.find` triggered guest access violations; that failure is not localized.
