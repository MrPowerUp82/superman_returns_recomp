# Vulkan CPU budget implementation plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task by task. Complete a fresh context review at the end.

**Goal:** Reach the game's 30 FPS cap in native Vulkan without reducing image quality.

**Architecture:** Preserve the existing capture, replay, recording and GPU owners. Transfer a draw's owned payload through the neutral packet queue by moving its ownership instead of copying its inline arrays. Use measured CPU function samples to select subsequent changes; do not assume that the residual game timeline represents pure guest computation.

**Tech Stack:** C++23, Clang, CMake/Ninja, Vulkan, Windows process sampling, existing native and GPU fixtures.

**Spec:** ../specs/2026-10-08-native-renderer-cpu-gpu-bottlenecks-design.md (including its texture policy update). Current hardware is i7-10750H / RTX 2060; this task validates on this machine.

## Global constraints

- Preserve command ordering, constants, resource versions and resource lifetimes.
- Keep one owner for mutable GPU state; no parallel command recording.
- Native mode never falls back to Xenos.
- Keep the existing texture watch/backoff policy and rendering settings.
- Do not claim 30 FPS before two completed gameplay measurements substantiate it.

## Review focus

- Queue growth and producer reuse must not relocate or overwrite an owned draw payload.
- Captured memory and shader/texture references must survive source batch destruction.
- Decode failure must preserve the previously published packet.
- A default empty draw owner must produce an explicit renderer error, not a crash or silent success.
- Small non-draw packets must retain their current ordering and behavior.

## Task 1: Remove inline draw relocation

- [x] Add a native regression for payload identity across packet movement and queue growth, including captured memory and shader lifetime.
- [x] Observe the regression fail with the current inline variant.
- [x] Store draw payloads behind exclusive ownership in RenderPacket; retain value construction for existing fixture callers and use GetDraw for reading.
- [x] Decode directly into the owned draw and publish only after successful validation.
- [x] Verify native tests, allocation budget, Vulkan tests, production bindings and GPU fixtures including async frame lifetime.
- [x] Compare gameplay stages and image gate against the saved current baseline. This comparison does not establish a gameplay FPS improvement.

## Task 2: Select the remaining limiting work from evidence

- [x] Build an optimized binary with a linker map and sample the busiest process threads in gameplay.
- [x] Distinguish guest/XDK work, replay work, recording work and waits.
- [x] Document the next bounded fix and add a relevant regression before implementing it.
- [ ] Repeat measurement after each retained change; keep only validated improvements.

## Task 3: Validate the 30 FPS target

- [ ] Run two consecutive default Vulkan gameplay benchmarks, idle and forward.
- [ ] Verify image/readback equivalence and D3D12 sanity.
- [ ] Obtain an independent code review and address findings.
- [ ] Record achieved FPS and remaining limitations honestly.

## Execution ledger

- ThinLTO completed: `cpu30_thin_lto1` 13.6/13.7 FPS; opt-in yield/XDK combination 12.6/13.4; without instrumentation `cpu30_thin_clean1` 14.2/13.9. All reached valid HUD gameplay. Image gates passed, but no demonstrated FPS gain. User confirms AC power and performance mode; do not attribute CPU costs to battery without evidence.
- Full-stack diagnostic: 7,424 samples (116 per thread) over 12 seconds in valid gameplay. StackWalk sampling is intrusive; exclude the run's FPS from clean performance acceptance. Guest graphics/capture, replay parser/initialization and Vulkan Draw/BuildBindings remain active costs. Lazy arena construction prototype saves only ~0.3 us/draw; not integrated.
- Sequential ALU PM4 writes: performance diagnostic initially failed the <=2x direct-conversion budget (2.85x). Group endian conversion/comparison and aggregate exact version changes per VS/PS bank; use size_t source indexing to allow vectorization. Final budget 1.02x, 168 native tests including opcode/bank boundary equivalence pass (also ThinLTO), 88 Vulkan tests and three pixel contracts pass. Review found no actionable issues. Release SHA256 D1477DF5514CA57650E4A5C81D0C7F4CDF136882CB0C700C7F425A06F275761E; `cpu30_pm4_clean1` 13.2/13.3 FPS, start gate passes (40.37 dB, histogram .00444). Isolated speedup does not establish gameplay gain.
- Next bounded experiment: capture overlays force the bulk fetch read to fall back even when later duplicate bytes equal the complete earlier bank. Retain range ordering and only return the complete bulk span after proving all newer partial overlay intersections byte-identical. Changed overlays retain the old scalar path. Regression written first; await completion of the active game measurement before building tests.
- Overlay experiment validated for correctness: regression failed 0 vs768 first, then 169 native tests, 88 Vulkan tests, three pixel contracts and frozen replay allocation budget passed. Review found no issues; clipped intersections/invalid offsets/overflow retain safety. Isolated fetch-bank reads 0.3–0.5 us versus forced scalar 2.5–3.1 us, checksums match. Release SHA256 72686519AFD69D702DCC853B5B2DB87B0144DEC4A4FE15B8E33B177607596336; clean gameplay 15.2/14.6 FPS, start gate passes (37.96 dB, .00948 histogram). Need repeat measurement; 30 remains unmet.
- Added build-directory ThinLTO cache after exact LLD link probe; option remains default off. Next diagnostic counts consecutive unchanged VS/PS mirror versions under profiling only, to assess constant-bank reuse before touching uploads/descriptor offsets. Do not run heavy builds during gameplay measurements.

