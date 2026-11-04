# Checkpoint 16 — launcher co-op entry and built-in P2 join

Date: 2026-11-04. Continues checkpoint15 after the user reported that the
experimental multiplayer worked and requested a launcher entry.

## Behavior

The Windows launcher has a persisted **Cooperativo — 2 jogadores
(experimental)** / **Co-op — 2 players (experimental)** option. Connect two
controllers before clicking Play, load a save in Metropolis, and the second
Superman joins when P1 has gameplay control. The first and second controllers
retain their existing guest-port 0/1 assignment.

The co-op argument bundle enables input, automatic join, second camera,
camera diagnostics/transport, split composition, shared idle, status handoff,
APT observations and independent stamina-position preview. It selects native
D3D12, 100% internal scale, DLSS off and frame generation off. UI controls show
those effective choices while solo preferences remain saved separately in the
same settings object. Disabling co-op restores them. The benchmark uses solo
preferences and receives no co-op flags.

The launch uses the normal game save. It neither copies the probe's save nor
installs any game files. Inherited `SR_BENCH_INPUT` / `SR_BENCH_INPUT_P2` variables
are removed from the child environment so a developer shell cannot inject
synthetic controller activity into a normal co-op session.

## Built-in join

`sr_coop_autojoin` is off by default and requires `sr_coop_input`. CMake embeds
the existing `tools/coop/spawn_p2.lua` in the executable; no checkout, PowerShell
process, script path or editable mailbox is required by the launcher.

The bridge retains checked guest reads, main-Lua-state validation, P1 actor
context selection, PPC-register/Lua-stack restoration and the restricted
two-argument controller binding. Only world/P1/control readiness results may
retry, once per second. Success, an already retained P2, partial spawn/binding
failure, unexpected results and Lua errors end automatic joining for the
session. The retained actor ID and one-shot policy prevent repeated spawns.
Ordinary guest calls keep the original controller binding behavior.

The existing editable mailbox remains available only through its separate
development flag. `probe.ps1 -AutoJoin` implies `-StaminaPreview`; combine it
with `-NoScript` to reproduce the built-in path without an external Lua chunk.
The probe still uses synthetic inputs and a copied save. `run_coop.cmd` remains
input-only and now directs users to the launcher for full experimental co-op.

## Validation

- Launcher checks: **97 passed, zero failures**. Coverage includes old/default
  settings, preference round trips, incompatible saved graphics settings,
  exactly one argument per option, required co-op dependencies, synthetic input
  exclusion, normal saves, solo restoration and benchmark isolation.
- Native CTest: **5/5 targets passed**, including **655 native tests**.
- Tools: **182 tests and 248 subtests passed**.
- Published the self-contained launcher beside the game and mirrored it to
  `artifacts/launcher`.
- Windows UI: checked co-op activation, effective disabled settings, visible
  instructions and restoration of keyboard/mouse on returning to solo.

- Release game build passed (`artifacts/launcher-coop-build-final.log`).

### Runtime

Owned PID **23428** used `-AutoJoin -NoScript`, copied slot 2 (15%) and
Metropolis. The title/load menus retained a single view. After gameplay started,
the built-in join returned **P1=1, P2=11608**, bound P2 to port 1 once and
constructed its native camera. The recorded process command line has no
`sr_coop_script`; logs show no external chunk execution. Visual inspection
confirmed two characters, distinct views and both HUD/radar passes; a P2 forward
command woke the shared HUD. `launcher-autojoin-join-check.json` passed.
`launcher-autojoin-apt-check.json --require-preview` passed with **4 composed
pairs, 265 private packet observations and zero skipped previews**.

A longer isolated run, owned PID **24812**, loaded the same verified save and
joined **P1=1, P2=9757** once. Preserve both `launcher-autojoin-long.1.log` and
`launcher-autojoin-long.log`: spdlog rolled over during the run. The APT checker
passed **7 composed pairs, 379 observations, 14 ancestry scopes and zero skips**.
The camera report observed 39 publications, 5 matching active consumptions,
7 verified split pairs and 7 native status pairs with no primary-meter mismatch.
Its strict combined gate **did not pass**: the idle run had only one matched
radar pair and no sampled split-position/consumption correlation. These counts
are partial diagnostics, not a new full camera-validation claim; checkpoint15
retains the earlier controlled renderer evidence. A preliminary short camera
report also lacked the minimum active sample count. No renderer code was changed.

All owned game processes stopped. Runtime tests used only the isolated save;
the user's physical-controller result was not independently repeated here.

## Remaining scope

Automatic joining happens once per process; level transitions, death/respawn,
rejoining after removal and complete gameplay validation remain experimental.
There is no new network protocol or Parsec-specific integration. The native
rendering and partial stamina-widget limits from checkpoint15 still apply.
The user's physical-controller result is a user report; automated movement
checks continue using the isolated synthetic-input harness.
