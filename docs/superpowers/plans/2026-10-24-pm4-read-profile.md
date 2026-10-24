# PM4 constant memory reads: diagnostic implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure the live LOAD_ALU_CONSTANT readability checks before choosing an optimization.

**Architecture:** Add per-mirror, opt-in counters and elapsed time around the existing live-source validation. Keep the existing VirtualQuery checks, register writes and captured-reader path. Publish raw totals in the existing 120-swap worker report, only with both CPU and PM4 profiling enabled.

**Tech Stack:** C++20, existing Windows VirtualQuery, native tests, D3D12 worker logging.

**Spec:** `docs/superpowers/specs/2026-10-23-native-renderer-parity-design.md`, CPU milestone. Updated evidence: user's private-fork screenshot and `logs/renderer_focus_20261024_base1*`.

## Global Constraints

- D3D12 first; preserve renderer state, memory checks, shaders and images.
- No blind changes to 1/30 constants; no claims of 60 FPS from host repeats.
- Profile Off by default; no new clocks or counters when disabled.
- Show measured data before behavior changes. This increment is diagnostic only.
- Reuse the current approved `codex/native-parity-d3d12` checkout and experimental build; preserve stock Release and do not interrupt a manual game session.

## Review Focus

- Failed live reads must still skip the complete ALU load without changing registers/versions.
- An owned/captured reader must not invoke or be counted as a live VirtualQuery check.
- Multi-region reads count every attempted query, including the failed region.
- Profiling must stay disabled by default and require both environment flags in the renderer.
- Timers must exclude WriteRange and remain nested in Execute; 120-swap publications reset only interval totals.

### Task 1: Account for live ALU checks

**Files:** `port/src/native_renderer/pm4_mirror.h`, `pm4_mirror.cpp`, `tests/native/test_pm4_mirror.cpp`.

- [x] Add an inert profile interface and tests for enabled live checks, disabled/custom-reader checks, and failure spanning readable/no-access regions.
- [x] Run native tests and observe missing counters fail, with constant state still correct.
- [x] Implement counters/query accounting and a clock around the existing HostReadable call only.
- [x] Run the native suite and CTest; verify all existing PM4 contracts.

### Task 2: Publish and measure

**Files:** `port/src/native_renderer/native_renderer.cpp`, `docs/d3d12-pm4-read-profile-2026-10-24.md`.

- [x] Gate with `SR_NATIVE_PM4_READ_PROFILE=1` and CPU profiling; log effective state.
- [x] Publish calls/bytes/queries/failures/ns for 120 swaps alongside worker timers.
- [x] Build the experimental game; run a diagnostic New Game window without concurrent builds/tests.
- [x] Analyze complete blocks inside the measured window, cross-check totals, record scene/gate limitations and compare with the four external claims.
- [x] Obtain a fresh read-only review and resolve actionable findings; keep behavioral optimization conditional on these results.

### Task 3: Conditional bounded ALU reads

Ruling: the live PM4 probe measured approximately 680 queries/frame and 7.3–7.9 ms/frame of validation in recent blocks. The user was shown these figures before this task. Reuse CheckedGuestReads/ReadProcessMemory only for live ALU sources, behind `sr_native_pm4_bounded_reads=false`; do not introduce a persistent readability cache or bypass inaccessible pages. This extends the diagnostic plan into the measured CPU milestone of the approved design.

**Files:** `pm4_mirror.h`, `pm4_mirror.cpp`, `native_renderer.cpp`, `test_pm4_mirror.cpp`.

- [x] Add an inert bounded-read option and RED tests: identical register/version results, refreshed bytes at the same address, no VirtualQuery in bounded mode, zero length, inaccessible/partial read rejection and custom-reader isolation.
- [x] Reuse per-thread checked scratch for one ALU load at a time; consume the owned span synchronously in WriteRange, preserving the existing live/default and owned-reader branches.
- [x] Run native tests and CTest; build the game.
- [x] Compare Off/On with identical binary/runtime, options and diagnostic flags; report raw read counts, ns, failures, worker/GPU and scene limitations. No performance claim from unmatched cameras/cinematics.
- [x] Request fresh review of the complete optimization and document remaining work; keep the option Off by default.