- Latest status: the 30 FPS requirement remains unmet. Do not interpret completed correctness tests as performance acceptance.
- Idle-yield experiment: opt-in reduced aggregate CPU consumption but gameplay remained 13.7/13.5 FPS. Default remains disabled.
- Bulk register/fetch decoding preserves partial capture overlays and PM4 precedence. Native regressions pass; gameplay benefit is not established.
- Frozen capture batches: replay borrows immutable batch bytes with shared lifetime and pooled recycling. Production replay allocation budget is zero capture allocations/bytes for 32 draws; native, Vulkan binding and GPU readback tests pass. `cpu30_frozen1` measured 12.0/12.4 FPS, so this does not establish an end-to-end gain.
- XDK constant prefix experiment: 8,224 comparisons against the generated guest functions matched complete PPC contexts and memory. Opt-in gameplay measured 11.8/8.4 FPS, with multiple CPU stages slower; default remains disabled and the cause of the slower run remains uncertain.
- Raw Vulkan vertex identity experiment rejected and removed before benchmarking: review confirmed ReplayCapturedRenderPacket converts vertex endian using stream/layout metadata. Sharing allocation identity across layouts would reuse converted bytes incorrectly. The helper identity test did not cover this production contract; it was removed with the experiment.
- Benchmark interruption: a Windows Firewall modal from the renamed saved executable intercepted benchmark input. `cpu30_before_now` and `cpu30_baseline_same_name` did not reach valid gameplay sampling and are discarded. Await dismissal with Cancel before further UI benchmarks; use the normal executable name for future comparisons.
- Follow-up review: removed the raw vertex identity experiment and the unproven staged shared-constant copy. Fresh native run: 166 tests, zero failed checks. Release build completed; executable SHA256 `D58D0ADD01766B830E991C452C62B466B9FA98C547BA5B35F4DE05C8E9E87F71`. Production bindings GPU contract variants 0/1/2 passed pixel readbacks; validation layers unavailable. No new gameplay FPS measurement is valid yet.
- After Firewall dismissal, valid gameplay: `cpu30_reviewed1` idle/forward 12.4/12.3 FPS; saved baseline `cpu30_before_paired1` 14.5/14.5 FPS. Gameplay draw counts varied, so these runs do not isolate the effect of ownership. Last 300 rows: candidate game/frontend/worker/record/GPU 76.52/35.41/56.13/58.75/9.59 ms; baseline 66.26/29.70/53.59/49.55/9.18 ms. Replay per packet is lower with arenas/frozen capture and sink cost falls from ~11 ms to <0.1 ms, but overall FPS does not improve in this pair. Start-image gate passes (39.59 dB, histogram 0.00631).
- Cached-coherent transient memory experiment rejected and removed: supported RTX memory flags 14 vs normal 6; selection regression failed before preference and passed after; three GPU constant contracts passed. Gameplay `cpu30_cached1` 13.6/12.5 FPS. Isolated production BuildBindings cost: normal 1.079/0.981 us per draw versus cached 1.693/1.409 us per draw, including reversed run order. Keep normal coherent upload policy.
- Contiguous missing-fetch fill experiment rejected and removed: 88 Vulkan tests and all three GPU constant contracts passed, but with 3,000 distinct draw payloads the isolated bindings cost was 2.291 us per draw normally versus 2.354 us with fill. Retain only the diagnostic benchmark and actual memory-property reporting.
- Next experiment: opt-in Release ThinLTO across generated guest code and project native/Vulkan code. The measured guest work alone is 36–41 ms/frame; crossing translation-unit boundaries may remove context/register and helper call overhead. Preserve instruction baseline and floating-point policy (no AVX/FMA/fast-math changes), limit LTO jobs, preserve a runnable baseline, and validate gameplay/image gates before accepting it.
- ThinLTO validation so far: exact LLD link probe accepted the two-job option; native tests compiled with -O3/-flto=thin pass (166 tests), capture budget passes, and all 8,224 generated-XDK comparisons preserve complete PPC context and memory. Game build and gameplay acceptance remain pending. Reviewer found no actionable build-configuration or diagnostic issues; option remains off by default.

