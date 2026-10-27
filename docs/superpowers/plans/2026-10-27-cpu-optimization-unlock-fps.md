# CPU optimization and FPS unlock implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Apply the screenshot CPU advice to the existing renderer and remove the default 30 FPS limit.

**Architecture:** Retain the existing CPU staging, bounded PM4 copies, complete resolve identities and conservative texture write proofs. Make the normal build supply the verified tracking runtime needed by those proofs. Investigate guest pacing before patching it; never replace simulation deltas or unrelated 1/30 constants speculatively.

**Tech Stack:** C++23, CMake/Ninja/Clang, PowerShell, WPF launcher, ReXGlue v0.10.0.

**Spec:** User request and supplied screenshots, with existing implementation described in `docs/d3d12-cpu-bundle-2026-10-24.md`.

## Global constraints

- Work in the current checkout on `codex/cpu-optimization-unlock-fps` so existing local game assets and toolchain remain available.
- Preserve explicit FPS choices and texture validation on unknown or invalidated write coverage.
- Do not manually edit generated guest code or claim measured FPS gains without gameplay measurements.
- Preserve SDK pinning, patch/source verification and conservative resource lifetimes.

## Review focus

- Unknown texture coverage still hashes; an unavailable runtime must not silently imply full optimization.
- Uncapped presentation must not arbitrarily alter simulation time steps.
- PM4 invalid, partial and protected memory reads fail safely, including nested reads.
- Saved user settings and explicit command-line overrides retain their meaning.
- No performance claim is inferred from a comment about another fork.

### Task 1: Tracking runtime in the normal build

- [x] Verify the pinned patched SDK and preserve a reference executable/runtime.
- [x] Make `build.cmd` select tracking by default, preparing it through the existing safe setup script. Keep an explicit OFF escape hatch.
- [x] Run SDK setup regression checks and a release build; confirm runtime tracking in gameplay.

### Task 2: Frame pacing

- [x] Trace vblank and guest timing paths; add focused regression coverage before behavioral changes.
- [x] Remove the default 30 FPS cap through the executable, launcher and launch scripts.
- [x] Modify only confirmed timing behavior: port Canary GPU vblank policy and advancing scanline counter, retaining simulation deltas and explicit limits. No unverified guest-loop patch was applied.

### Task 3: Audit and validation

- [x] Audit bounded PM4 reads and renderer CPU paths against the screenshot advice; change only remaining proven defects.
- [x] Run native, Vulkan and launcher suites plus the affected tooling checks.
- [x] Review the integrated diff and run a gameplay smoke/benchmark without concurrent compilation.
- [x] Document the effective paths, measured results and unresolved guest limiter evidence in `docs/xenia-canary-fps-unlock-2026-10-27.md`.

## Canary continuation

- Official source pinned at `72c2afa3ba2b1b232f89de71577251d6edafb1bd`.
- Native unbounded mode now follows `MarkVblank` + 1 ms sleep with VSync off;
  MMIO scanline progress no longer returns a constant height.
- Source-derived implementation is validated; measured gameplay remains below
  30 FPS. Full guest limiter removal and correct simulation at 60 remain unproven.
- REA skill followed its source-analysis route. Native provider unavailable;
  no provider/client setup was performed and no REA decompilation is claimed.

## REA runtime continuation

- [x] Reuse the newly connected REA 6.8.0 server; native Ghidra is now available.
- [x] Inspect current/reference runtime exports and retain Evidence bundles.
- [x] Reject generic NtWait/NtDelay hooks after interference; unload, close,
  terminate only the owned diagnostic process and preserve the failed capture.
- [x] Observe per-thread CPU accounting without function hooks and recover the
  PE guest-to-host table (33,445 source-matching entries).
- [x] Confirm the internal FGGameRender scheduler's mode 1, 30 Hz and
  33,333,334 ns minimum period in 55 live snapshots.
- [x] Test and implement a guarded post-constructor period change, preserving
  the distinct fixed-step SIM peer and the elapsed-time delta algorithm.
- [x] Review byte isolation, original-call/context behavior and prepublication.
- [ ] Validate live startup/remainder/delta consumer behavior with the new period.
- [ ] Complete gameplay smoke and record its limits; do not treat startup FPS
  or unpaired city views as a controlled performance comparison.

The period also seeds startup timing and participates in remainder accounting.
Only direct writes are byte-isolated; resulting deltas are not promised to be
identical. The rejected title A/B includes loading and cannot establish a gain.
