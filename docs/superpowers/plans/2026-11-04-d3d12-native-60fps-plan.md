# Native D3D12 Renderer +60 FPS Optimization Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce average frame time on the standard 60s title flyover benchmark (`1280x720`, `resolution_scale = 1`, native D3D12 renderer, no Frame Generation) from `~24.0 ms` (`41.5 FPS`) to `< 16.67 ms` (`> 60 FPS`).

**Architecture:** Three parallel, composable tracks evaluated via strict A/B benchmarks after each task:
1. **Approach 1 (XDK/PM4 Redundancy Bypass & Page-Aware Write-Watch):** Stop page faults and single-page unwatch events from permanently poisoning shared physical heap allocations (`ReleaseCoverage` in `WriteTrackingState`), coalesce and delta-reuse `CaptureDevice` guest memory snapshots across draws in a `WorkBatch`, and bypass PM4 constant ring serialization when running in native D3D12 mode.
2. **Approach 3 (Worker Cache & Stream Validation Dedup):** Expand the worker's per-slot `SlotCache` from an 8-entry linear ring to a 64-entry 2-way set-associative cache (eliminating ~404 per-frame `GetTextureSrvIndex` + `XXH3_64bits` + `textures_.find` misses) and stamp `TrackedBuffer` with `last_checked_batch_serial_` to skip redundant `RefreshTrackedBuffer` validations within a 128-draw `WorkBatch`.
3. **Approach 2 (Native C++/SIMD Reimplementation of Hot Guest Engine Functions):** Replace the top recompiled PowerPC guest CPU bottlenecks (`sub_823055A8`, `sub_82319108`, `sub_82305B88`, `sub_822F5290`, `sub_82316958`, `sub_8230F8E0`, `sub_82783948`) with native C++/SIMD hooks verified against the generated PowerPC reference functions via differential oracle tests.

**Tech Stack:** C++20, CMake, Ninja, D3D12, SIMDe / x86 SSE4.1/AVX2, ReXGlue SDK, Python 3 (`tools/bench/d3d12_fps.py`).

**Spec:** [`docs/superpowers/specs/2026-11-04-d3d12-native-60fps-design.md`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/docs/superpowers/specs/2026-11-04-d3d12-native-60fps-design.md)

## Global Constraints

- **No Frame Generation / No Quality Downgrade:** `frame_generation` must remain `false`, `resolution_scale` must remain `1`, and no draw calls, shadows, or post-process passes may be skipped.
- **Clean A/B Benchmark Discipline:** Every performance claim must be measured with [`tools/bench/d3d12_fps.py`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/bench/d3d12_fps.py) using `--warmup 5 --seconds 60` at `1280x720`, with all diagnostic env vars (`SR_NATIVE_STAGE_PROFILE`, `SR_NATIVE_CPU_PROFILE`, `SR_NATIVE_RESOURCE_UNLOCK_AUDIT`, `SR_NATIVE_CAPTURE`) unset or `0`.
- **Fail-Closed Memory Safety:** Explicit contract violations (`api->invalidate(session, watch)` called from `AuditAndInvalidateTextureProof` on compare mismatch) must still permanently disqualify the entire allocation (`it->second.invalidated = true`). Only normal page-level write faults and `Unwatch` events use `ReleaseCoverage` so unaffected sibling resources keep valid coverage.
- **Bit/ULP Equivalence for Native Guest Hooks:** Every reimplemented guest function in Approach 2 must pass a standalone differential test in `tests/xdk/` comparing guest RAM and register outputs against the extracted `port/generated/default/superman_returns_recomp.*.cpp` reference implementation.

## Review Focus

- **Page-Aware Write-Watch Invalidation (Task 1):** Verify that `ReleaseCoverage` increments `pages_[p].epoch`, clears `pages_[p].covered = false`, and updates `PublishPageEpoch(p, page.epoch)` without setting `it->second.invalidated = true`, while `Invalidate` still sets `it->second.invalidated = true`. Verify `runtime_compare_missed_write_disqualifies_entire_allocation_generation` in `tests/memory_tracking/test_runtime_tracking.cpp` continues to pass.
- **Intra-Batch `CaptureDevice` Range Inheritance (Task 2):** Verify that when a draw reuses `CaptureRange` entries from an earlier draw in the same `WorkBatch`, `cmd.range_first` and `cmd.range_count` (or the per-batch shared range table) remain valid for `Load32` on the worker thread without dangling pointers or missing updates when `vertex_decl`, `render_targets`, or `depth_stencil` change mid-batch.
- **Worker `SlotCache` Associativity & Invalidation (Task 3):** Verify that `SlotCache::Invalidate()` and `InvalidateSlots()` clear all 64 set-associative entries and that `fetch[0..5]` descriptor identity checks still guard every hit.
- **Differential Oracle Coverage (Tasks 5 & 7):** Verify that each native C++ replacement hook restores stack/non-volatile registers identically to the PowerPC original and handles empty/zero-count edge cases cleanly.

---

### Task 1: Page-Aware Write-Watch Coverage (`ReleaseCoverage`) & Runtime A/B (Approach 1B)