- Baseline: `cpu_fix_final2`, idle 13.5 FPS, forward 14.0 FPS; last 300 timeline rows: interval 69.29 ms, frontend 31.00 ms, worker 57.44 ms, recording 51.99 ms, GPU 10.16 ms.
- Replay profiling: 34–41 ms/frame rebuilding packets, 10–13 ms/frame handing non-swap packets to the sink. Inline packet arrays are copied by packet movement.
- Diagnostic setup: WPR CPU collection failed with 0xc5585011. Use a bounded RIP sampler, with unconditional thread resume and optimized linker-map symbols. A failed compiler reconfiguration briefly left empty Release flags; restore -O3/-DNDEBUG before any measurement. The preserved original binary's .text was 49.3 MB, versus 107.6 MB in the unoptimized diagnostic build; the unoptimized build is not a benchmark candidate.
- Ruling: Continue the already requested optimization without a new approval pause. The existing design and user's explicit 30 FPS instruction authorize internal reversible changes; retain all rendering contracts.
- Ownership experiment: individual 25,928-byte draw allocations removed sink copies but increased replay time (57–80 ms/frame) and regressed gameplay. Replace them with one arena per captured batch. With arenas, sink remains below 0.1 ms and replay returns to 36 ms/frame; gameplay is still about 14 FPS, so ownership alone does not meet the goal.
- Task 2 next bounded experiment: samples identify several guest threads repeatedly polling through the verified `sub_828A7978 -> NtYieldExecution -> SwitchToThread` path and engine worker lock/timer loops. Introduce a per-thread backoff only after eight yields less than 1 ms apart. First yields and non-native mode retain the original behavior. Request a 250 us high-resolution wait (fallback at most a requested 1 ms), retain the original guest return/register state, and measure with an opt-in environment switch before selecting a default. Do not bypass work or change any render data.

