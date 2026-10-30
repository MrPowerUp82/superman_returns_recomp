# Reference Optimizations Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans for implementation and a fresh final reviewer. Track steps below.

**Goal:** Complete the applicable reference optimizations without duplicating existing fixes.
**Architecture:** Keep existing native render/audio paths; extend uploaded texture proofs with the optional fast tracking domain and validate every captured range through the runtime epoch view. Fall back to locked sampling when unavailable or invalid.
**Tech Stack:** C++20, Windows, D3D12, ReXGlue write tracking, CMake/Clang.
**Spec:** `docs/superpowers/specs/2026-10-29-reference-optimizations-design.md`

## Global Constraints

- Preserve resource identity, complete coverage, writer exclusion and legacy hash fallback.
- Do not change simulation timing or claim the private fork's performance/fidelity.
- Reuse this clean checkout and its installed SDK/builds; no new dependencies.

## Review Focus

- A write in any mip/layer must reject reuse.
- A replacement resource must reject an older proof.
- Missing runtime view or changed domain must use the existing fallback.
- Incomplete coverage and active writers must never authorize reuse.
- Compare mode must continue hashing; a failed fast check must not bypass it.

## Task 1: Verify existing reference features

- [x] Audit renderer paths locally and FPS/XMA through the independent audit.
- [x] Capture the baseline benchmark and identify remaining limitations.

## Task 2: Fast texture proof checks

Files: `resource_write_tracking.h`, `texture_write_proof.h`, `native_renderer.cpp`, `native_renderer.h`, `tests/native/test_tracking_fast_view.cpp`.

Interface: `ResourceWriteTracker::CanReuseTextureFastIn(view, proof, ranges, live_resource_version)` and its instance wrapper. Add optional `domain` to `TextureUploadProof` and captured ranges/domain to `TextureTrackingEntry`.

- [x] Write tests for unchanged multi-range reuse, writes to each range, resource replacement, invalid coverage/writers, absent view, changed domain and out-of-bounds ranges.
- [x] Build and confirm the expected RED (new API absent).
- [x] Implement proof checks, capture the domain before reading guest bytes and publish only that captured domain when unchanged.
- [x] Integrate fast checks before Sample; retain fallback and add fast hit count to existing diagnostics.
- [x] Build and run native tests, build Release and run the benchmark with matching settings.

## Task 3: Review and report

- [x] Independent review of proof lifetime, fallback and range coverage.
- [x] Record actual results, existing implementations and limits in a concise audit document.
- [x] Verify diff and checks; leave reviewable changes in the checkout without automatic commit/push.

## Task 4: Correct confirmed XMA defects (audit follow-up)

Files: `port/src/xma_fixes.cpp`, `port/src/xma_flush_policy.h`, `tests/native/test_xma_flush_policy.cpp`.

- [x] Correct input exhaustion to 16,384 bits per packet, testing both active buffers and their boundaries.
- [x] Restore the original guest context argument for each XMA export call, including the post-flush kick; test an export clobbering its return register.
- [x] Run focused audio regressions, then include them in the native suite and Release build verification.

## Execution ledger

Ruling: Work in the existing clean checkout, as the desktop instructions prefer. The user authorized applying the reference optimizations; proceed with the bounded implementation and validation.

Pre-flight: Task 1 supplies the current-state evidence; Task 2 preserves existing tracking invariants; Task 3 reviews that integration. No concurrent code writers.

Task 2: RED confirmed missing fast proof/domain API; GREEN: 573 native tests, 0 failed checks, and all four native CTest targets passed. Final integration tests follow the audio fixes.

Ruling: Add Task 4 because the independent audit found two concrete bugs in the requested XMA flush path. Delegate only XMA files; renderer changes remain local. No broad audio-engine rewrite or simulation tick changes.

Final: Release build passed, 575 native tests passed, native CTest 5/5 and runtime CTest 4/4 passed (35 runtime tests in the main target). Independent texture review found no defects. XMA RED/GREEN and the actual hook ABI regression passed. Same-binary A/B completed successfully with fast_reuses=0/22314 and zero tracking mismatches; 34.73/40.49 FPS is exploratory, not a causal gain or a 60 FPS claim. Evidence and preexisting limitations are in `docs/reference-optimizations-2026-10-29.md`.