**Files:**
- Modify: [`tools/rexglue-write-tracking/src/write_tracking_state.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/write_tracking_state.h)
- Modify: [`tools/rexglue-write-tracking/src/write_tracking_state.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/write_tracking_state.cpp)
- Modify: [`tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp)
- Modify: [`tests/memory_tracking/test_runtime_tracking.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/memory_tracking/test_runtime_tracking.cpp)
- Modify: [`tests/native/test_resource_write_tracking.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_resource_write_tracking.cpp)

- [ ] **Step 1: Write failing unit tests for page-scoped `ReleaseCoverage` vs allocation-wide `Invalidate`**

Add a test in [`tests/native/test_resource_write_tracking.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_resource_write_tracking.cpp) and a runtime test in [`tests/memory_tracking/test_runtime_tracking.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/memory_tracking/test_runtime_tracking.cpp) proving that:
1. Releasing or faulting on page `0x1000..0x1FFF` of a multi-page allocation (`0x1000..0x3FFF`) invalidates page `0x1000` (`RexTrackingCoverage::Unknown` after `ReleaseCoverage` with advanced `invalidation_epoch`), leaves sibling page `0x2000..0x2FFF` at `RexTrackingCoverage::Complete` with its original `invalidation_epoch`, and allows `MarkCovered({0x1000, 0x1000})` to re-arm page `0x1000` on the next upload.
2. Calling explicit `Invalidate` (used on compare-audit mismatch) still permanently poisons the entire allocation (`RexTrackingCoverage::Invalidated` on both `0x1000` and `0x2000`, and `MarkCovered` returns `false`).

```cpp
// Add to tests/native/test_resource_write_tracking.cpp:
SR_TEST(tracking_release_coverage_keeps_sibling_pages_and_allows_rearm) {
  WriteTrackingState state;
  state.Allocate(0x1000, 0x3000);
  SR_CHECK(state.MarkCovered({0x1000, 0x1000}));
  SR_CHECK(state.MarkCovered({0x2000, 0x1000}));
  const auto page1_before = state.Sample({0x1000, 0x1000});
  const auto page2_before = state.Sample({0x2000, 0x1000});
  SR_CHECK(page1_before.coverage == RexTrackingCoverage::Complete);
  SR_CHECK(page2_before.coverage == RexTrackingCoverage::Complete);

  state.ReleaseCoverage({0x1000, 0x1000});
  const auto page1_after = state.Sample({0x1000, 0x1000});
  const auto page2_after = state.Sample({0x2000, 0x1000});
  SR_CHECK(page1_after.coverage == RexTrackingCoverage::Unknown);
  SR_CHECK(page1_after.invalidation_epoch > page1_before.invalidation_epoch);
  SR_CHECK(page2_after.coverage == RexTrackingCoverage::Complete);
  SR_CHECK_EQ(page2_after.invalidation_epoch, page2_before.invalidation_epoch);

  // Page 1 can be re-covered after re-arming its watch without reallocating the heap:
  SR_CHECK(state.MarkCovered({0x1000, 0x1000}));
  SR_CHECK(state.Sample({0x1000, 0x1000}).coverage == RexTrackingCoverage::Complete);
}
```

```cpp
// Add to tests/memory_tracking/test_runtime_tracking.cpp:
TEST(runtime_page_fault_and_unwatch_preserve_sibling_resource_in_shared_heap) {
  GuestFixture guest;
  const auto* api = guest.memory->resource_tracking_api();
  ASSERT(guest.memory->TrackAllocation(0x20000, 0x4000));
  RexTrackingRange first_range{0x20000, 0x1000};
  RexTrackingRange second_range{0x22000, 0x1000};
  const auto first_watch = api->register_watch(guest.session(), &first_range, 1, 1);
  const auto second_watch = api->register_watch(guest.session(), &second_range, 1, 2);
  ASSERT(first_watch != 0 && second_watch != 0);

  RexTrackingStamp second_before{};
  ASSERT(api->sample(guest.session(), second_watch, &second_before, 1) == 1);
  ASSERT(second_before.coverage == RexTrackingCoverage::Complete);

  // Write to the first resource's page and unwatch it; sibling resource on another page stays Complete.
  *guest.memory->TranslateVirtual<uint32_t*>(0x80020000) = 0x12345678u;
  api->unregister_watch(guest.session(), first_watch);

  RexTrackingStamp second_after{};
  ASSERT(api->sample(guest.session(), second_watch, &second_after, 1) == 1);
  ASSERT(second_after.coverage == RexTrackingCoverage::Complete);
  ASSERT(second_after.invalidation_epoch == second_before.invalidation_epoch);

  // Re-registering a watch on the written page succeeds and restores Complete coverage:
  const auto rearmed_watch = api->register_watch(guest.session(), &first_range, 1, 3);
  ASSERT(rearmed_watch != 0);
  RexTrackingStamp first_rearmed{};
  ASSERT(api->sample(guest.session(), rearmed_watch, &first_rearmed, 1) == 1);
  ASSERT(first_rearmed.coverage == RexTrackingCoverage::Complete);
}
```

- [ ] **Step 2: Run the unit tests to verify they fail**

Run:
```powershell
cmake -S tests/native -B build/tests-native
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native -R tracking_release_coverage --output-on-failure
```
Expected: Compilation failure (`ReleaseCoverage` is not a member of `WriteTrackingState`).

- [ ] **Step 3: Implement `ReleaseCoverage` in `WriteTrackingState` and wire it in `runtime_memory_tracker.cpp`**

In [`tools/rexglue-write-tracking/src/write_tracking_state.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/write_tracking_state.h):
```cpp
  void Invalidate(RexTrackingRange range);
  void Invalidate(uint32_t address, uint32_t size) { Invalidate({address, size}); }
  void ReleaseCoverage(RexTrackingRange range);
  void ReleaseCoverage(uint32_t address, uint32_t size) { ReleaseCoverage({address, size}); }
```
And in `private:` of `WriteTrackingState`:
```cpp
  void InvalidateLocked(RexTrackingRange range, uint64_t epoch);
  void ReleaseCoverageLocked(RexTrackingRange range, uint64_t epoch);
```

In [`tools/rexglue-write-tracking/src/write_tracking_state.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/write_tracking_state.cpp):
```cpp
void WriteTrackingState::ReleaseCoverageLocked(RexTrackingRange range, uint64_t epoch) {
  uint32_t first = 0, last = 0;
  if (!ValidRange(range, first, last)) return;
  for (uint32_t p = first; p <= last; ++p) {
    auto& page = pages_[p];
    page.epoch = epoch;
    page.covered = false;
    PublishPageEpoch(p, page.epoch);
  }
}

void WriteTrackingState::ReleaseCoverage(RexTrackingRange range) {
  std::lock_guard lock(mutex_);
  const uint64_t epoch = NextEpochLocked();
  if (epoch) ReleaseCoverageLocked(range, epoch);
}
```
Also update `BeginWrite` in `write_tracking_state.cpp` to use `ReleaseCoverageLocked` instead of `InvalidateLocked` when a tracked writer begins writing to a page range, or keep `BeginWrite` page-scoped so host/runtime writes only dirty the affected pages while `Invalidate(range)` remains the permanent allocation poisoner for `api->invalidate` and overlapping allocations.
Verify in [`tests/native/test_resource_write_tracking.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_resource_write_tracking.cpp) whether `tracking_active_writer_and_end_do_not_restore_coverage` expects `InvalidateLocked` on `BeginWrite`; if so, keep `BeginWrite` calling `InvalidateLocked` and use `ReleaseCoverage` specifically in `runtime_memory_tracker.cpp` for `Unwatch` (`runtime_memory_tracker.cpp:251`) and `TrackingFault` (`runtime_memory_tracker.cpp:414`).

In [`tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp):
- In `Unwatch` (around line 251), replace `m.Invalidate({page * kPageSize, kPageSize});` with `m.ReleaseCoverage({page * kPageSize, kPageSize});`.
- In `TrackingFault` (around line 414), replace `m.Invalidate({p * kPageSize, kPageSize});` with `m.ReleaseCoverage({p * kPageSize, kPageSize});`.
- Keep `InvalidateWatch` (`runtime_memory_tracker.cpp:265`, called by `api->invalidate`) calling `m.Invalidate(r)` so missed-write contract violations in `compare` mode still permanently disqualify the entire allocation!

Also check `WriteTrackingState::Sample` in [`tools/rexglue-write-tracking/src/write_tracking_state.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/rexglue-write-tracking/src/write_tracking_state.cpp):
When `page.covered` is `false` and `page.epoch > 0` (a page fault or coverage release occurred), ensure `Sample` reports `RexTrackingCoverage::Invalidated` (or `Unknown` with `invalidation_epoch = max(epoch, page.epoch)`) and that `MarkCovered` can re-arm `page.covered = true` as long as `it->second.invalidated` is `false` and `page.writers == 0`!
Let's check `Sample()` in `write_tracking_state.cpp:74-103`:
- In `Sample()`, `if (it->second.invalidated) invalidated = true; else if (!page.covered) unknown = true;`.
- If a page had `ReleaseCoverage`, `page.epoch` is bumped and `page.covered` is set to `false`. Then `Sample()` returns `invalidation_epoch = epoch` (which includes `page.epoch`) and `coverage = RexTrackingCoverage::Unknown`.
- Both `SameCoveredContent` (`resource_write_tracking.h:59`: `if (a[i].coverage != RexTrackingCoverage::Complete || b[i].coverage != RexTrackingCoverage::Complete) return false;`) and `FastViewSample::Complete` (`tracking_fast_view.h`) reject `Unknown` AND detect the bumped `page.epoch` (`PublishPageEpoch(p, page.epoch)` updates `fast_page_epochs_[p]`)!
- And when the renderer re-hashes/re-uploads the resource and registers a new watch (`ArmWatch` -> `MarkCovered`), because `it->second.invalidated` is still `false`, `MarkCovered` succeeds (`return true`) and restores `Complete` coverage with a fresh `coverage_generation`!

