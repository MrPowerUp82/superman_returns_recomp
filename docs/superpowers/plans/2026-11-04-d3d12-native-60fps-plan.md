# D3D12 Native Renderer +60 FPS Implementation Plan (Updated for `cd4ff91`)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Push the native D3D12 renderer in `superman_returns_recomp` from its current post-Milestone-1–5 state (`~36.2–48.7 FPS`, `20.5–25.0 ms/frame`) past **60 FPS (`< 16.67 ms/frame` average)** in heavy Metropolis open-world gameplay at native `1280x720` (`resolution_scale = 1`, no Frame Generation, no DLSS, no RT, no visual quality reductions).

**Architecture:** Build directly on the foundation merged in `7487ddf`..`cd4ff91` (`WriteTrackingState::ReleaseCoverage`, `WorkBatch::shared_ranges`, 64-entry `SlotSrvCache`, `ShouldSkipIntraBatchBufferCheck`, `128`-command `WorkBatch`, and default-on `SubmitNativeXdkDraw`) by resolving the four remaining bottlenecks proven in [`docs/IMPLEMENTATION_AUDIT.md`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/docs/IMPLEMENTATION_AUDIT.md) and [`logs/full_audit_20261106_01_profile_off/game.log`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/logs/full_audit_20261106_01_profile_off/game.log):
1. **Fix Allocation Poisoning in `Monitor::Protection` & `HostWriteScope`:** Stop legacy physical texture-watch callbacks (`PhysicalHeap::TriggerCallbacks` -> `TrackedProtect` -> `Monitor::Protection`) from calling allocation-wide `Invalidate(...)` (`invalidated_checks=61,110` in `game.log`), which currently disables write-tracking proofs for both textures and vertex/index buffers.
2. **Scale `SlotSrvCache` to 512 Entries (`128 × 4-way`) & Stamp Frame-Cached Buffer Checks:** Eliminate 2-way set thrashing on slot 0 across 2,015 draws/frame (`slot_misses=931/frame`, `slots=6.18 ms/frame`) and stamp `last_checked_batch` on frame-cached `RefreshTrackedBuffer` hits (`calls=3,833/frame`).
3. **Admit `vs_list`/`ps_list` in `SubmitNativeXdkDraw` & Sync Constants Directly to `Pm4Mirror`:** Eliminate the 17,166 `vs_list` shader-literal refusals and the 7,156 `ring_limit` refusals while bypassing redundant 567M-dword PM4 constant ring round-trips.
4. **Native C++/SIMD Hooks for the Top Recompiled Guest CPU Hotspots:** Replace the top 7 non-XDK recompiled guest functions (`sub_82319108`, `sub_82305B88`, `sub_823055A8`, `sub_822F5290`, `sub_82316958`, `sub_8230F8E0`, `sub_82783948`) verified against the generated PowerPC functions with differential unit tests.

**Tech Stack:** C++20, Direct3D 12, CMake + Ninja, `clang-cl` / MSVC, SSE/AVX2 (`<immintrin.h>`), `rexglue-write-tracking` SDK patch (`tools/rexglue-write-tracking/` synced to `.tools/rexglue-object-motion-source`).

---

## Current Project State & Completed Foundation (`HEAD = cd4ff91`)

Between commits `7d6ca8d` and `cd4ff91`, the following pieces of the initial plan were implemented and validated (see [`docs/IMPLEMENTATION_AUDIT.md`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/docs/IMPLEMENTATION_AUDIT.md), [`docs/coop/PERFORMANCE-2026-11-07.md`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/docs/coop/PERFORMANCE-2026-11-07.md), and [`docs/build-sdk-selection-2026-11-07.md`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/docs/build-sdk-selection-2026-11-07.md)):

