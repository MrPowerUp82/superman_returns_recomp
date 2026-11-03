# Co-op continuation — 2026-11-03

## Objective and current state

Continue `checkpoint2.md`: two players in **one process, one shared world and two
independent split-screen views**. Keep the normal save and launcher behavior.

**Observed now:** a second gameplay actor can be created through the game's own
Lua entity lifecycle and bound to guest controller port 1, while P1 remains on
port 0. Both actors were visible in the same running world. Separate synthetic
controller commands produced movement responses from the corresponding actors.

**Still incomplete:** stop/idle behavior for P2, complete input isolation across
all abilities, two cameras/viewports, automatic join/leave, singleton ownership,
transitions, saving rules, and physical-controller testing. This is an opt-in
development probe, not a completed playable co-op release. `run_coop.cmd` still
only prepares inputs; it does not automatically spawn P2.

## Files to continue from

- `port/src/coop_lua.cpp`: existing file mailbox, plus UTF-8 BOM handling,
  context selection and a development-only controller-binding overload.
- `port/src/benchmark_input_driver.{h,cpp}`: optional second synthetic input;
  `MakeCoopDeviceAssignment()` keeps it exclusively on port 1 while delegating
  all normal devices to the SDK's SlotAssignment.
- `port/src/coop_split.cpp`: uses that assignment factory when co-op is enabled.
- `tools/coop/probe.ps1`: Start/Input/Script/Status/Stop for one isolated process.
- `tools/coop/{status,spawn_p2,inspect_p2,check_bindings}.lua`: reproducible probes.
- `tools/analysis/coop_ast_probe.py`: recover the missing research corpus locally.
- `tests/tools/test_coop_ast_probe.py`: RefPack overlap/size and BGFA bounds/zlib.
- `docs/coop/NOTES.md`: architecture and usage.

Generated assets, logs, game copies and saves are ignored under `artifacts/`.
No game assets were added to Git. No commit or remote publication was made.

## Recovered evidence and entry points

Exact game XEX SHA-256 was verified again:
`c8f243acd99de9a91f5ae4f409721c0e954e3d5eb96861419d3da07b8106db2b`.

Co-op Anything's current directory query returned no matching entry. Its engine
detector returned unknown for this Xbox extraction, not an identified engine.
The source port/runtime is ReXGlue; preserve the single-instance route described
in its split-screen playbook's inside-one-copy section.

Run `python tools/analysis/coop_ast_probe.py` to regenerate
`artifacts/coop-research/assets-index.json` and `assets/`. The full default scan
indexed 25,491 entries and extracted 4,534 UTF-8 text files, plus selected compiled
XML string tables. The unnamed archives in `objtiv.AST` matter; scanning only
preload/baseworld/lunits misses the Superman templates and FSM scripts.

Useful records with the current default scan order:

| Asset | Local output |
|---|---|
| SupermanEnt gameplay template | `assets/023490.strings.txt` |
| Superman layer spawn definition | `assets/023573.strings.txt` |
| Actor flow preloads | `assets/023574.strings.txt` |
| Actor flow costume attributes | `assets/023581.strings.txt` |
| Actor flow dependency-spawn script | `assets/023583.txt` |
| Superman attributes | `assets/023584.strings.txt` |
| Idle enter script, currentControl initialization | `assets/023982.txt` |

These paths are navigation aids. Use `assets-index.json` to locate by name;
compiled XML strings do not reconstruct the XML's hierarchy or all values.

| Native address | Meaning / evidence |
|---|---|
| `0x8275D340` | lua_pcall, mailbox observer |
| `0x8296D8CC` | Lua manager; main VM at manager+12 |
| `0x8296E114` | ScriptEntity context stack begin/end |
| `0x821D96E0` | SpawnEntityActive Lua wrapper |
| `0x821D8A10` | spawn implementation, derives world-flow context |
| `0x821C23E0` | Entity registry lookup by ID |
| `0x82262B38` | actor resolver; numeric ID or current ScriptEntity's ACTR+60 |
| `0x8229F778` | original SetActorToDefaultController Lua binding |
| `0x8229F380` | native actor-to-port table binding |
| `0x8296E8C8` | input manager; +4 slot array, +8 byte count, +12 default port |
| `0x8229DDD8` | CheckInputEvent; optional explicit actor at Lua argument 3 |
| `0x8229DF88` | GetInputEventValue; resolves current ScriptEntity's actor |

Input slot entries are actor IDs, with empty=255. Actor+88 supplies the ID used
by the native binding routine. The overload checks port range/integrality and
occupancy before invoking it, and returns a Lua boolean. The original
single-argument game call is untouched, including outside mailbox execution.

## Critical context distinction

The context stack holds **ScriptEntity interfaces**, not raw gameplay Entity
pointers. ScriptEntity uses vtable+24 for interface lookup; an explicit Entity
uses vtable+36. An experiment substituting the Entity into the context stack
crashed reading `0x57464C57` (WFLW). That hook was removed.