- [ ] **Step 4: Build and run both `sr_native_tests` and `sr_memory_tracking_tests`**

Run:
```powershell
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native --output-on-failure
```
And build/run the runtime memory tracking tests and main target:
```powershell
cmake --build build/linux-debug --target superman_returns_recomp
```
(If a separate build dir is used for `tests/memory_tracking`, run its `ctest` target as well.)
Expected: All tests in `sr_native_tests` and `sr_memory_tracking_tests` pass.

- [ ] **Step 5: Stage the updated `rexglue.dll` runtime and run the 60s A/B benchmark**

Copy the newly built write-tracking `rexglue.dll` into the active game runtime directory (or verify `build/linux-debug/bin/Debug/rexglue.dll` is up to date) and run [`tools/bench/d3d12_fps.py`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/bench/d3d12_fps.py):
```powershell
python tools/bench/d3d12_fps.py --exe build/linux-debug/bin/Debug/superman_returns_recomp.exe --assets extracted --warmup 5 --seconds 60 --out artifacts/d3d12-fps-20261104/bench_task1_page_coverage.json
```
Inspect `game.log` for `native texture tracking:` and `native buffer tracking:` counters (`fast_reuses`, `skipped_checks`, `invalidated_checks`) and record the resulting FPS and ms/frame.

- [ ] **Step 6: Commit Task 1**

```powershell
git add tools/rexglue-write-tracking/src/write_tracking_state.h tools/rexglue-write-tracking/src/write_tracking_state.cpp tools/rexglue-write-tracking/src/runtime_memory_tracker.cpp tests/native/test_resource_write_tracking.cpp tests/memory_tracking/test_runtime_tracking.cpp
git commit -m "perf(tracking): use page-scoped ReleaseCoverage on unwatch and page fault"
```

---

### Task 2: Intra-Batch Delta Capture & Range Coalescing in `CaptureDevice` (Approach 1A)

**Files:**
- Modify: [`port/src/graphics/guest/captured_batch.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/graphics/guest/captured_batch.h)
- Modify: [`port/src/graphics/guest/captured_batch.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/graphics/guest/captured_batch.cpp)
- Modify: [`port/src/native_renderer/native_renderer.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.h)
- Modify: [`port/src/native_renderer/native_renderer.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp)
- Modify: [`tests/native/test_render_packets.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_render_packets.cpp)

- [ ] **Step 1: Inspect how `CaptureDevice` and `Load32` interact across a `WorkBatch`**

In [`port/src/native_renderer/native_renderer.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp):
- `CaptureDevice(const uint8_t* base)` (lines 9201–9237) is called on every draw when `cur_.packet_check` is `false`.
- Currently, every draw calls `CaptureBytes` up to 11 times:
  1. `profile::kDevicePtrAddr` (4B, skipped when `kDevicePtrAddr == 0`)
  2. `dev + 0` (`32` bytes: dirty masks `0..39`)
  3. `dev + kDevice.fetch_constants` (`0x80` bytes: 16 vertex fetch slots, `0x400..0x47F`)
  4. `dev + kDevice.register_shadow` (`0x1D0` bytes: `0x2800..0x29CF`)
  5. `dev + kDevice.vertex_decl - 4` (`8` bytes: `0x2D0C..0x2D13`)
  6. `dev + kDevice.index_buffer - 0xC` (`0x120` bytes: `0x2F78..0x3097`, covering `index_buffer`, `render_targets[0..3]`, `depth_stencil`, `stream_buffers[0..15]`, `stream_strides[0..15]`, `textures[0..25]`, `viewport`, `shader_a`, `shader_b`)
  7. `rt0..rt3` (`4 × 0x28` bytes at indirect surface pointers)
  8. `ds` (`0x28` bytes at indirect depth-stencil pointer)
  9. `decl` (`0x34 + 12 * elem_count` bytes at indirect vertex declaration pointer)
- On the worker (`native_renderer.cpp:9513`), `t_capture_ranges = batch.ranges.data() + cmd.range_first`, `t_capture_range_count = cmd.range_count`, and `Load32(base, addr)` linearly scans `t_capture_ranges[0..t_capture_range_count)`.
- Furthermore, when `cur_.packet_check` is `false`, look at what the worker actually reads via `Load32` during `ExecuteDraw`:
  - `ExecuteDraw` uses `cmd.packet` (`DrawPacket`), `cmd.state` (`DrawState`), and `cmd.constants` (`ConstantSnapshot`)? Wait: let's check what happens when `cur_.packet_check` is `false`!
  - In `EnqueueDraw` (`native_renderer.cpp:9265-9330`), when `cur_.packet_check` is `false`:
    - `cmd.has_packet = false`!
    - The guest thread calls `CaptureDevice(base)` and `CaptureDrawState(cmd.state)` (`cmd.state.pm4 = pm4_.snapshot()`, `cmd.state.device_ptr = device_ptr_`), and then on the worker (`native_renderer.cpp:9535`), `BuildPacketFromCapturedState(base, cmd)` runs on the worker thread using `t_capture_ranges` (`Load32`)!
- Look at `CaptureDevice` again:
  - For every draw in a `WorkBatch`, `rt0..rt3` (`4 × 0x28` bytes), `ds` (`0x28` bytes), and `decl` (`0x34 + 12*n` bytes) are indirect guest objects (`D3DSurface`, `D3DVertexDeclaration`) whose contents in guest memory are immutable descriptors while bound!
  - Even better: if `Load32` searches `cmd`'s ranges FIRST and then falls back to a small per-batch shared descriptor cache (`batch.shared_ranges` for `rt0..rt3`, `ds`, `decl` deduplicated by `(guest_addr, size)` within the `WorkBatch`), then:
    1. `rt0..rt3`, `ds`, and `decl` are copied into `batch.bytes` **once per unique pointer per `WorkBatch`** instead of 1,450 times per frame!
    2. Each draw's `cmd.range_count` shrinks from 10 ranges down to **4 ranges** (`dev[0..32]`, `dev+0x400`, `dev+0x2800`, `dev+0x2D0C`, `dev+0x2F78` — and `dev+0x2800..0x3097` is only `0x898` = 2,200 bytes total, or we can keep the 4 contiguous device slices so each draw has only 4 `CaptureRange` items instead of 10!).
    3. Fewer `CaptureRange` items per draw cuts both guest `CaptureBytes` overhead AND worker `Load32` linear scan iterations by >50%!

- [ ] **Step 2: Write a failing unit test in `tests/native/test_render_packets.cpp` for batch-shared descriptor range lookup**

Add a unit test in [`tests/native/test_render_packets.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_render_packets.cpp) testing a helper `CaptureSharedBatchRange(WorkBatch& batch, uint32_t addr, const void* data, uint32_t size)` and `FindCapturedBytes(std::span<const CaptureRange> cmd_ranges, std::span<const CaptureRange> shared_ranges, std::span<const uint8_t> batch_bytes, uint32_t addr, uint32_t size)`:
- Verify that capturing the same surface/declaration descriptor `(addr, size)` 100 times in one `WorkBatch` appends its bytes and `CaptureRange` to `batch.shared_ranges` only **once** when the content at `addr` is unchanged.
- Verify that if the guest mutates the descriptor bytes at `addr` mid-batch, `CaptureSharedBatchRange` detects the byte difference (`std::memcmp`) and updates/appends a new range so correctness is 100% preserved.
- Verify that `FindCapturedBytes` resolves command-local ranges first and `shared_ranges` second.

- [ ] **Step 3: Run `sr_native_tests` to confirm the new test fails**

Run:
```powershell
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native -R shared_batch_range --output-on-failure
```
Expected: Fails to compile until `shared_ranges` and the helpers are added to [`port/src/graphics/guest/captured_batch.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/graphics/guest/captured_batch.h).