| Area | Status in `cd4ff91` | Remaining Bottleneck in [`game.log`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/logs/full_audit_20261106_01_profile_off/game.log) (Last 120 Swaps, ~2,015 Draws/Frame) |
|---|---|---|
| **SDK Write-Tracking (`ReleaseCoverage`)** | ✅ `WriteTrackingState::ReleaseCoverage` implemented in [`write_tracking_state.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/write_tracking_state.h) and wired into `Unwatch` and `TrackingFault`. | ❌ **`Monitor::Protection` ([`runtime_memory_tracker.cpp:94`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp#L94)) still calls `Invalidate(...)`!** When `PhysicalHeap::TriggerCallbacks` (`texture_watch_`) unprotects pages on write fault, `TrackedProtect` calls `Monitor::Protection`, permanently setting `alloc.invalidated = true` for the entire 512 MB physical heap (`checks=61241 invalidated_checks=61110`, causing `texture_lookup=5.30 ms/frame`, `texture_hash=2.52 ms/frame`, and `BufferProofHolds` drops `9,612` watches). |
| **Narrow `CaptureDevice` (`shared_ranges`)** | ✅ `WorkBatch::shared_ranges` & `AppendSharedRange` merged in [`captured_batch.h:100`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/graphics/guest/captured_batch.h#L100) and wired into `Renderer::CaptureDevice` (`fe_device = 0.75 ms/frame`). | ✅ Complete (`fe_device` reduced to `0.75 ms/frame`). |
| **Multi-Entry `SlotSrvCache`** | ✅ `SlotSrvCache` (`32 sets × 2 ways = 64 entries`) merged in [`slot_srv_cache.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/slot_srv_cache.h) and wired into `UploadConstants` ([`native_renderer.cpp:4752`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp#L4752)). | ⚠️ **64 entries (2-way) still thrashes on heavy scenes:** across 2,015 draws/frame, `slot_misses = 931.28/frame` (`slots = 6.18 ms/frame`). Needs expansion to `128 sets × 4 ways` (`512` entries/slot) plus a per-frame resolved `(storage_fetch -> srv_index)` fast lookup in `GetTextureSrvIndex`. |
| **Intra-Batch Buffer Check Skip** | ✅ `ShouldSkipIntraBatchBufferCheck` merged in [`buffer_tracking_policy.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/buffer_tracking_policy.h) and called in `RefreshTrackedBuffer` ([`native_renderer.cpp:5926`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp#L5926)). | ⚠️ **Frame-cached hits don't stamp `last_checked_batch`:** when `t.content.frame == front_frame_` returns early at [`native_renderer.cpp:5936`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp#L5936), it leaves `t.last_checked_batch` stale, so every subsequent draw in the batch reloads `write_seq_` (`calls=3833/frame`, `fb_refresh=2.30 ms/frame`). |
| **Native XDK Draw Submission (`SubmitNativeXdkDraw`)** | ✅ Enabled by default in [`native_hooks.cpp:363`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_hooks.cpp#L363); `SubmitNativeXdkDrawDirectToMirror` helper added in [`xdk_draw_submission.h:454`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/xdk_draw_submission.h#L454). | ⚠️ **17,166 `vs_list` refusals + 7,156 `ring_limit` refusals + redundant PM4 ring copy/scan:** `SubmitNativeXdkDraw` refuses whenever `vs+60` (`vs_list`) or `ps+892` (`ps_list`) is non-zero (`sub_82108470`), and still writes all float constants into the guest PM4 ring (`fe_ring=0.54 ms`, worker `ScanCopy` parses `567M dwords`). |
| **Native Guest Math & Engine Hooks** | ❌ Not started yet. | ❌ Recompiled PPC functions `sub_82319108`, `sub_82305B88`, `sub_823055A8`, `sub_822F5290`, `sub_82316958`, `sub_8230F8E0`, `sub_82783948` still consume ~50% of guest thread CPU time (`guest_hotspots` in [`profiles.json`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/logs/full_implementation_audit_20261106_01/profiles.json)). |

### Canonical Build & Test Commands in `cd4ff91`
- **Sync SDK Write-Tracking Sources:**
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup_memory_tracking_sdk.ps1 -Checkout .tools/rexglue-object-motion-source`
- **Build Production Release Binary (`port/out/build/win-amd64-release/superman_returns.exe`):**
  `cmd /c build.cmd` (or `cmake --build port/out/build/win-amd64-release --config Release --target superman_returns`)
- **Run Native Unit Tests (`sr_native_tests`):**
  `cmake --build build/tests-native --target sr_native_tests; ctest --test-dir build/tests-native --output-on-failure`
- **Run XDK Differential Tests (`sr_xdk_tests`):**
  `cmake --build build/tests-xdk --target sr_xdk_tests; ctest --test-dir build/tests-xdk --output-on-failure`

---

## File Map

| File | Responsibility | Action |
|---|---|---|
| [`tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp) | Runtime page-protection monitor (`Monitor::Protection`, `HostWriteScope`) | Modify (`Invalidate` -> `ReleaseCoverage` on watched page unprotect / normal host I/O) |
| [`tests/memory_tracking/test_runtime_tracking.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/memory_tracking/test_runtime_tracking.cpp) | Runtime memory tracker unit tests | Modify (verify `TrackingHostProtection` & `HostWriteScope` preserve allocation validity) |
| [`tests/native/test_resource_write_tracking.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_resource_write_tracking.cpp) | Resource write tracker unit tests | Modify (verify non-overlapping watches survive `ReleaseCoverage`) |
| [`port/src/native_renderer/slot_srv_cache.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/slot_srv_cache.h) | Set-associative per-slot SRV cache + per-frame texture SRV lookup cache | Modify (`kSets = 128, kWays = 4`, add `FrameTextureSrvCache`) |
| [`tests/native/test_slot_srv_cache.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_slot_srv_cache.cpp) | Unit tests for `SlotSrvCache` & `FrameTextureSrvCache` | Modify |
| [`port/src/native_renderer/buffer_tracking_policy.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/buffer_tracking_policy.h) | Intra-batch and frame-cached buffer check policy helpers | Modify |
| [`tests/native/test_buffer_tracking_policy.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_buffer_tracking_policy.cpp) | Unit tests for buffer tracking policy | Modify |
| [`port/src/native_renderer/native_renderer.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp) | Worker `GetTextureSrvIndex` frame cache + `RefreshTrackedBuffer` batch stamp + direct XDK draw submission | Modify |
| [`port/src/native_renderer/xdk_draw_submission.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/xdk_draw_submission.h) | Native XDK draw submission (`sub_82108470` shader literal lists + direct mirror constant sync) | Modify |
| [`port/src/native_renderer/native_hooks.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_hooks.cpp) | Guest hook dispatch for XDK draws, guest math, and scene traversal | Modify |
| [`tests/xdk/test_draw_submission.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/test_draw_submission.cpp) | Differential oracle tests for `SubmitNativeXdkDraw` vs generated PPC | Modify |
| [`port/src/native_renderer/guest_math_hooks.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/guest_math_hooks.h) | Native C++/SIMD hooks for `sub_823055A8`, `sub_82319108`, `sub_82305B88` | Create |
| [`port/src/native_renderer/guest_engine_hooks.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/guest_engine_hooks.h) | Native C++ hooks for `sub_822F5290`, `sub_82316958`, `sub_8230F8E0`, `sub_82783948` | Create |
| [`port/include/game_profile.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/include/game_profile.h) | Declare `NativeHook_*` entry points for `#pragma rex_hook` | Modify |
| [`tests/xdk/test_constant_copy.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/test_constant_copy.cpp) | Differential oracle tests for native guest math & engine hooks vs generated PPC | Modify |

---

## Task 1: Fix Allocation Poisoning in `Monitor::Protection` & `HostWriteScope` (Remainder of Approach 1B)

**Why this matters:** In [`logs/full_audit_20261106_01_profile_off/game.log:1781`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/logs/full_audit_20261106_01_profile_off/game.log#L1781), `resource write tracking` reports `checks=61241 uncovered_checks=61110 invalidated_checks=61110` and `buffer watch: watches 19560 drops 9612`. Why? When the legacy physical write-watch (`PhysicalHeap::TriggerCallbacks`, used by `texture_watch_`) unprotects a faulted page, it calls `TrackedProtect(memory_, ...)` -> `TrackingHostProtection` -> `Monitor::Protection` ([`runtime_memory_tracker.cpp:94`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp#L94)), which currently executes `if (watched) Invalidate(a * kPage, (b - a + 1) * kPage);`. Calling `Invalidate` sets `alloc.invalidated = true` on the entire guest physical heap allocation forever! Replacing `Invalidate` with `ReleaseCoverage` in `Monitor::Protection` and normal `HostWriteScope` keeps the allocation valid while still incrementing `write_epoch` and clearing `covered` on the affected pages.

**Files:**
- Modify: [`tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp:84-97`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp#L84-L97) and [`tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp:324-340`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp#L324-L340)
- Modify: [`tests/memory_tracking/test_runtime_tracking.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/memory_tracking/test_runtime_tracking.cpp)
- Modify: [`tests/native/test_resource_write_tracking.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_resource_write_tracking.cpp)

- [ ] **Step 1: Add a failing test in `tests/native/test_resource_write_tracking.cpp` proving that `ReleaseCoverage` on one page range does not poison `Sample` on sibling watched ranges**

Add to [`tests/native/test_resource_write_tracking.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_resource_write_tracking.cpp):

```cpp
bool TestProtectionChangeReleasesPageCoverageWithoutInvalidatingHeap() {
  sr::tracking::WriteTrackingState state;
  if (!state.Allocate(0, 64 * 4096)) return false;

  // Texture A on pages [0, 2), Texture B on pages [8, 10).
  if (!state.Watch(0, 2 * 4096)) return false;
  if (!state.Watch(8 * 4096, 2 * 4096)) return false;

  const auto baseline_a = state.Sample(0, 2 * 4096);
  const auto baseline_b = state.Sample(8 * 4096, 2 * 4096);
  if (!baseline_a.valid() || !baseline_b.valid()) return false;

  // Simulate PhysicalHeap::TriggerCallbacks / TrackedProtect changing protection on Texture A.
  state.ReleaseCoverage(0, 2 * 4096);

  // Texture A lost coverage and bumped epoch, but the allocation is NOT invalidated.
  const auto after_a = state.Sample(0, 2 * 4096);
  if (after_a.coverage != sr::tracking::CoverageState::Uncovered) return false;
  if (after_a.write_epoch <= baseline_a.write_epoch) return false;

  // Texture B on pages [8, 10) MUST remain Covered with unchanged epoch!
  const auto after_b = state.Sample(8 * 4096, 2 * 4096);
  if (!after_b.valid() || after_b.write_epoch != baseline_b.write_epoch) return false;

  // Re-watching Texture A restores Covered status.
  if (!state.Watch(0, 2 * 4096)) return false;
  const auto rewatched_a = state.Sample(0, 2 * 4096);
  return rewatched_a.valid();
}
```

- [ ] **Step 2: Update `Monitor::Protection` and `HostWriteScope` in `tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp`**

In [`tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp), update `Monitor::Protection` (lines 84–96) and `HostWriteScope` (lines 324–339):

```cpp
  void Protection(const void* host, size_t length) {
    if (!base || !length) return;
    std::lock_guard lock(mutex);
    size_t a = 0, b = 0;
    if (!Range(host, length, a, b)) { state.Invalidate(0, size); return; }
    bool watched = false;
    for (size_t p = a; p <= b; ++p) {
      watched |= watch_refs[p] != 0;
      armed[p] = 0;
    }
    if (watched) state.ReleaseCoverage(a * kPage, (b - a + 1) * kPage);
  }
```
And in `HostWriteScope::HostWriteScope` (around line 334):
```cpp
    if (watched) m.state.ReleaseCoverage(first * kPage, (last - first + 1) * kPage);
```
*(Note: while `host_writers[p] > 0`, `Monitor::Sample` already sets `s.coverage = CoverageState::Unknown`, and `ReleaseCoverage` bumps `write_epoch` and clears `covered` on those pages so any observer sees both the write and the coverage loss without poisoning the entire 512 MB heap allocation.)*

- [ ] **Step 3: Sync SDK sources and run unit tests**

Run:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup_memory_tracking_sdk.ps1 -Checkout .tools/rexglue-object-motion-source
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native --output-on-failure
```
Expected: `setup_memory_tracking_sdk.ps1` reports `SDK source patched and verified` and `100% tests passed` in `sr_native_tests`.

- [ ] **Step 4: Rebuild Release binary and verify `invalidated_checks=0` in `game.log`**

Run:
```powershell
cmake --build port/out/build/win-amd64-release --config Release --target superman_returns
```
 Expected: Build succeeds cleanly.

- [ ] **Step 5: Commit Task 1**

```powershell
git add tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp tests/native/test_resource_write_tracking.cpp tests/memory_tracking/test_runtime_tracking.cpp
git commit -m "fix(tracking): release page coverage on host protection changes instead of poisoning heap"
```

---

## Task 2: Expand `SlotSrvCache` to 512 Entries (`128 × 4-Way`) & Add Per-Frame Texture SRV Lookup Cache (Remainder of Approach 3B)

**Why this matters:** In [`logs/full_audit_20261106_01_profile_off/game.log:1766`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/logs/full_audit_20261106_01_profile_off/game.log#L1766), across 2,015 draws/frame, `UploadConstants` still incurs **931.28 `slot_misses`/frame** (`slots = 6.18 ms/frame`, `texture_lookup = 5.30 ms/frame`). Expanding `SlotSrvCache` from `32 × 2` (64 entries) to `128 × 4` (512 entries per slot, ~24 KB) eliminates intra-frame set-conflict evictions on hot slots (like slot 0 albedo), and adding a lightweight per-frame `FrameTextureSrvCache` (`256 sets × 4 ways` keyed by the 6 `TextureStorageFetch` dwords) in `GetTextureSrvIndex` avoids repeating `textures_.find` + page-watch/hash validation when the same texture is bound to multiple slots in the same frame.

**Files:**
- Modify: [`port/src/native_renderer/slot_srv_cache.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/slot_srv_cache.h)
- Modify: [`tests/native/test_slot_srv_cache.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_slot_srv_cache.cpp)
- Modify: [`port/src/native_renderer/native_renderer.cpp:3766-3835`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp#L3766-L3835)

- [ ] **Step 1: Write failing tests in `tests/native/test_slot_srv_cache.cpp` for 256+ distinct textures per slot and cross-slot `FrameTextureSrvCache` hits**

Add to [`tests/native/test_slot_srv_cache.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_slot_srv_cache.cpp):

```cpp
static bool TestLargeWorkingSetNoThrashing() {
  SlotSrvCache cache{};
  // Simulate 200 distinct textures bound to slot 0 within a single frame.
  for (uint32_t i = 1; i <= 200; ++i) {
    const auto fetch = MakeFetch(0x02000000u + i * 0x1000u, 0x00100100u + i);
    cache.Insert(10, i, fetch, 1000 + i, 1, 0);
  }
  uint32_t hits = 0;
  for (uint32_t i = 1; i <= 200; ++i) {
    const auto fetch = MakeFetch(0x02000000u + i * 0x1000u, 0x00100100u + i);
    if (cache.Find(10, fetch) != nullptr) ++hits;
  }
  // At least 95% of a 200-texture single-slot working set must remain resident.
  return hits >= 190;
}

static bool TestFrameTextureSrvCacheCrossSlotHit() {
  FrameTextureSrvCache frame_cache{};
  const auto storage_fetch = MakeFetch(0x04001000u, 0x00200200u);
  if (frame_cache.Find(42, 7, storage_fetch) != UINT32_MAX) return false;
  frame_cache.Insert(42, 7, storage_fetch, 333);
  if (frame_cache.Find(42, 7, storage_fetch) != 333) return false;
  // Next frame or next invalidation stamp must miss:
  if (frame_cache.Find(43, 7, storage_fetch) != UINT32_MAX) return false;
  if (frame_cache.Find(42, 8, storage_fetch) != UINT32_MAX) return false;
  return true;
}
```

- [ ] **Step 2: Run `sr_native_tests` to confirm the new test fails**

Run:
```powershell
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native --output-on-failure
```
Expected: Compile error (`FrameTextureSrvCache` undefined) or test failure (`TestLargeWorkingSetNoThrashing` fails with 64-entry cache).

- [ ] **Step 3: Expand `SlotSrvCache` (`kSets = 128, kWays = 4`) and implement `FrameTextureSrvCache` in `port/src/native_renderer/slot_srv_cache.h`**

Update [`port/src/native_renderer/slot_srv_cache.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/slot_srv_cache.h) so:
- `SlotSrvCache` uses `kSets = 128`, `kWays = 4`, `kCapacity = kSets * kWays` (`512` entries), with a per-set 2-bit replacement hand (`uint8_t next_way[kSets]{}`) instead of `used_way0`.
- `FrameTextureSrvCache` provides a `256 sets × 4 ways` per-frame cache mapping `(frame_count, invalidation_stamp, storage_fetch)` -> `srv_index`, wired into `Renderer::GetTextureSrvIndex` ([`native_renderer.cpp:3766`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp#L3766)) right after `TextureStorageFetch(fetch)`.

- [ ] **Step 4: Wire `FrameTextureSrvCache` into `Renderer::GetTextureSrvIndex` in `port/src/native_renderer/native_renderer.cpp` and run `sr_native_tests`**

Run:
```powershell
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native --output-on-failure
```
Expected: `100% tests passed` in `sr_native_tests`.

- [ ] **Step 5: Commit Task 2**

```powershell
git add port/src/native_renderer/slot_srv_cache.h port/src/native_renderer/native_renderer.cpp tests/native/test_slot_srv_cache.cpp
git commit -m "perf(d3d12): expand SlotSrvCache to 512 entries and add per-frame texture SRV cache"
```

---

## Task 3: Stamp `last_checked_batch` on Frame-Cached `RefreshTrackedBuffer` Hits (Remainder of Approach 3A)

**Why this matters:** In [`port/src/native_renderer/native_renderer.cpp:5926-5940`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp#L5926-L5940), `RefreshTrackedBuffer` checks `ShouldSkipIntraBatchBufferCheck(...)` first, and if that misses (e.g. on the first draw of a new `WorkBatch`), it checks `t.content.frame == front_frame_ && t.content.write_seq == write_seq_.load(std::memory_order_relaxed)`. When that second check succeeds and returns early at line 5939, **it never updates `t.last_checked_batch = current_batch` or `t.last_checked_invalidation_stamp = invalidation_stamp_`**! As a result, every subsequent draw in the same batch misses `ShouldSkipIntraBatchBufferCheck` and reloads `write_seq_` (`calls=3833/frame`).

**Files:**
- Modify: [`port/src/native_renderer/buffer_tracking_policy.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/buffer_tracking_policy.h)
- Modify: [`port/src/native_renderer/native_renderer.cpp:5925-5942`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp#L5925-L5942)
- Modify: [`tests/native/test_buffer_tracking_policy.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_buffer_tracking_policy.cpp)

- [ ] **Step 1: Add a helper and test in `buffer_tracking_policy.h` / `test_buffer_tracking_policy.cpp` for stamping batch state on frame-cached buffer reuse**

In [`port/src/native_renderer/buffer_tracking_policy.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/buffer_tracking_policy.h), add:

```cpp
inline void StampIntraBatchBufferCheck(
    uint64_t& last_checked_batch,
    uint64_t& last_checked_invalidation_stamp,
    uint64_t current_batch,
    uint64_t current_invalidation_stamp) noexcept {
  last_checked_batch = current_batch;
  last_checked_invalidation_stamp = current_invalidation_stamp;
}
```
And call `StampIntraBatchBufferCheck(t.last_checked_batch, t.last_checked_invalidation_stamp, current_batch, invalidation_stamp_);` inside the `if (t.content.frame == front_frame_ && ...)` fast-return block at [`native_renderer.cpp:5936-5940`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp#L5936-L5940).

- [ ] **Step 2: Build and run `sr_native_tests`**

Run:
```powershell
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native --output-on-failure
```
Expected: `100% tests passed`.

- [ ] **Step 3: Commit Task 3**

```powershell
git add port/src/native_renderer/buffer_tracking_policy.h port/src/native_renderer/native_renderer.cpp tests/native/test_buffer_tracking_policy.cpp
git commit -m "perf(d3d12): stamp intra-batch buffer state on frame-cached RefreshTrackedBuffer hits"
```

---

## Task 4: Admit `vs_list`/`ps_list` in `SubmitNativeXdkDraw` & Bypass Redundant PM4 Constant Ring Serialization (Remainder of Approach 1C)

**Why this matters:** In [`logs/full_audit_20261106_01_profile_off/game.log:1752-1757`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/logs/full_audit_20261106_01_profile_off/game.log#L1752-L1757):
- `native XDK draw submission: submitted=492684 unsupported_state=24448 ring_full=7156`
- `native XDK refusal reasons: gen_unsupported=6348 vs_list=17166 ps_list=934 ring_limit=7156`
1. **74% of all `unsupported_state` refusals (`18,100 / 24,448`) are `vs_list` (`17,166`) and `ps_list` (`934`)!** In the XDK draw prologue (`sub_82108470`), `vs+60` (`vs_list`) and `ps+892` (`ps_list`) point to a small array of shader literal constant descriptors that copy literal dwords into the ring before the shader microcode DMA packet. Supporting `sub_82108470` in `SubmitNativeXdkDraw` eliminates these 18,100 refusals.
2. **Even when `SubmitNativeXdkDraw` succeeds (`492,684` draws), `native_hooks.cpp` still writes all dirty float/ALU/fetch constants into the guest PM4 ring, `Renderer::DrawIndexedVertices` copies those ring bytes into `batch_->bytes` (`fe_ring = 0.54 ms/frame`), and the worker thread parses `567,611,039 dwords` (`26.6M PM4 packets`) back into `mirror_`!** Worse, writing up to 2 KB of float constants into the guest ring per draw causes `7,156 ring_limit` refusals!

**Files:**
- Modify: [`port/src/native_renderer/xdk_draw_submission.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/xdk_draw_submission.h)
- Modify: [`tests/xdk/test_draw_submission.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/test_draw_submission.cpp)

- [ ] **Step 1: Add differential oracle test cases in `tests/xdk/test_draw_submission.cpp` covering non-empty `vs_list` and `ps_list` (`sub_82108470`)**

Inspect `sub_82108470` in [`generated/superman_returns_recomp.1.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/generated/superman_returns_recomp.1.cpp) and add a differential test in [`tests/xdk/test_draw_submission.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/test_draw_submission.cpp) that populates `vs+60` (`vs_list`) and `ps+892` (`ps_list`) and asserts byte-for-byte ring + device parity between `SubmitNativeXdkDraw` and `sub_820FBBF8` / `sub_820FC000`.

- [ ] **Step 2: Implement `sub_82108470` list emission in `SubmitNativeXdkDraw` (`port/src/native_renderer/xdk_draw_submission.h`)**

Replace the `VsListNotEmpty` / `PsListNotEmpty` refusal guards at [`xdk_draw_submission.h:233-240`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/xdk_draw_submission.h#L233-L240) with exact `sub_82108470` packet emission so `vs_list` and `ps_list` draws are admitted natively.

- [ ] **Step 3: Run `sr_xdk_tests` and `sr_native_tests`**

Run:
```powershell
cmake --build build/tests-xdk --target sr_xdk_tests
ctest --test-dir build/tests-xdk --output-on-failure
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native --output-on-failure
```
Expected: `100% tests passed` in both suites.

- [ ] **Step 4: Commit Task 4**

```powershell
git add port/src/native_renderer/xdk_draw_submission.h tests/xdk/test_draw_submission.cpp
git commit -m "perf(xdk): admit vs_list and ps_list shader literal tables in SubmitNativeXdkDraw"
```

---

## Task 5: Native C++/SIMD Reimplementation of Hot Guest Math & Culling Functions (`sub_82319108`, `sub_82305B88`, `sub_823055A8` — Approach 2A)

**Why this matters:** In [`logs/full_implementation_audit_20261106_01/profiles.json`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/logs/full_implementation_audit_20261106_01/profiles.json), guest CPU sampling shows `sub_82319108`, `sub_82305B88`, and `sub_823055A8` among the hottest recompiled functions in the entire binary because every PowerPC load/store goes through byte-swapped memory helpers and scalar VMX128 emulation.

**Files:**
- Create: [`port/src/native_renderer/guest_math_hooks.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/guest_math_hooks.h)
- Modify: [`port/include/game_profile.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/include/game_profile.h)
- Modify: [`port/src/native_renderer/native_hooks.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_hooks.cpp)
- Modify: [`tests/xdk/test_constant_copy.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/test_constant_copy.cpp)

- [ ] **Step 1: Inspect the generated implementations of `sub_823055A8`, `sub_82319108`, and `sub_82305B88` in `generated/`**

Read the exact generated C++ in `generated/superman_returns_recomp.*.cpp` for:
- `sub_823055A8` (sphere/AABB frustum plane loop)
- `sub_82319108` (transform/bounds tree evaluation)
- `sub_82305B88` (4×4 matrix / vector batch transform)

- [ ] **Step 2: Write differential oracle tests in `tests/xdk/test_constant_copy.cpp` comparing `Native_sub_823055A8`, `Native_sub_82319108`, and `Native_sub_82305B88` against the generated PPC functions**

Create randomized guest memory fixtures in [`tests/xdk/test_constant_copy.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/test_constant_copy.cpp), run the generated `sub_*` on `ctx_ref`/`mem_ref` and `Native_sub_*` on `ctx_nat`/`mem_nat`, and assert identical return values (`r3`/`f1`) and guest memory output.

- [ ] **Step 3: Implement `port/src/native_renderer/guest_math_hooks.h` and wire hooks in `game_profile.h` & `native_hooks.cpp`**

Implement `Native_sub_823055A8`, `Native_sub_82319108`, and `Native_sub_82305B88` in [`port/src/native_renderer/guest_math_hooks.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/guest_math_hooks.h) (guarded by `SR_NATIVE_GUEST_MATH_HOOKS`, default `1`).

- [ ] **Step 4: Run `sr_xdk_tests` and verify 100% differential parity**

Run:
```powershell
cmake --build build/tests-xdk --target sr_xdk_tests
ctest --test-dir build/tests-xdk --output-on-failure
```
Expected: `100% tests passed`.

- [ ] **Step 5: Commit Task 5**

```powershell
git add port/src/native_renderer/guest_math_hooks.h port/include/game_profile.h port/src/native_renderer/native_hooks.cpp tests/xdk/test_constant_copy.cpp
git commit -m "perf(cpu): add native C++/SIMD hooks for hot guest math and frustum culling functions"
```

---

## Task 6: Native C++ Reimplementation of Hot Guest Engine Draw Setup & Scene Dispatchers (`sub_822F5290`, `sub_82316958`, `sub_8230F8E0`, `sub_82783948` — Approach 2B & 2C)

**Why this matters:** In [`logs/full_implementation_audit_20261106_01/profiles.json`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/logs/full_implementation_audit_20261106_01/profiles.json), `sub_822F5290` (`7.33%` of all guest CPU samples), `sub_82316958` (`4.29%`), `sub_8230F8E0` (`5.14%`), and `sub_82783948` (`3.57%`) account for **~20.3% of total guest CPU execution**.

**Files:**
- Create: [`port/src/native_renderer/guest_engine_hooks.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/guest_engine_hooks.h)
- Modify: [`port/include/game_profile.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/include/game_profile.h)
- Modify: [`port/src/native_renderer/native_hooks.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_hooks.cpp)
- Modify: [`tests/xdk/test_constant_copy.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/test_constant_copy.cpp)

- [ ] **Step 1: Inspect the generated implementations of `sub_822F5290`, `sub_82316958`, `sub_8230F8E0`, and `sub_82783948` in `generated/`**

Verify all memory accesses and helper calls for each of the four functions.

- [ ] **Step 2: Write differential oracle tests in `tests/xdk/test_constant_copy.cpp` and implement `port/src/native_renderer/guest_engine_hooks.h`**

Test each native function against its generated PowerPC counterpart across randomized valid inputs, then register `#pragma rex_hook` declarations in [`port/include/game_profile.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/include/game_profile.h) and [`port/src/native_renderer/native_hooks.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_hooks.cpp) (guarded by `SR_NATIVE_GUEST_ENGINE_HOOKS`, default `1`).

- [ ] **Step 3: Build Release binary and run the full benchmark verification**

Run:
```powershell
cmake --build build/tests-xdk --target sr_xdk_tests
ctest --test-dir build/tests-xdk --output-on-failure
cmake --build port/out/build/win-amd64-release --config Release --target superman_returns
python tools/bench/d3d12_fps.py --seconds 60
```
Expected:
- All unit and differential tests pass (`100% tests passed`).
- `invalidated_checks = 0` in `game.log`.
- `slot_misses` reduced from `~931/frame` to `< 150/frame`.
- `vs_list` and `ps_list` XDK draw refusals reduced to `0`.
- Average FPS at `1280x720` (`resolution_scale = 1`, no FG/DLSS/RT) reaches **> 60.0 FPS** (`< 16.67 ms/frame`).

- [ ] **Step 4: Commit Task 6**

```powershell
git add port/src/native_renderer/guest_engine_hooks.h port/include/game_profile.h port/src/native_renderer/native_hooks.cpp tests/xdk/test_constant_copy.cpp
git commit -m "perf(cpu): add native C++ hooks for hot guest draw packet and scene traversal functions"
```
