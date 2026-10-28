# Four guest scheduler peers: evidence and render-only boundary

The generated guest code contains four engine scheduler objects. This identifies
four paced peers, not four independently proven presentation limiters. The
existing native hook changes the render peer only. This audit adds regression
coverage and does not change production pacing, simulation, sound, or clocks.
It does not establish correspondence with all four loops in the private-fork
screenshot, whose addresses and contracts are unavailable.

## Source and image provenance

Inspected the current generated PPC C++, the current
`port/src/native_renderer/guest_render_scheduler.cpp` and
`port/src/native_renderer/guest_render_scheduler_policy.h` implementations,
and [the earlier scheduler observations](xenia-canary-fps-unlock-2026-10-27.md).
No game, runtime hook, web request, REA provider, or heavy build was run for
this audit. Source conclusions below are static; prior runtime findings remain
attributed to the earlier document.

Read-only supporting image: `port/logs/default_image.bin`, 10,354,688 bytes,
SHA256 `629983cbecf6a607a5b1fceea9a4f7078cf554adc667dc0bb5c042c48de7d214`.
This is the decoded memory image at base `0x82000000`: use `VA - base`, not
the original PE section raw-file offsets retained in its headers. For example,
the 32 bytes at VA `82166178` are
`7d8802a69181fff8fbe1fff09421ff907c86237838a000017c7f1b78485ba845`,
matching the generated constructor instructions. Its vtables and static rate
tables corroborate the source. It does not supply live BSS/object state or
cryptographic identity for a currently running process.

## The four peers

`8218CFE8` constructs and starts all four peers, passing priorities 5, 0, 2,
and 5 to core, SIM, render, and sound respectively. It starts each through
`82720590`. Source: `port/generated/default/superman_returns_recomp.135.cpp:2452`.
The three tables at `82911888`, `8291188C`, and `82911890` each contain bytes
`3c 1e 1e 3c` (60, 30, 30, 60) in the supporting image and in the previously
recorded runtime observation. The fields below are layout/source identities,
not a new measurement that every peer runs at its nominal rate during gameplay.

| Peer | Creation / constructor | Vtable | Mode at +52 | Table byte / rate | Scheduler pointer global | Callback at vtable +4 | Graph-group pointer global / initial group |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Core | `82177E60` (inlined derived construction; standalone equivalent `82166060`) | `82005B70` | 0: fixed step | +0 / 60 Hz | `8296D8D8` | `821660B8` | `82911310` / `8295C62C` |
| Render | `82177F90` → `82166178` | `82005B98` | 1: variable step | +1 / 30 Hz | `8296D8DC` | `82166210` | `82911318` / `8295C690` |
| SIM | `821780C0` (inlined derived construction; standalone equivalent `821662D0`) | `82005BC4` | 0: fixed step | +2 / 30 Hz | `8296D8E0` | `82166328` | `82911324` / `8295C65C` |
| Sound | `821781F8` → `821663E8` | `82005BEC` | 1: variable step | +3 / 60 Hz | `8296D8E4` | `82166468` | `8291132C` / `8295C6C0` |

Creation sources: `.90.cpp:2271`, `.117.cpp:2036`, `.68.cpp:1452`,
`.119.cpp:1793`. Derived constructor sources: `.28.cpp:1911`, `.27.cpp:2129`,
`.128.cpp:2016`, `.79.cpp:1426`, all under `port/generated/default/` with
prefix `superman_returns_recomp`.

Each callback loads its own graph group then branches to `8271ED68`:
core `.1.cpp:1758`; render `.39.cpp:1999`; SIM `.63.cpp:1975`;
sound `.76.cpp:2114`. `8271ED68` is `.11.cpp:25801`. It performs graph
bookkeeping, then follows either `8271EB00` or the currently selected root's
virtual update at slot +20. This is graph work, not a four-way chain of
presentation sleeps.

The group identity is independently traceable. Initializers `828DAB28`,
`828DAB78`, `828DABD8`, and `828DAC28` assign group vtables `82001808`,
`82001820`, `82001870`, and `820018C0`. Their slot +12 methods return strings:
`82148F10` → `8200171C` (`FGGameCore`), `821490A8` → `82001728`
(`FGGameRender`), `821492D8` → `82001788` (`FGGameSim`), and `821497A8`
→ `820017D4` (`FGGameSound`). Initializer sources: `.75.cpp:33387`,
`.103.cpp:31140`, `.63.cpp:32317`, `.21.cpp:34708`.

## Actual pacing and delta paths

All four scheduler vtables have +0 = `82720B50`, +32 = `82196960`, and
+36 = `82720C60`. Render also overrides +40 with `821661F8`, which tests
the pause flag; it is not another independent timer loop.

`827209D8` initializes mode, clock/credit fields, and the nominal rate, calls
`82720418` to derive +72, then sets +88 to three times that initial period.
Sources: `.111.cpp:25951`, `.108.cpp:25019`. The setter uses PPC single
precision `float(1e9 / rate)`, adds 0.5 in single precision, then truncates
to int64. This yields **33,333,334 ns at 30 Hz and 16,666,668 ns at 60 Hz**;
the latter is not mathematical `ceil(1e9/60)`. The native explicit target
policy intentionally uses integer ceil, hence 16,666,667 ns at target 60.