- [ ] **Step 4: Implement `shared_ranges` deduplication in `WorkBatch`, `CaptureDevice`, and `Load32`**

1. In [`port/src/graphics/guest/captured_batch.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/graphics/guest/captured_batch.h):
   - Add `std::vector<CaptureRange> shared_ranges;` to `WorkBatch` and clear it in `WorkBatch::Clear()` (`captured_batch.cpp`), reserving `64` entries.
   - Add inline helpers in `captured_batch.h`:
     ```cpp
     inline void CaptureSharedRange(WorkBatch& batch, uint32_t addr, const void* src, uint32_t size) {
       if (!src || !size) return;
       for (auto it = batch.shared_ranges.rbegin(); it != batch.shared_ranges.rend(); ++it) {
         if (it->addr == addr && it->size >= size) {
           if (std::memcmp(batch.bytes.data() + it->offset, src, size) == 0) return;
           break;
         }
       }
       const uint32_t offset = static_cast<uint32_t>(batch.bytes.size());
       const auto* p = static_cast<const uint8_t*>(src);
       batch.bytes.insert(batch.bytes.end(), p, p + size);
       batch.shared_ranges.push_back({addr, size, offset});
     }

     inline const uint8_t* LookupCapturedRange(
         const CaptureRange* cmd_ranges, uint32_t cmd_count,
         const CaptureRange* shared_ranges, uint32_t shared_count,
         const uint8_t* batch_bytes, uint32_t addr, uint32_t size) {
       for (uint32_t i = 0; i < cmd_count; ++i) {
         const auto& r = cmd_ranges[i];
         if (addr >= r.addr && addr + size <= r.addr + r.size) {
           return batch_bytes + r.offset + (addr - r.addr);
         }
       }
       for (uint32_t i = shared_count; i > 0; --i) {
         const auto& r = shared_ranges[i - 1];
         if (addr >= r.addr && addr + size <= r.addr + r.size) {
           return batch_bytes + r.offset + (addr - r.addr);
         }
       }
       return nullptr;
     }
     ```
2. In [`port/src/native_renderer/native_renderer.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp):
   - Add `thread_local const CaptureRange* t_shared_capture_ranges = nullptr;` and `thread_local uint32_t t_shared_capture_range_count = 0;` alongside `t_capture_ranges` (around line 855).
   - Update `Load32` (around line 860) to check `LookupCapturedRange(t_capture_ranges, t_capture_range_count, t_shared_capture_ranges, t_shared_capture_range_count, t_capture_bytes, addr, 4)`.
   - In `CaptureDevice` (lines 9201–9237), use `CaptureSharedBytes` (wrapping `CaptureSharedRange(cur_, addr, base + addr, size)`) for `rt0..rt3`, `ds`, and `decl`.
   - In `Execute(const WorkBatch& batch)` (around line 9513), set `t_shared_capture_ranges = batch.shared_ranges.data()` and `t_shared_capture_range_count = uint32_t(batch.shared_ranges.size())` and reset them to `nullptr` / `0` when the batch finishes.

- [ ] **Step 5: Run unit tests and build the main executable**

Run:
```powershell
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native --output-on-failure
cmake --build build/linux-debug --target superman_returns_recomp
```
Expected: All unit tests in `sr_native_tests` pass and `superman_returns_recomp.exe` links cleanly.

- [ ] **Step 6: Run the 60s A/B benchmark for Task 2**

Run:
```powershell
python tools/bench/d3d12_fps.py --exe build/linux-debug/bin/Debug/superman_returns_recomp.exe --assets extracted --warmup 5 --seconds 60 --out artifacts/d3d12-fps-20261104/bench_task2_capture_device.json
```
Record FPS and ms/frame vs Task 1.

- [ ] **Step 7: Commit Task 2**

```powershell
git add port/src/graphics/guest/captured_batch.h port/src/graphics/guest/captured_batch.cpp port/src/native_renderer/native_renderer.h port/src/native_renderer/native_renderer.cpp tests/native/test_render_packets.cpp
git commit -m "perf(native): deduplicate immutable surface and vertex decl captures per WorkBatch"
```

---

### Task 3: Worker `SlotCache` Set-Associative Expansion (Approach 3B)

**Files:**
- Create: [`port/src/native_renderer/slot_srv_cache.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/slot_srv_cache.h)
- Modify: [`port/src/native_renderer/native_renderer.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.h)
- Modify: [`port/src/native_renderer/native_renderer.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp)
- Modify: [`tests/native/CMakeLists.txt`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/CMakeLists.txt)
- Create: [`tests/native/test_slot_srv_cache.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_slot_srv_cache.cpp)

- [ ] **Step 1: Write failing unit tests for `SlotSrvCache` in `tests/native/test_slot_srv_cache.cpp`**

Currently, `Renderer::SlotCache` in [`port/src/native_renderer/native_renderer.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.h#L961-L976) has `srv_entries[8]` scanned linearly with round-robin eviction (`srv_next = (srv_next + 1) & 7`). When a frame binds >8 distinct textures to slot 0 across 1,450 draws, the ring thrashing causes **404 `slot_misses` per frame** (`game.log:1607`), each falling back into `GetTextureSrvIndex` -> `TextureStorageFetch` -> `XXH3_64bits` -> `textures_.find`.

Create [`tests/native/test_slot_srv_cache.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_slot_srv_cache.cpp) testing a pure header `SlotSrvCache` (32 sets × 2 ways = 64 entries per slot, indexed by `SlotSrvSetIndex(const uint32_t fetch[6])`):
- Test 1 (`slot_srv_cache_retains_working_set_beyond_eight_textures`): Insert 32 distinct texture `fetch[6]` descriptors into a single slot and re-query all 32 in a loop; verify 100% hit rate (where the old 8-entry ring had 0% hit rate!).
- Test 2 (`slot_srv_cache_two_way_set_associative_lru_eviction`): Force 3 distinct `fetch[6]` descriptors into the same set index; verify the 2 most recently used entries stay cached and only the least recently used entry in that set is evicted.
- Test 3 (`slot_srv_cache_checks_all_six_fetch_words_and_invalidates_cleanly`): Flip any bit in `fetch[0..5]` and verify it misses; call `Invalidate()` and verify all 64 entries are cleared.

- [ ] **Step 2: Register `test_slot_srv_cache.cpp` in `tests/native/CMakeLists.txt` and verify it fails to build**