- 2026-10-09 bank diagnostic: cpu30_bank_profile1 14.8 idle /12.1 forward FPS; last300 game76.53, guest42.81, frontend33.72ms. About30% VS and60% PS versions repeat. User confirms AC and performance mode. No power-setting changes.
- Exact bank reuse experiment: opt-in SR_VULKAN_CONSTANT_BANK_CACHE, bounded last-bank host copies, byte comparison, same submission/chunk only, explicit BeginSubmission reset. Keep12KB reservations and descriptor bases; redirect dynamic offsets for repeated banks. TDD stub failed9checks, implementation passes90Vulkan tests. Three production shader ABI GPU readbacks pass VS-only/PS-only/both reuse; GameRenderer normal/merged fixtures pass. Validation layers unavailable.
- Isolated3000-draw benchmark with30%VS/60%PS repeats: normal3.348/3.416us versus cached3.132/3.207us. Small CPU saving (~0.6ms for3000draws), no gameplay gain established. Game build and review pending, feature remains off by default.
- Bank gameplay acceptance failed: ON 11.2/11.6 FPS, same EXE OFF 12.4/11.8 FPS. Gate passes26.57dB/.01428; no game speedup established, cache remains OFF by default. Expanded GPU contract restores shader-consumed VS depth and PS color independently before both-bank reuse; all3readbacks pass.
- Next diagnostic: SR_NATIVE_GUEST_CPU_PROFILE=1, seven exact generated function wrappers from stack sample, always execute original. TLS bounded counters, inclusive plus self excluding only tracked child scopes. No guest context/memory changes. This is timing instrumentation, not a behavioral optimization; default OFF. Build/review pending.
- First function diagnostic build passed (4A0F08C536B8E128D0851CE8CBDDC19E49945C9FC7F3AA77F3D500598EC0D1C7); gameplay cpu30_guest_functions1 11.7/11.5FPS with instrumentation. Last300 game80.95, guest45.85, frontend35.10, worker56.50, record65.07, GPU7.19ms. Highest inclusive functions82783948 and82316958; flush821078C0 comparatively small. Values include waiting and native capture, not pure CPU.
- Fresh bounded stack2 sample during gameplay:8064samples,126/thread,64threads; every suspended thread resumed. FPS from this intrusive run excluded from clean acceptance. Matching executable MAP saved logs/cpu30_guestprofile.map and module bases cpu30_stack2_modules.json. Dispatcher82783948 child sample counts:8230F8E0=17,827838D0=10,82300328=9,822E9E68/82307CF8/823103C8=7each;82316958 child822F5290=26/32. Nearest private MAP names remain approximate.
- Extended opt-in profiler to those seven children (14 bounded counters per thread). Build passes, gameplay diagnostic pending. No guest behavior or settings changed; default switches remain off.30FPS unmet.

- 2026-10-10 register experiments authorized by user: isolated CTR passes 8,192 differential cases and all 33,229 generated bodies match after reversing CTR localization. Clean references 11.6/11.1, 12.3/11.8, 11.7/11.2 FPS; CTR 12.7/12.8 and 13.2/12.8. First image gate passes, second fails character chroma .053>.05; same executable captures also vary, which does not establish acceptance. CR alone passes 32,768 differential cases and generated-body comparison; build passes, clean 11.7/11.5 FPS, gate passes33.1dB/.024. No useful CR gain. Restore baseline manifest/generated code/executable; leave localization flags false.
- Confirmed SDK blocker: non_argument localization resets helper-local r12 instead of transporting caller hidden arguments; __savegprlr_29 would save zero LR, __savevmx_110 would access wrong address. Do not enable it alone without correcting generator contracts.
- Capture-aware diagnostic cpu30_capture_cpu1: 13.1/12.1 FPS instrumented, excluded from clean acceptance; image gate passes38.7dB/.007. Native capture44.43% main elapsed time; after subtracting tracked native capture/children,822F5290self10.72%,8230F8E0self7.99%,82783948self7.41%. OS CPU sampler without suspension:42complete intervals43.27s, main.972core,Vulkan owner.891core,replay.697core. Confirms real CPU work, not merely elapsed scheduling delays. Next target is native capture plus replay/recording.30FPS remains unmet.

- 2026-10-13 bounded experiment: repeated TextureResourceId FNV calculation in texture upload and descriptor remapping. TLS128-entry cache of pure IDs only, full6word equality, versions/handles/lifetimes untouched, opt-in SR_VULKAN_TEXTURE_ID_CACHE default OFF. Regression stub failed10001checks; implementation91Vulkan tests0failures, randomized10000fetches and single-word changes. Isolated300000draws32slots1500fetches, reversed order: original.999/1.058us versus cache.311/.415us per draw, equal checksums. Review no actionable findings. Clean current baseline10.0/10.8FPS, image gate26.5dB/.016 pass. Game build and same-binary A/B pending;30FPS unmet.

