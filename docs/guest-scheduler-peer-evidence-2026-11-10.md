# SIM scheduler unlock experiment (2026-11-10)

The fork Superman-Returns-PC (`port/src/frame_rate.cpp`) gives both 30 Hz
peers, render and SIM, the period of the FPS limit. This port only changes
render. To test the SIM half, `sr_native_guest_sim_scheduler` (experimental,
off by default) sets the SIM peer's +72 in a pre-hook on its start
`82720590`, before +104 is seeded; `ApplyGuestSimSchedulerPeriod` admits only
vtable `82005BC4`, mode 0, rate 30, period 33333334 and targets 31..240.
`sr_native_guest_scheduler_log` logs updates/s and game time / wall time of
both peers every 10 s.

Measured with `tools/bench/bench.ps1`, D3D12, `--sr_native_fps_limit=60
--vsync=false`, one run each:

| Run | idle avg/min FPS | forward avg/min FPS | SIM updates/s | SIM game/wall |
| --- | --- | --- | --- | --- |
| `sim_off_60` | 26.0 / 21.5 | 25.9 / 23.7 | 30.0 | 1.000 |
| `sim_on_60` | 22.2 / 18.1 | 24.6 / 20.2 | 60.0 | 1.000 |

SIM at 60 Hz kept game time equal to wall time (no slow motion), but render
fell by 1-4 FPS: the render peer runs at ~22-26 updates/s, below SIM's 30, so
the SIM period was not the limit on this machine and doubling SIM work took
CPU from rendering. The GPU sat near 31%. The limit is CPU work in the render
path, not a guest scheduler period. The option stays off.
