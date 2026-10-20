# Vulkan texture binding reuse — 2026-10-20

Goal remains 60 real gameplay FPS, native Vulkan, scale 1, MSAA 1, no frame
generation. This iteration has not demonstrated 60 FPS.

## Diagnosis

Two 20-second CPU samples of preserved executable 6C0C2CD6 showed recorder
time in copies and resource lookups. Leaf samples are not inclusive profiles;
raw stack words are not unwound frames. Nearest DLL export names were
misleading: VCRUNTIME RVAs 0x1ccab/0x1ccf4/0x1cd26/0x1cd2e are an AVX copy
body, not `__NLG_Return2`. The initially inspected fill body at 0x1e6b0 was
the wrong offset and does not support a memset diagnosis.

Added six elapsed-CPU phase counters gated by existing `SR_VULKAN_PROFILE`.
With profiling disabled they do not call the clock. Preserved instrumented
baseline executable: F9B48306C2E6F62A8ADB8F632FC8252F38BB33FF9014F7D44E15BCF4C24F5CA0.

Observed gameplay baseline report: 2851 draws, bindings 34.1 ms; layout 1.9,
streams 5.3, pages 0.6, constants 9.2, textures 15.1, finish 1.5 ms. Another
report: 2735 draws, texture preparation 13.7 ms. These are rounded elapsed
times and include profiling overhead. They do not establish descriptor API
calls as the dominant cause.

## Change and safeguards

Keep two recent complete texture fetch descriptions per guest slot. A reuse
still looks up the current ResourceStore entry and compares its resource
identity. A changed upload/resolve version requires preparation again. The
heap retains acquired versions through the submission lifetime, preventing
cached pointer reuse. Reset the cache each BeginSubmission. Full fetch
comparison includes sampler, dimension, and gamma state.

`SR_VULKAN_TEXTURE_BINDING_CACHE=0` disables preparation reuse for A/B. Both
paths use the existing non-owning FindTexture lookup; heap acquisition
provides submission retention. Thus this switch isolates cache reuse and
does not recreate every instruction of the older Texture lookup path.

Candidate executable:
`D3DBDACAE8EA97FD45BA4DC5F29C45CCA97A316D38E05C2E30DBF8080EDFD9DF`.
Serial replay and 32 commands per batch remain the production settings.

## Correctness evidence

- Red GPU executable failed `Repeated textures exceeded preparation budget`
  before cache implementation. An earlier `--require-validation` invocation
  lacked VK_LAYER_PATH and failed setup; it is not the regression proof.
- 108 Vulkan unit tests passed.
- Full production global GPU contract passed with validation_active=1 and
  validation_errors=0, comparing pixels with the portable renderer.
- Repeated 3000-draw fixture: 2 texture preparations and 2998 reuses.
- Same-ID replacement fixture: 258 preparations, zero stale reuses.
- Existing two-pending-version, draw-order, indexed, IA and layout tests pass.
- Fresh code review reported no concrete findings.

## Gameplay observations

Profiled candidate reports: 2904 draws, texture preparation 6.3 ms,
3445 preparations / 28509 reuses; 2771 draws, 4.6 ms,
3347 preparations / 27135 reuses. About 89% of preparation work is reused.
Compared with the nearby baseline draw counts this is a substantial CPU
subphase reduction; scenes and scheduling are not identical.

The first 20-second profiled candidate window averaged 11.97 FPS. The
profiled baseline window averaged 12.21 FPS. Neither is an isolated causal
FPS comparison: initial loading, camera motion, shader warming and scene
draw counts differ.

Clean A/B used the same D3DBDACA executable, global mode, serial replay,
32-command batches, no profiling/timeline, scale 1, MSAA 1, LSFG off,
cap 60, logical 1280x720 / physical 1600x900. No builds, CPU sampling or
GPU tests ran during these windows. Each window is approximately 20 seconds.
The diagnostic helper now saves each raw log window and separately parses
guest FPS and native Vulkan real_fps; repeated presentations are excluded.

| Mode | Scenario | Guest FPS | Vulkan real FPS |
|---|---|---:|---:|
| Cache disabled | Idle | 13.08 | 13.129 |
| Cache enabled | Idle | 13.03 | 13.025 |
| Cache disabled | Held forward | 12.28 | 12.305 |
| Cache enabled | Held forward | 12.32 | 12.490 |

This does **not** demonstrate a meaningful overall FPS improvement. Idle
cameras orbit; held-forward runs reach a building and are not sustained free
movement or identical rendered pixels. Each mode has one pair of windows;
these observations are not a statistical speedup claim. The transient HUD
transition in one movement capture was not compared at identical game time;
GPU fixture equivalence does not prove every gameplay pixel is identical.

The profiled 240-frame gameplay timeline reports mean busy times: game
98.01 ms, recorder 56.90 ms, replay worker 46.26 ms, native frontend
43.56 ms, GPU real 14.87 ms. These stages overlap and **must not be added**;
the game interval includes frontend work. GPU idle mean was 83.02 ms.
This explains why reducing one binding subphase does not establish faster
frame delivery. Further work must examine the CPU capture/replay/guest
critical path, while constants remain the largest measured binding subphase
after reuse. A different Vulkan feature flag alone is not supported as the
next cure by these measurements.

The reduction in redundant texture preparation is kept enabled; the clean
comparison shows no material FPS change in either direction. The A/B switch
remains available. Target 60 FPS remains unmet.

Final negative control: the fully built candidate with cache disabled and
validation configured failed the preparation-budget assertion again. Thus
the regression test detects disabling the actual reuse path.

## Evidence locations

Logs start with `native60_bindings_phases_20261020`,
`native60_texture_reuse_`, and `native60_texture_cache_` under `logs/`.
Preserved baseline executables/MAPs are under
`artifacts/native-renderer-performance/bindings_*_before.*`.
