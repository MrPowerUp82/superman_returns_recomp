# Superman Returns: local co-op in one instance

## Decision (2026-11-03)

The requested architecture is **two players in one process, one shared world,
with split-screen views**. The earlier two-process/localhost route is retired.
No networking is required for this local mode.

**Status: input preparation only. There is no playable two-player mode or
split-screen rendering yet.** Opening one instance does not add multiplayer to
the single-player guest game.

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
  return site `0x825FFDBC` passes r24. Whether the guest selects a controller on
  Start, and where its input is bound to the controlled actor, remain unverified.
- `.gemini-tasks/20261026-player-read/results/02-body.out.md` traces the model
  instance used by `sub_82306BB0`. This is rendering provenance, **not** a confirmed
  player constructor, gameplay object, or safe cloning interface.
- `port/src/graphics/camera_candidate_probe.h` observes a candidate view/projection
  shader constant. It explicitly does not establish a gameplay camera object,
  matrix convention, or a safe second-camera rendering path.

## Implementation milestones

1. **Input preparation (implemented):** independent guest controller ports in one
   process. Verify with two real controllers, disconnect/reconnect and rumble.
2. **Player lifecycle:** identify the controllable actor constructor, spawn and
   destruction, world registration, input update and singleton references. Spawn
   P2 through the game's lifecycle; copying a render object is insufficient.
3. **Independent control:** bind guest ports 0 and 1 to separate actors, including
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

Real-controller routing, a second actor and independent views remain unverified.
The earlier two-window test only validated the retired launcher and is not
validation of the requested local co-op mode.