Add `test_slot_srv_cache.cpp` to `add_executable(sr_native_tests ...)` in [`tests/native/CMakeLists.txt`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/CMakeLists.txt#L18-L35).
Run:
```powershell
cmake --build build/tests-native --target sr_native_tests
```
Expected: Fails because `slot_srv_cache.h` does not exist yet.

- [ ] **Step 3: Implement `port/src/native_renderer/slot_srv_cache.h` and integrate into `Renderer::SlotCache`**

Create [`port/src/native_renderer/slot_srv_cache.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/slot_srv_cache.h):
```cpp
#pragma once
#include <cstdint>
#include <cstring>

namespace superman_returns::native {

struct SlotSrvCache {
  static constexpr uint32_t kNumSets = 32;
  static constexpr uint32_t kNumWays = 2;

  struct Entry {
    uint32_t fetch[6] = {};
    uint32_t srv = 0;
    uint32_t format = 0;
    uint32_t swz_r = 0;
    uint32_t fetch_version = 0;
    bool valid = false;
  };

  struct Set {
    Entry ways[kNumWays] = {};
    uint8_t mru = 0;
  };

  Set sets[kNumSets] = {};

  static constexpr uint32_t SetIndex(const uint32_t fetch[6]) {
    // Mix base address word (fetch[1]) with format/size/endian words (fetch[0], fetch[2..5])
    uint32_t h = fetch[1] ^ (fetch[1] >> 12) ^ (fetch[4] * 0x9E3779B9u) ^ fetch[2] ^ fetch[5];
    return (h ^ (h >> 5)) & (kNumSets - 1);
  }

  Entry* Lookup(const uint32_t fetch[6], uint32_t expected_version = 0) {
    Set& set = sets[SetIndex(fetch)];
    for (uint32_t w = 0; w < kNumWays; ++w) {
      Entry& e = set.ways[w];
      if (e.valid && (expected_version == 0 || e.fetch_version == expected_version) &&
          std::memcmp(e.fetch, fetch, sizeof(e.fetch)) == 0) {
        set.mru = static_cast<uint8_t>(w);
        return &e;
      }
    }
    return nullptr;
  }

  void Insert(const uint32_t fetch[6], uint32_t srv, uint32_t format,
              uint32_t swz_r, uint32_t fetch_version) {
    Set& set = sets[SetIndex(fetch)];
    for (uint32_t w = 0; w < kNumWays; ++w) {
      Entry& e = set.ways[w];
      if (!e.valid || std::memcmp(e.fetch, fetch, sizeof(e.fetch)) == 0) {
        std::memcpy(e.fetch, fetch, sizeof(e.fetch));
        e.srv = srv;
        e.format = format;
        e.swz_r = swz_r;
        e.fetch_version = fetch_version;
        e.valid = true;
        set.mru = static_cast<uint8_t>(w);
        return;
      }
    }
    const uint32_t victim = set.mru ^ 1u;
    Entry& e = set.ways[victim];
    std::memcpy(e.fetch, fetch, sizeof(e.fetch));
    e.srv = srv;
    e.format = format;
    e.swz_r = swz_r;
    e.fetch_version = fetch_version;
    e.valid = true;
    set.mru = static_cast<uint8_t>(victim);
  }

  void Invalidate() {
    for (auto& set : sets) {
      for (auto& w : set.ways) w.valid = false;
      set.mru = 0;
    }
  }
};

}  // namespace superman_returns::native
```

Update `Renderer::SlotCache` in [`port/src/native_renderer/native_renderer.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.h#L961-L976) and `UploadConstants` in [`port/src/native_renderer/native_renderer.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp#L4649-L4716) to use `SlotSrvCache srv_cache;` instead of the 8-entry linear array `srv_entries[8]`.

Also in `GetTextureSrvIndex` (`native_renderer.cpp:5266`), before computing `TextureStorageFetch` and `XXH3_64bits`, check a per-frame fast lookup table or rely on `SlotSrvCache` when `fetch_version == pm4_.fetch_version(slot)` or `std::memcmp(e.fetch, fetch, 24) == 0`!
Notice why `SlotCache` in `UploadConstants` (`native_renderer.cpp:4652-4665`) had Two paths:
1. Fast path (`fv != 0 && e.fetch_version == fv`): didn't even need `std::memcmp(e.fetch, fetch, 24)` when `e == srv_entries[0]`? Wait: `pm4_.fetch_version(slot)` increments every time the guest writes to `fetch_constants` for `slot`! So when texture A is bound to slot 0 (`fv = 100`), then texture B is bound to slot 0 (`fv = 101`), and then texture A is bound to slot 0 again (`fv = 102`), `fv` is now `102`, NOT `100`!
2. Look at `native_renderer.cpp:4654-4665`:
   ```cpp
   for (uint32_t i = 0; i < 8; ++i) {
     const auto& e = slot_cache_.srv_entries[slot][i];
     if (e.valid && ((fv != 0 && e.fetch_version == fv) ||
                     std::memcmp(e.fetch, fetch, sizeof(fetch)) == 0)) {
       ...
     }
   }
   ```
   Because `slot_cache_` is cleared at the start of every frame (`InvalidateSlots()` at `BeginFrame`, `native_renderer.cpp:3651`), any entry in `slot_cache_` was ALREADY validated in the **current frame** (`frame_count_`)!
   With only 8 entries per slot (`srv_entries[slot][8]`), binding >8 textures to slot 0 evicted texture A before it was bound again in the same frame!
   By replacing `srv_entries[slot][8]` with `SlotSrvCache srv_cache[32]` (64 entries per slot, 2-way set-associative indexed by `SetIndex(fetch)`) AND keeping a 1-entry `last_fetch_version` shortcut per slot for consecutive draws with identical `fv`, `UploadConstants` finds `e` in O(1) (at most 2 comparisons in `sets[SetIndex(fetch)]`) for up to 64 distinct textures per slot per frame!

- [ ] **Step 4: Run `sr_native_tests` and build `superman_returns_recomp`**

Run:
```powershell
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native --output-on-failure
cmake --build build/linux-debug --target superman_returns_recomp
```
Expected: All tests in `sr_native_tests` (including `test_slot_srv_cache`) pass and `superman_returns_recomp` builds cleanly.

- [ ] **Step 5: Run the 60s A/B benchmark for Task 3**

Run:
```powershell
python tools/bench/d3d12_fps.py --exe build/linux-debug/bin/Debug/superman_returns_recomp.exe --assets extracted --warmup 5 --seconds 60 --out artifacts/d3d12-fps-20261104/bench_task3_slot_cache.json
```
Check `game.log` (`slot_hits` vs `slot_misses`) and record the FPS and ms/frame improvement.

- [ ] **Step 6: Commit Task 3**

```powershell
git add port/src/native_renderer/slot_srv_cache.h port/src/native_renderer/native_renderer.h port/src/native_renderer/native_renderer.cpp tests/native/CMakeLists.txt tests/native/test_slot_srv_cache.cpp
git commit -m "perf(native): replace 8-entry SlotCache ring with 64-entry 2-way set-associative cache"
```

---

### Task 4: Intra-Batch Stream Validation Dedup in `PlanStreams` (Approach 3A)

**Files:**
- Modify: [`port/src/native_renderer/buffer_tracking_policy.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/buffer_tracking_policy.h)
- Modify: [`port/src/native_renderer/native_renderer.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.h)
- Modify: [`port/src/native_renderer/native_renderer.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp)
- Modify: [`tests/native/test_buffer_tracking_policy.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_buffer_tracking_policy.cpp)

- [ ] **Step 1: Inspect `PlanStreams` and `RefreshTrackedBuffer` in `native_renderer.cpp`**

Read [`port/src/native_renderer/native_renderer.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp#L5819-L6000) and [`port/src/native_renderer/buffer_tracking_policy.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/buffer_tracking_policy.h):
- `PlanStreams` runs on the guest thread for every non-inline draw (`~1,450 draws/frame`).
- For each vertex stream (and index buffer), it looks up `buffers_[(phys_addr << 32) | byte_len]` and calls `RefreshTrackedBuffer(base, phys_addr, byte_len, entry)`.
- Inside `RefreshTrackedBuffer` (`native_renderer.cpp:5826-5900`), even when a static/shared vertex or index buffer was ALREADY validated in an earlier draw of the **same 128-draw `WorkBatch`** (or after the most recent `InvalidateGuestRange` call), it repeatedly checks `BufferProofHolds(entry)` (sampling write-watch stamps or running `HashGuestBytes`).
- If we maintain a monotonic `guest_buffer_invalidation_serial_` (incremented on `InvalidateGuestRange` and on `BeginFrame` / `FlushBatch`) and store `uint64_t validated_batch_serial = 0;` on `TrackedBuffer`:
  - When `entry.validated_batch_serial == current_stream_validation_serial_` AND `BufferTrackingModeValue() != BufferTrackingMode::Compare` (so compare auditing still runs on every check when enabled), `RefreshTrackedBuffer` can immediately return `true` if `entry.watch != 0` (covered by write-watch, or already hashed in the current validation epoch without any `VertexBuffer_Unlock` / `IndexBuffer_Unlock` since)!
  - Even simpler and completely safe under write-watch: when `entry.validated_invalidation_serial == guest_buffer_invalidation_serial_` AND `entry.validated_frame == frame_count_` AND `entry.proof.watch == 0` (meaning write-watch is off or unavailable for this buffer, and no `VertexBuffer_Unlock` / `IndexBuffer_Unlock` has occurred since it was hashed earlier in this same `WorkBatch`), or when `entry.proof.watch != 0` and `entry.validated_batch_serial == current_batch_serial_` and `FastViewSample` has no page epoch change, we avoid redundant hash/map work across consecutive draws sharing the same VB/IB!

- [ ] **Step 2: Write failing unit tests in `tests/native/test_buffer_tracking_policy.cpp`**

Add tests in [`tests/native/test_buffer_tracking_policy.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_buffer_tracking_policy.cpp) for `ShouldSkipIntraBatchBufferCheck(BufferTrackingMode mode, uint64_t current_batch_serial, uint64_t last_validated_batch_serial, uint64_t current_unlock_serial, uint64_t last_validated_unlock_serial, bool has_valid_gpu_buffer)`:
- Returns `true` when `mode != BufferTrackingMode::Compare`, `has_valid_gpu_buffer == true`, `current_batch_serial != 0`, `current_batch_serial == last_validated_batch_serial`, and `current_unlock_serial == last_validated_unlock_serial`.
- Returns `false` when `current_unlock_serial != last_validated_unlock_serial` (an `InvalidateGuestRange` / buffer unlock occurred mid-batch), when `current_batch_serial != last_validated_batch_serial`, or when `mode == BufferTrackingMode::Compare`.

- [ ] **Step 3: Run `sr_native_tests` to verify the test fails**

Run:
```powershell
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native -R buffer_tracking_policy --output-on-failure
```
Expected: Fails to compile until `ShouldSkipIntraBatchBufferCheck` is added to [`port/src/native_renderer/buffer_tracking_policy.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/buffer_tracking_policy.h).

- [ ] **Step 4: Implement `ShouldSkipIntraBatchBufferCheck` and wire it into `RefreshTrackedBuffer` and `InvalidateGuestRange`**

1. Add `ShouldSkipIntraBatchBufferCheck` to [`port/src/native_renderer/buffer_tracking_policy.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/buffer_tracking_policy.h).
2. In [`port/src/native_renderer/native_renderer.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.h):
   - Add `uint64_t validated_batch_serial = 0;` and `uint64_t validated_unlock_serial = 0;` to `TrackedBuffer`.
   - Add `uint64_t stream_batch_serial_ = 1;` (incremented in `FlushBatch` and `OnSwap`) and `std::atomic<uint64_t> buffer_unlock_serial_{1};` (incremented in `InvalidateGuestRange`).
3. In `RefreshTrackedBuffer` ([`port/src/native_renderer/native_renderer.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_renderer.cpp#L5826)):
   - Read `const uint64_t unlock_serial = buffer_unlock_serial_.load(std::memory_order_acquire);`.
   - If `ShouldSkipIntraBatchBufferCheck(mode, stream_batch_serial_, entry.validated_batch_serial, unlock_serial, entry.validated_unlock_serial, entry.versions.back().resource != nullptr)` holds AND (if `entry.proof.watch == 0` or `BufferProofHolds(entry)`), skip re-hashing and stamp `entry.validated_batch_serial = stream_batch_serial_; entry.validated_unlock_serial = unlock_serial;`.
   - Even better: when `ShouldSkipIntraBatchBufferCheck(...)` holds and `entry.proof.watch == 0` (fallback hashing path), we skip `HashGuestBytes` for draws within the same batch that share the same VB/IB as long as no `InvalidateGuestRange` occurred!

- [ ] **Step 5: Run unit tests, build `superman_returns_recomp`, and run the 60s A/B benchmark**

Run:
```powershell
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native --output-on-failure
cmake --build build/linux-debug --target superman_returns_recomp
python tools/bench/d3d12_fps.py --exe build/linux-debug/bin/Debug/superman_returns_recomp.exe --assets extracted --warmup 5 --seconds 60 --out artifacts/d3d12-fps-20261104/bench_task4_plan_streams.json
```

- [ ] **Step 6: Commit Task 4**

```powershell
git add port/src/native_renderer/buffer_tracking_policy.h port/src/native_renderer/native_renderer.h port/src/native_renderer/native_renderer.cpp tests/native/test_buffer_tracking_policy.cpp
git commit -m "perf(native): skip redundant intra-batch stream buffer re-hashing between buffer unlocks"
```

---

### Task 5: Native C++/SIMD Reimplementation of Hot Math & Culling Leaf Functions `sub_82319108`, `sub_82305B88`, and `sub_823055A8` (Approach 2A)

**Files:**
- Create: [`port/src/native_renderer/guest_math_hooks.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/guest_math_hooks.h)
- Modify: [`port/src/native_renderer/game_profile.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/game_profile.h)
- Modify: [`port/src/native_renderer/native_hooks.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_hooks.cpp)
- Modify: [`tests/xdk/CMakeLists.txt`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/CMakeLists.txt)
- Modify: [`tests/xdk/test_xdk_constant_copy.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/test_xdk_constant_copy.cpp)

- [ ] **Step 1: Inspect the generated PowerPC code for `sub_82319108`, `sub_82305B88`, and `sub_823055A8`**

Locate and read the exact generated definitions in `port/generated/default/superman_returns_recomp.*.cpp` (and [`artifacts/d3d12-fps-20261104/823055A8.txt`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/artifacts/d3d12-fps-20261104/823055A8.txt)):
- `sub_82319108` (`~11,000 calls/frame`, `~0.8 ms/frame` self time): leaf/near-leaf helper called inside `sub_823055A8` and scene traversal.
- `sub_82305B88` (`~680 calls/frame`, `~0.9 ms/frame` self time): companion spatial/visibility helper called alongside `sub_823055A8`.
- `sub_823055A8` (`~380 calls/frame`, `~2.35 ms/frame` self time): VMX frustum/bounding-volume culling and visibility list builder (saves/restores 14 GPRs + 6 VMX registers on every call and executes ~180 scalar/VMX big-endian stack loads/stores per iteration).

- [ ] **Step 2: Extract `sub_82319108` and `sub_82305B88` (and leaf callees) into `tests/xdk/` for differential oracle testing**

In [`tests/xdk/CMakeLists.txt`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/CMakeLists.txt), use the existing `extract_constant_reference` CMake function to extract `ref_sub_82319108` and `ref_sub_82305B88` (and `ref_sub_823055A8` / its pure leaf helpers) from `port/generated/default/superman_returns_recomp.*.cpp`.
Add differential oracle tests in [`tests/xdk/test_xdk_constant_copy.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/test_xdk_constant_copy.cpp) that initialize two identical guest memory arenas and `PPCContext` structs, run `ref_sub_*` on one and `Native_sub_*` on the other across randomized and edge-case inputs (zero counts, positive/negative floats, boundary flags), and assert identical `ctx` register outputs and guest memory state.

- [ ] **Step 3: Implement `port/src/native_renderer/guest_math_hooks.h` and wire the hooks in `native_hooks.cpp`**

1. Create [`port/src/native_renderer/guest_math_hooks.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/guest_math_hooks.h) containing the pure native C++/SIMD implementations of `sub_82319108`, `sub_82305B88`, and `sub_823055A8` (with an env-var kill switch `SR_NATIVE_GUEST_MATH=0` defaulting to enabled when `native::Enabled()` is true).
2. Add confirmed hook roles in [`port/src/native_renderer/game_profile.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/game_profile.h) and `SR_DEFINE_HOOK` entries in [`port/src/native_renderer/native_hooks.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_hooks.cpp).

- [ ] **Step 4: Run `sr_xdk_draw_tests`, `sr_native_tests`, and build `superman_returns_recomp`**

Run:
```powershell
cmake -S tests/xdk -B build/tests-xdk
cmake --build build/tests-xdk
ctest --test-dir build/tests-xdk --output-on-failure
cmake --build build/linux-debug --target superman_returns_recomp
```
Expected: 100% pass on differential oracle tests and clean build of `superman_returns_recomp.exe`.

- [ ] **Step 5: Run the 60s A/B benchmark for Task 5**

Run:
```powershell
python tools/bench/d3d12_fps.py --exe build/linux-debug/bin/Debug/superman_returns_recomp.exe --assets extracted --warmup 5 --seconds 60 --out artifacts/d3d12-fps-20261104/bench_task5_guest_math.json
```
Record FPS and ms/frame improvement.

- [ ] **Step 6: Commit Task 5**

```powershell
git add port/src/native_renderer/guest_math_hooks.h port/src/native_renderer/game_profile.h port/src/native_renderer/native_hooks.cpp tests/xdk/CMakeLists.txt tests/xdk/test_xdk_constant_copy.cpp
git commit -m "perf(guest): native C++/SIMD reimplementation of hot culling and math routines"
```

---

### Task 6: Direct XDK Device-State Constant Capture / PM4 Ring Bypass (Approach 1C)

**Files:**
- Modify: [`port/src/native_renderer/xdk_draw_submission.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/xdk_draw_submission.h)
- Modify: [`port/src/native_renderer/pm4_mirror.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/pm4_mirror.h)
- Modify: [`port/src/native_renderer/pm4_mirror.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/pm4_mirror.cpp)
- Modify: [`port/src/native_renderer/native_hooks.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_hooks.cpp)
- Modify: [`tests/native/test_xdk_draw_submission.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_xdk_draw_submission.cpp)

- [ ] **Step 1: Inspect the round-trip between `SubmitNativeXdkDraw` and `Pm4Mirror::Sync`**

Look at [`port/src/native_renderer/xdk_draw_submission.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/xdk_draw_submission.h#L310-L450) and [`port/src/native_renderer/native_hooks.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_hooks.cpp#L632-L659):
- Currently, `TryNativeDrawSubmission` (`native_hooks.cpp:475`) is gated behind `SR_NATIVE_XDK_DRAW_SUBMISSION=1` (off by default!).
- Why was it off or what did it do? When enabled, `SubmitNativeXdkDraw` reads the dirty bitmasks at `device[0..39]`, formats Type-0/Type-3 PM4 packets into a 12 KB stack buffer `commands`, copies `commands` into the guest physical command ring (`ring.data()`), advances `device[40]` (`ring_write`), and then `Renderer::Get().DrawVertices` immediately calls `SyncRing` (`pm4_.Sync(base, dev)`), which parses those exact PM4 words back out of the guest ring and byte-swaps them into `pm4_.regs_`!
- Even worse: if `SubmitNativeXdkDraw` fills the current command ring segment (`uint64_t(old_write) + size > ReadXdkWord(device, 48)`), it returns `XdkDrawResult::RingFull` and falls back to `SR_ORIGINAL(SR_ADDR_DRAW_INDEXED_VERTICES)`, which calls `RingMakeSpace` (`sub_820FD8C0`) to wrap the ring!
- Wait: what if `SubmitNativeXdkDraw` is enabled by default in native D3D12 mode (when `cur_.packet_check == false`), AND instead of formatting `dirty_words(read64(0), 0x4000, 0x700, 16)` (VS float constants) and `dirty_words(read64(8), 0x4400, 0x1700, 16)` (PS float constants) into the guest command ring, we apply the dirty constant groups and register shadow groups **directly** to `Pm4Mirror` (`Renderer::Get().ApplyNativeXdkDeviceDirtyState(device, ...)`) while only emitting the tiny draw/shader packets (or keeping ring advancement minimal so the ring almost never wraps)?
- Wait: let's check if anything in the guest reads the command ring back!
  - Does the CPU ever read the PM4 command ring in `superman_returns_recomp` when the native D3D12 renderer is active?
  - Let's check `native_graphics_system.cpp` and `native_hooks.cpp`: in native renderer mode, the Xenos command processor thread is **disabled**! Nothing reads the guest PM4 command ring except `Pm4Mirror::Sync` (`native_renderer.cpp:9181`)!
  - Let's verify that in `native_graphics_system.cpp` and `pm4_mirror.cpp`! If nothing else reads the PM4 command ring in native D3D12 mode, then:
    1. Enabling `SubmitNativeXdkDraw` by default (with env-var override `SR_NATIVE_XDK_DRAW_SUBMISSION=0` to disable) immediately replaces the recompiled PowerPC `sub_820FBBF8` / `sub_820FC000` (`1.63 ms/frame` guest CPU)!
    2. Adding a direct `ApplyDirtyDeviceStateToMirror` callback to `SubmitNativeXdkDraw` (when `cur_.packet_check == false`) applies dirty VS/PS float constants, fetch constants, and context/misc registers directly into `Pm4Mirror` without formatting them into `commands` or copying them into `ring` AND without `Pm4Mirror::Sync` having to parse them back out (`0.40 ms/frame` saved in `SyncRing` + fewer `RingMakeSpace` wraps)!

- [ ] **Step 2: Write unit tests in `tests/native/test_xdk_draw_submission.cpp` for direct mirror synchronization**

Add a test `xdk_native_draw_direct_mirror_sync_matches_ring_scan_bit_for_bit` in [`tests/native/test_xdk_draw_submission.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/native/test_xdk_draw_submission.cpp):
- Set up a `device` with dirty VS constants (`dev+0`), dirty PS constants (`dev+8`), dirty `state` (`dev+16`, including `common_shader`), dirty `context` (`dev+24`, including `bools`), and dirty `misc` (`dev+32`).
- Run `SubmitNativeXdkDraw` + `mirror_a.ScanCopy(...)` on copy A.
- Run `SubmitNativeXdkDrawDirectToMirror` on copy B with `mirror_b`.
- Assert that `mirror_b.reg(r) == mirror_a.reg(r)` and `mirror_b.written(r) == mirror_a.written(r)` for all registers `0x2000..0x4FFF`, `mirror_b.fetch_version(s) == mirror_a.fetch_version(s)` for all 32 slots, and `device` post-state is identical (except `device[40]` ring write pointer if ring write is bypassed or minimal).

- [ ] **Step 3: Run `sr_native_tests` to verify the new test fails**

Run:
```powershell
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native -R xdk_native_draw_direct_mirror --output-on-failure
```
Expected: Fails to compile until `SubmitNativeXdkDrawDirectToMirror` is implemented.

- [ ] **Step 4: Implement `SubmitNativeXdkDrawDirectToMirror`, enable `NativeXdkDrawSubmissionEnabled()` by default, and wire into `native_hooks.cpp`**

1. In [`port/src/native_renderer/pm4_mirror.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/pm4_mirror.h), add a fast bulk setter `WriteBigEndianRange(uint32_t first_reg, const uint8_t* be_words, uint32_t count)` that byte-swaps `count` 32-bit words from `be_words` into `regs_[first_reg .. first_reg + count - 1]`, updates `written_`, and bumps `fetch_versions_` if `first_reg` overlaps `0x4800..0x48BF`.
2. In [`port/src/native_renderer/xdk_draw_submission.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/xdk_draw_submission.h), add `SubmitNativeXdkDrawDirectToMirror` (or an optional `Pm4Mirror* direct_mirror` parameter to `SubmitNativeXdkDraw`) so that when `direct_mirror != nullptr`, dirty register blocks are written straight into `direct_mirror` without overflowing or touching the guest ring buffer.
3. In [`port/src/native_renderer/native_hooks.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_hooks.cpp), change `NativeXdkDrawSubmissionEnabled()` to default to `true` (unless `SR_NATIVE_XDK_DRAW_SUBMISSION=0`), and before calling `SubmitNativeXdkDraw`, call `native::Renderer::Get().SyncRing(base, dev)` if the ring write pointer moved since the last draw, then pass `&native::Renderer::Get().pm4_mirror()` when packet compare mode is off.

- [ ] **Step 5: Run unit tests, build `superman_returns_recomp`, and run the 60s A/B benchmark**

Run:
```powershell
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native --output-on-failure
cmake --build build/linux-debug --target superman_returns_recomp
python tools/bench/d3d12_fps.py --exe build/linux-debug/bin/Debug/superman_returns_recomp.exe --assets extracted --warmup 5 --seconds 60 --out artifacts/d3d12-fps-20261104/bench_task6_xdk_direct_mirror.json
```
Verify visual output and record FPS and ms/frame.

- [ ] **Step 6: Commit Task 6**

```powershell
git add port/src/native_renderer/xdk_draw_submission.h port/src/native_renderer/pm4_mirror.h port/src/native_renderer/pm4_mirror.cpp port/src/native_renderer/native_renderer.h port/src/native_renderer/native_hooks.cpp tests/native/test_xdk_draw_submission.cpp
git commit -m "perf(xdk): enable native XDK draw submission and direct Pm4Mirror state sync"
```

---

### Task 7: Native C++ Reimplementation of Engine Draw Setup (`sub_822F5290`, `sub_82316958`, `sub_8230F8E0`) & Scene Dispatcher (`sub_82783948`) (Approach 2B & 2C)

**Files:**
- Create: [`port/src/native_renderer/guest_engine_hooks.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/guest_engine_hooks.h)
- Modify: [`port/src/native_renderer/game_profile.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/game_profile.h)
- Modify: [`port/src/native_renderer/native_hooks.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_hooks.cpp)
- Modify: [`tests/xdk/CMakeLists.txt`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/CMakeLists.txt)
- Modify: [`tests/xdk/test_xdk_constant_copy.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/test_xdk_constant_copy.cpp)

- [ ] **Step 1: Inspect the generated PowerPC implementations of `sub_822F5290`, `sub_82316958`, `sub_8230F8E0`, and `sub_82783948`**

Read the generated functions in `port/generated/default/superman_returns_recomp.*.cpp`:
- `sub_822F5290` (`~1,100 calls/frame`, `2.6 ms/frame` self time): per-mesh material/shader constant binding dispatcher before `DrawIndexedVertices`.
- `sub_82316958` and `sub_8230F8E0` (`~1,400–1,480 calls/frame`, `2.5 ms/frame` combined self time): per-draw render state and stream setup helpers.
- `sub_82783948` (`~85 calls/frame`, `3.0 ms/frame` self time): scene-graph pass list iteration and sort-key dispatcher.

- [ ] **Step 2: Add differential oracle tests in `tests/xdk/`**

Extract the reference functions via `extract_constant_reference` in [`tests/xdk/CMakeLists.txt`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/CMakeLists.txt) (stubbing or intercepting external function calls with call-log recorders) and write differential tests in [`tests/xdk/test_xdk_constant_copy.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tests/xdk/test_xdk_constant_copy.cpp) verifying that `Native_sub_822F5290`, `Native_sub_82316958`, `Native_sub_8230F8E0`, and `Native_sub_82783948` produce identical guest memory mutations, callee invocation sequences, and register states as the generated PowerPC code.

- [ ] **Step 3: Implement `port/src/native_renderer/guest_engine_hooks.h` and register hooks in `game_profile.h` & `native_hooks.cpp`**

Implement the native C++ replacements in [`port/src/native_renderer/guest_engine_hooks.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/guest_engine_hooks.h) (guarded by `SR_NATIVE_GUEST_ENGINE=0` kill switch, enabled by default when `native::Enabled()` is true) and wire them in [`port/src/native_renderer/game_profile.h`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/game_profile.h) and [`port/src/native_renderer/native_hooks.cpp`](file:///c:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/native_hooks.cpp).

- [ ] **Step 4: Run `sr_xdk_draw_tests`, `sr_native_tests`, and build `superman_returns_recomp`**

Run:
```powershell
cmake --build build/tests-xdk
ctest --test-dir build/tests-xdk --output-on-failure
cmake --build build/tests-native --target sr_native_tests
ctest --test-dir build/tests-native --output-on-failure
cmake --build build/linux-debug --target superman_returns_recomp
```
Expected: All differential oracle and native unit tests pass.

- [ ] **Step 5: Run the final 60s A/B benchmark and verify >60 FPS (`<16.67 ms/frame`)**

Run:
```powershell
python tools/bench/d3d12_fps.py --exe build/linux-debug/bin/Debug/superman_returns_recomp.exe --assets extracted --warmup 5 --seconds 60 --out artifacts/d3d12-fps-20261104/bench_task7_final_60fps.json
```
Verify that average FPS exceeds `60.0 FPS` (`< 16.67 ms/frame`) on the 60s title flyover benchmark with zero visual regressions and zero missed-write compare errors.

- [ ] **Step 6: Commit Task 7**

```powershell
git add port/src/native_renderer/guest_engine_hooks.h port/src/native_renderer/game_profile.h port/src/native_renderer/native_hooks.cpp tests/xdk/CMakeLists.txt tests/xdk/test_xdk_constant_copy.cpp
git commit -m "perf(guest): native C++ reimplementation of draw setup and scene pass dispatchers"
```