- Texture ID game acceptance rejected: same EXE OFF11.4/11.4 versus ON11.0/10.9FPS, both image gates pass. Removed experimental production cache/helper/test and rebuilt baseline9A9ED5E346AED1FBFC7EAE1F38B660D80B3866C15899214C0D500648A70353F0.90Vulkan tests and3production shader pixel contracts pass after removal. No gain retained.
- Fresh yield check same restored EXE: ON13.4/13.9FPS with4.4/4.1cores; OFF14.3/13.7FPS with7.2/7.1cores. Both image gates pass. Reduced CPU load does not establish improved FPS; keep OFF default. Current measurements fluctuate enough that earlier references cannot establish gains. SDK emitted LLVM helper alias has no noinline attribute on the generated body, so do not assume that macro blocks all ThinLTO inlining.
- Added timeline-detail-only physical write notification report using the existing completion counter, once per120swaps; no extra atomics in the write callback. Goal: distinguish costly memory protection notifications from other kernel activity before attempting an exact per-use hash alternative for small buffers. Build/gameplay measurement pending.

- Physical notification diagnostic: gameplay139–204events/frame (boot361), image gate26.5dB/.016passes, instrumented12.3/12.1FPS excluded from clean acceptance. New bounded hypothesis: small buffers (<=32KiB, existing hash bound) use a complete hash on every refresh and do not arm new physical page watches; preserve textures/large buffers and never unprotect shared pages. Opt-inSR_NATIVE_SMALL_BUFFER_HASH_EVERY_USE defaultOFF. TDD intra-frame unnotified-write regression failed5checks first;170native tests pass after implementation. Review found no new invalidation/lifetime issue; hash/copy remains nonatomic as before. Build and paired clean gameplay pending.

- Small-buffer eager hashing rejected for FPS: same EXE2C608CD986A9ED634D76B02EAA6EE022A7AD90010060FF40A949E3A249653E6E ON8.5/10.1 versusOFF12.6/12.4FPS, both image gatespass26.4/.021and26.5/.018. KeepflagOFF during next experiment so only generated calls change. OS GPU usage sample overflowed in OFF idle; exclude that usage value, FPS/HUD/image tests remain valid.
- Major codegen hypothesis: separate-TU Clang23 ThinLTO probe retains weak alias call/materialized state even without noinline; direct original calls inline/fold the same integer computation (all variants output18). MAP current32679public/original subpairs:32643sameVA,36overridden. New tool direct_original_calls.py verifies paired PE/MAP timestamps, preserves full baseline/exe/MAP before apply, rewrites static(ctx,base) calls only, lexical skips and inverse source audit. Conservative closed integer graph excludes FP/vector state, imports/unknown/indirect/MMIO/SEH/setjmp/longjmp, and preserves public hooks/function mappings.5tests pass after red-green regressions, reviewP2backup-inside-source-tree fixed.32768real save/restgpr14chains underThinLTO preserve fullPPCcontext,8192bytes andhostMXCSR. First scope9576targets52010calls in143units built; five MMIO targets plus parents are being removed before gameplay with an incremental rebuild. Baseline archive logs/cpu30_direct_calls_baseline_20261013.30FPS remains unmet; no game gain claimed.

- Final direct-original candidate scope: 9,571 closed integer targets, no MMIO or unknown/FP callees; 52,006 static calls changed in 143 units. Five MMIO functions removed before gameplay (four incremental units). Build passed, candidate SHA6C2A6E3E88A4F73D00B6A350B74855F7BECDEB25F0E7B6BB74A9D3B0C4C81CB9. Clean candidate12.8/13.0 and14.5/13.8FPS, both image gates pass26.5dB/.018 and26.2dB/.035. Saved baseline14.0/13.4FPS, gate26.4dB/.026 pass. Baseline2 is excluded because source restoration/audit overlapped its forward measurement; baseline3 rerun completed without concurrent work. No stable gain established; all generated files restored byte-exact to archive, modified files written with fresh timestamps to avoid stale candidate objects on the next build. Normal executable restored2C608CD986A9ED634D76B02EAA6EE022A7AD90010060FF40A949E3A249653E6E. Generator experiment is not wired into CMake. Existing eager-small-buffer and idle-yield options remain defaultOFF; physical-write report and user/kernel CPU sampler retained as diagnostic tools.30FPS still unmet.
Baseline3 clean rerun:12.5 idle/13.4 forwardFPS, image gate20.9dB/.039passes. No30FPS claim.
