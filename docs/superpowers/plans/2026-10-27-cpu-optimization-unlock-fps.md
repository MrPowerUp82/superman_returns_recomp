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

- [ ] Verify the pinned patched SDK and preserve a reference executable/runtime.
- [ ] Make `build.cmd` select tracking by default, preparing it through the existing safe setup script. Keep an explicit OFF escape hatch.
- [ ] Run SDK setup regression checks and a release build; confirm runtime tracking in gameplay.

### Task 2: Frame pacing

- [ ] Trace vblank and guest timing paths; add focused regression coverage before behavioral changes.
- [ ] Remove the default 30 FPS cap through the executable, launcher and launch scripts.
- [ ] Modify only confirmed guest frame waits, retaining simulation deltas and explicit limits.

### Task 3: Audit and validation

- [ ] Audit bounded PM4 reads and renderer CPU paths against the screenshot advice; change only remaining proven defects.
- [ ] Run native, Vulkan and launcher suites plus the affected tooling checks.
- [ ] Review the integrated diff and run a gameplay smoke/benchmark without concurrent compilation.
- [ ] Document the effective paths, measured results and any unresolved guest limiter evidence.