`82720590` seeds +104 with one period, +112 with zero and +96 with the
current clock, then starts the scheduler thread. Source: `.13.cpp:26772`.
The existing render hook runs before this publication/startup path.

`82720B50` loops while +66 is set, checks pause +65, and polls the clock
through `827203A0`. It updates accumulated time +104 and allows the next
stage only when **+104 + +112 >= +72**. A failed gate or callback uses the
fallback `825F7168`, not a literal Sleep(33). Source: `.126.cpp:25252`.
`827206B8` repeats this same gate under the scheduler lock; it is called by
`82720C60`. Sources: `.8.cpp:25822`, `.16.cpp:27608`. These are two checks
of the same per-object period, not two independently configured frame caps.

`82720C60` tests readiness at vtable +32, calls `82720740`, passes +132 to
the callback at vtable +4, then accounts the result through `82720858`.
The mode branches in `82720740` are decisive (`.35.cpp:26296`):

- Mode 0 assigns +120 from **+72**: changing the gate also changes the fixed
  callback timestep. This applies to core and SIM.
- Mode 1 assigns +120 from the newly accumulated elapsed time +104; it
  subtracts the period from credit +112, adds elapsed time, then clamps
  excess credit to one period. This applies to render and sound.
- Mode 2 clamps elapsed delta to +88. None of these four constructors selects
  mode 2. Preserving +88 does not mean it caps mode-1 deltas.

All branches convert +120 to seconds at +132 and reset +104.
`82720858` adds +120 to cumulative scheduler time +80, increments +128,
and computes statistics including +168 (`.76.cpp:26339`). Thus period changes
can affect startup timing, credit/catch-up behavior and consumers of scheduler
time, even if the hook writes only +72. The existing render hook leaves its
constructor seed +120/+132 and all other direct writes intact.

## Fourth peer: concrete sound dependencies

The sound path is `821781F8` → `821663E8` → scheduler callback `82166468`
→ group at `*(8291132C)` → `8271ED68` → selected graph work. The group
constructor `828DAC28`, its name method `821497A8`, and the image vtable/string
above establish its sound identity without assuming that any mode-1 object
is a presentation scheduler.

There are concrete consumers beyond the shared loop:

- `82149838` reads the sound scheduler via `8296D8E4`, reads cumulative +80,
  and passes it to `8234AFE0`; source `.52.cpp:1165`. This method occupies
  +20 of the `820018D8` descriptor vtable (`FSGameSoundStartup`), one of the
  graph descriptors associated with the sound group.
- `82335000` reads this peer's nominal rate +60 and uses it in timing/ramp
  arithmetic; source `.124.cpp:10045`.
- `8233A5E0` reads its cumulative +80 and converts it to floating time;
  source `.132.cpp:9563`.

This does not trace every dynamically selected graph node or prove the exact
runtime audio workload. It supplies direct reasons to preserve the 60 Hz sound
cadence instead of extending the render patch to every variable-step peer.

## Change and verification

Only `tests/native/test_guest_render_scheduler.cpp` and this document change.
No new production hook or diagnostic option was necessary. Three new cases:

- Exercise all four source-identified peer layouts at targets 0, 60, and 120,
  require rejection without any writes for core/SIM/sound, and require only
  bytes +72..79 to change for the admitted render object.
- Give each non-render vtable render-like mode/rate/period fields and require
  rejection, isolating class identity from incidental timing matches.
- Reapply the policy after the period was changed and require no retargeting
  or state writes, preserving its constructor-only admission contract.

Built only the standalone scheduler test translation unit plus the tiny existing
test main using project clang++/C++20: **6 tests, 0 failed checks**. A temporary
header copy with just the vtable guard removed produced **8 failed checks**,
including all three non-render identity probes; production files were never
mutated. Re-ran the unmodified-policy binary successfully. These are regression
tests of existing behavior, not a claim of TDD for a new pacing implementation.
`git diff --check` for the touched files passed.

The full native suite/build is left to the parent's integration validation,
because other workers are editing its sources/CMake. These tests do not execute
generated PPC, start threads, check graph synchronization, measure images/FPS,
or validate gameplay/audio at higher rates. The mode-0 distinction is established
by source, not by simulating a second copy of that algorithm in the tests.

## Remaining evidence before another unlock

One render presentation gate is confirmed and already addressed by the existing
hook. Two identified peers are fixed-step core/SIM; the fourth is variable-step
sound. None of those three is proven to be an additional removable presentation
throttle. Removing all their gates safely would require proving callback/graph
dependencies and preserving simulation/audio time contracts, not replacing 1/30.

A coordinated future runtime audit should observe all four objects and their
selected graphs, callback readiness/blocked time, deltas, cumulative time and
unique rendered frames in the same stable gameplay scene. A low observed frame
rate alone cannot distinguish necessary peer synchronization, polling overhead,
simulation cadence, or CPU/GPU work. No extra limiter removal or performance
gain is claimed by this audit.