The implemented solution leaves the stack alone. A script starting with
`-- sr-coop-context: player1` waits for an actual P1 script callback. The missing
argument path in `sub_82262B38` resolves the current actor; it must match the
registry actor assigned to port 0 before the chunk is consumed. Normal status
queries can run in any valid script context. If the game is in a video or has
not started processing P1's FSM, a targeted chunk can remain pending.

## Validation actually performed

- Full `build.cmd` and subsequent incremental builds passed. Latest changed
  driver/assignment code was built by
  `.tools/build_reference_release_incremental.cmd`, with log
  `artifacts/coop-build-two-inputs.log`.
- Lua 5.0.2 VM queries succeeded at boot and inside the world. A UTF-8 BOM file
  returned `SR_COOP_BOM_OK` after the BOM handling change.
- In one isolated run, spawn returned P1=1, P2=8025 and native binding logged
  `actor=8025 bound to guest port=1`. Re-running the spawn script returned
  `P2 already spawned: 8025`, without another spawn.
- First P1-only forward pulse: P1 displacement 20.4660 game units; P2
  displacement approximately 0.000000119. Both were visible during Warworld.
- After adding the second synthetic pad, another isolated run returned
  P1=1, P2=8810, bound to port 1. The second device was explicitly logged on
  guest port 1, with no shared synthetic-device routing to port 0.
- P2-only 2-second forward pulse: P2 displacement 16.7369; P1 drift 0.07141.
  Both actors reported currentControl=2. This demonstrates a P2 input response;
  it does not establish complete gameplay independence.
- Binding validation returned:
  `same=true occupied0=false occupied1=false invalid=false fraction=false`.
- `python -m unittest discover -s tests/tools -p test_coop_ast_probe.py`: 2 passed.
- The actual local game archive extraction completed with decompression size
  validation, and `git diff --check` passed.
- All probe instances created in this continuation were stopped. No normal
  user game process was closed, and no normal save was overwritten.

Logs append across runs. Local runtime evidence is in
`artifacts/coop-test/probe.log` (especially the P2=8025 and P2=8810 runs).
`lua.log` and `lua-slot2.log` retain earlier failures. The current guest image
dump is `artifacts/coop-test/guest-image.bin` (base 0x82000000).

## Remaining behavior to investigate first

P2 continued moving after the forward pulse and after an explicit neutral
command. One neutral interval measured about 3.97 game units over 2.4 seconds.
CheckInputEvent reported MOVE=false and STRAFE=false for both actors, while
currentControl remained 2. RestartFSM(P2) did not remove the displacement.
Enemies/combat and animation/root motion were active; the exact cause is not
localized. Do not claim stop/idle handling or full independent movement passes.

Next probes should isolate an idle actor away from combat, inspect the native
locomotion/animation controller and per-actor analog values, and distinguish
residual velocity/root motion from shared single-player state. Use
`sub_8229DF88` and its animation/native consumers as leads. Do not reset FSMs
repeatedly as a substitute for identifying the shared state.

A development Lua chunk using `pairs(_G)` and `string.find` also crashed twice
with a read near guest 0x11. That separate failure is not localized. Avoid
global enumeration; use the extracted native registration code and focused
known API calls.

Only after controls are reliable, identify camera ownership and the simulation
vs render boundary for two world renders. No camera or viewport patch was made
in this continuation. Then handle joins, removals, transitions, cutscenes, saving,
and actual two-controller reconnect/rumble/playthrough tests.

## Resume commands

```powershell
./tools/coop/probe.ps1 -Action Start
# Wait for title, navigate menus deliberately. Start=16, down=2, A=4096.
# The bundled save has 5% and 15% slots. Verify selection on screen; a fast timed
# button sequence can select the first slot/new-game path and enter Warworld.
./tools/coop/probe.ps1 -Action Input -Buttons 16
./tools/coop/probe.ps1 -Action Script -Script tools/coop/status.lua
# Once P1 is processing gameplay scripts:
./tools/coop/probe.ps1 -Action Script -Script tools/coop/spawn_p2.lua
./tools/coop/probe.ps1 -Action Script -Script tools/coop/inspect_p2.lua
./tools/coop/probe.ps1 -Action Input -Port 1 -Forward 32767 -Milliseconds 2000
./tools/coop/probe.ps1 -Action Script -Script tools/coop/inspect_p2.lua
./tools/coop/probe.ps1 -Action Script -Script tools/coop/check_bindings.lua
./tools/coop/probe.ps1 -Action Status
./tools/coop/probe.ps1 -Action Stop
```

Set `-NoScript` on Start for a baseline without the mailbox. Start refuses any
existing game process, reuses the isolated copy/save, sets the probe to lower
process priority, and restores its temporary environment variables afterward.
The probe uses `native` rendering by default; `-Renderer xenos` is available.
