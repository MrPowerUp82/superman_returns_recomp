# CPU Graphics Command Processing Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce per-frame CPU overhead across PM4 command parsing/mirroring, front-end guest capture, and worker-thread draw preparation/replay for both D3D12 and Vulkan native backends.

**Architecture:** Implement incremental dirty-range tracking for PM4 float constant banks and single-slot scratch reads for live `LOAD_ALU_CONSTANT` packets; coalesce contiguous guest byte captures in `CaptureBytes`/`CaptureDevice`, route index buffer planning through slot 16 of `front_stream_cache_`, and iterate active vertex streams via precomputed declaration bitmasks; deduplicate D3D12 `IASetVertexBuffers` bindings, `BufferEntry` lookups, and `VertexDeclaration` scans, while eliminating duplicate constant writes and scalar memory lookups in Vulkan `DecodeCapturedPacket`.

**Tech Stack:** C++20, D3D12, Vulkan packet replay (`superman_returns::graphics::guest`), CMake/Ninja, `sr_native_tests` (`ctest`).

**Spec:** `docs/superpowers/specs/2026-10-26-cpu-graphics-command-optimization-design.md`

## Global Constraints

- Preserve all live guest memory safety invariants: `CheckedGuestReads` (`Read` for nested `INDIRECT_BUFFER`, `ReadScratch` for `LOAD_ALU_CONSTANT`) and `HostReadable` must reject partial or inaccessible pages without mutating register state.
- Coalesce `CaptureBytes` only when the new range is strictly contiguous in both guest virtual address (`last.address + last.length == address`) and batch byte arena offset (`last.offset + last.length == batch_->bytes.size()`) and belongs to the current command (`batch_->ranges.size() > cur_.range_first`).
- Keep all 483+ existing native unit tests passing (`ctest --test-dir port/out/build/win-amd64-write-tracking --output-on-failure`) and verify full executable link (`build.cmd`).

## Review Focus

- Partial float constant updates containing IEEE-754 `NaN` bit patterns must still be sanitized to `0` on incremental refreshes in `Pm4Mirror::FloatConstants(bool)`, while untouched registers in the same bank retain their previously cached values.
- `CheckedGuestReads::ReadScratch` called inside `LOAD_ALU_CONSTANT` while parsing a nested `INDIRECT_BUFFER` must not invalidate or reallocate the span owned by the outer `CheckedGuestReads::Read` slot.
- `CapturePm4Dependencies` must roll back `batch.bytes` and `batch.ranges` to their initial sizes if any referenced `LOAD_ALU_CONSTANT` or `INDIRECT_BUFFER` fails to read, without leaking or corrupting the `thread_local` scratch pool across subsequent calls.
- Coalescing contiguous `CaptureRange` entries in `CaptureBytes` must not merge ranges across command boundaries (`cur_.range_first`) or break `CapturedMemory::Read` / `ReadUnambiguous` when a later range partially overwrites an earlier range.
- Caching index buffers in `front_stream_cache_[16]` must immediately observe `InvalidateGuestRange` (`t.dirty = true`) and changes to `reset_index` or index format (`16-bit` vs `32-bit`, endianness).

---

### Task 1: Incremental Float Constant Dirty Ranges & Ephemeral Scratch Reads in `Pm4Mirror`

**Files:**
- Modify: `port/src/native_renderer/checked_guest_memory.h:15-41`
- Modify: `port/src/native_renderer/pm4_mirror.h:95-122`
- Modify: `port/src/native_renderer/pm4_mirror.cpp:21-39,73-115,266-277`
- Modify: `port/src/graphics/guest/pm4_capture.cpp:32-88`
- Test: `tests/native/test_checked_guest_memory.cpp`
- Test: `tests/native/test_pm4_mirror.cpp`

**Interfaces:**
- Consumes: `graphics::guest::CaptureFloatConstants(std::span<const uint32_t>, bool, std::array<uint32_t, 1024>&, std::string&)`
- Produces:
  - `std::span<const uint8_t> CheckedGuestReads::ReadScratch(const void* source, uint32_t length)` in `port/src/native_renderer/checked_guest_memory.h`
  - Incremental `Pm4Mirror::FloatConstants(bool pixel) const` backed by `float_dirty_min_[2]` and `float_dirty_max_[2]` in `port/src/native_renderer/pm4_mirror.h`

- [ ] **Step 1: Write the failing tests in `tests/native/test_checked_guest_memory.cpp` and `tests/native/test_pm4_mirror.cpp`**

Add to `tests/native/test_checked_guest_memory.cpp`:
```cpp
SR_TEST(checked_guest_reads_scratch_does_not_disturb_nested_slots) {
  CheckedGuestReads reads;
  std::array<uint8_t, 64> indirect{}, alu1{}, alu2{};
  indirect.fill(0xAA);
  alu1.fill(0x11);
  alu2.fill(0x22);
  auto outer = reads.Read(indirect.data(), uint32_t(indirect.size()));
  const auto* outer_ptr = outer.data();
  auto s1 = reads.ReadScratch(alu1.data(), uint32_t(alu1.size()));
  SR_CHECK_EQ(s1.size(), alu1.size());
  SR_CHECK_EQ(s1[0], 0x11u);
  auto s2 = reads.ReadScratch(alu2.data(), uint32_t(alu2.size()));
  SR_CHECK_EQ(s2.size(), alu2.size());
  SR_CHECK_EQ(s2[0], 0x22u);
  SR_CHECK(outer.data() == outer_ptr);
  SR_CHECK_EQ(outer[0], 0xAAu);
  SR_CHECK(reads.ReadScratch(nullptr, 16).empty());
}
```

Add to `tests/native/test_pm4_mirror.cpp`:
```cpp
SR_TEST(float_constants_incremental_dirty_range_sanitizes_nans_and_preserves_clean_words) {
  Pm4Mirror m;
  Stream s1;
  // Write VS c0.x = 1.0f, c0.y = quiet NaN (0x7FC00001), PS c255.w (offset 1023) = 2.0f.
  s1.Type0(Pm4Mirror::kAluConstantBase, {0x3F800000u, 0x7FC00001u, 0x40000000u, 0x40400000u});
  s1.Type0(Pm4Mirror::kAluConstantBase + 1024 + 1023, {0x40000000u});
  m.ScanCopy(nullptr, s1.bytes.data(), s1.size());

  const auto& vs1 = m.FloatConstants(false);
  const auto& ps1 = m.FloatConstants(true);
  SR_CHECK_EQ(vs1[0], 0x3F800000u);
  SR_CHECK_EQ(vs1[1], 0u); // NaN sanitized to 0
  SR_CHECK_EQ(vs1[2], 0x40000000u);
  SR_CHECK_EQ(vs1[3], 0x40400000u);
  SR_CHECK_EQ(ps1[1023], 0x40000000u);

  // Partial update: modify only VS c10.x (offset 40) to NaN and c10.y (offset 41) to 4.0f.
  Stream s2;
  s2.Type0(Pm4Mirror::kAluConstantBase + 40, {0x7F800005u, 0x40800000u});
  m.ScanCopy(nullptr, s2.bytes.data(), s2.size());

  const auto& vs2 = m.FloatConstants(false);
  SR_CHECK_EQ(vs2[0], 0x3F800000u);
  SR_CHECK_EQ(vs2[1], 0u);
  SR_CHECK_EQ(vs2[2], 0x40000000u);
  SR_CHECK_EQ(vs2[3], 0x40400000u);
  SR_CHECK_EQ(vs2[40], 0u);
  SR_CHECK_EQ(vs2[41], 0x40800000u);
  SR_CHECK_EQ(m.ShareFloatConstants(false)->at(41), 0x40800000u);
}
```

- [ ] **Step 2: Run the test binary to verify the new test fails to compile (`ReadScratch` missing)**

Run: `cmake --build port/out/build/win-amd64-write-tracking --target sr_native_tests`
Expected: FAIL with `no member named 'ReadScratch' in 'superman_returns::native::CheckedGuestReads'`.

- [ ] **Step 3: Implement `CheckedGuestReads::ReadScratch`, incremental dirty ranges in `Pm4Mirror`, and `thread_local` dependency scratch in `CapturePm4Dependencies`**

In `port/src/native_renderer/checked_guest_memory.h`:
- Add `std::vector<uint8_t> scratch_;` to `CheckedGuestReads`.
- Implement `std::span<const uint8_t> ReadScratch(const void* source, uint32_t length)` that validates `!source || !length || length > 0x20000000u`, resizes `scratch_` to `length`, calls `ReadProcessMemory(GetCurrentProcess(), source, scratch_.data(), length, &copied)`, and returns `scratch_` (or `{}` on failure). In `Reset()`, if `scratch_.capacity() > 64 * 1024`, swap it with an empty `std::vector<uint8_t>()`.

In `port/src/native_renderer/pm4_mirror.h` and `port/src/native_renderer/pm4_mirror.cpp`:
- Add `mutable std::array<uint32_t, 2> float_dirty_min_{0, 0};` and `mutable std::array<uint32_t, 2> float_dirty_max_{1024, 1024};` to `Pm4Mirror`.
- In `Pm4Mirror::Write(uint32_t index, uint32_t value)`: when `index - kAluConstantBase < 0x800u && regs_[index] != value`, update `float_dirty_min_[bank]` and `float_dirty_max_[bank]` for `offset = (index - kAluConstantBase) & 0x3FFu`.
- In `Pm4Mirror::WriteRange`: track `uint32_t first_change = length, last_change = 0;` inside the per-bank loop; when `destination[i] != value`, update `first_change` and `last_change = uint32_t(i + 1)`. After the loop, if `changes`, update `float_dirty_min_[bank]` and `float_dirty_max_[bank]` relative to `bank_offset = (first - kAluConstantBase) & 0x3FFu`.
- In `Pm4Mirror::FloatConstants(bool pixel)`: when `float_cache_version_[bank] != version`, sanitize only `[float_dirty_min_[bank], float_dirty_max_[bank])` (`((v & 0x7f800000u) == 0x7f800000u && (v & 0x007fffffu)) ? 0u : v`) from `regs_ + kAluConstantBase + bank * 1024` into `float_cache_[bank]`, then set `float_dirty_min_[bank] = 1024; float_dirty_max_[bank] = 0; float_cache_version_[bank] = version;`.
- In `Pm4Mirror::ScanFrom` (`case 0x2F`): replace the inner `thread_local CheckedGuestReads reads; reads.Reset(); captured = reads.Read(...)` with `captured = indirect_reads.ReadScratch(live_source, size * 4);`.

In `port/src/graphics/guest/pm4_capture.cpp`:
- Replace the per-call `std::vector<Dependency> sources;` with a `thread_local` reusable dependency scratch arena (`struct DependencyArena { std::vector<Dependency> slots; size_t used = 0; ... }`) that reuses inner `std::vector<uint8_t>` capacities across calls and trims capacities above `64 * 1024` or slot counts above `32` on reset, while keeping the rollback `batch.bytes.resize(initial_bytes); batch.ranges.resize(initial_ranges);` on failure.

- [ ] **Step 4: Build and run `sr_native_tests` to verify Task 1 passes**

Run: `cmake --build port/out/build/win-amd64-write-tracking --target sr_native_tests && .\port\out\build\win-amd64-write-tracking\tests\native\sr_native_tests.exe`
Expected: PASS (all tests pass including `checked_guest_reads_scratch_does_not_disturb_nested_slots` and `float_constants_incremental_dirty_range_sanitizes_nans_and_preserves_clean_words`).

- [ ] **Step 5: Commit Task 1**

```powershell
git add port/src/native_renderer/checked_guest_memory.h port/src/native_renderer/pm4_mirror.h port/src/native_renderer/pm4_mirror.cpp port/src/graphics/guest/pm4_capture.cpp tests/native/test_checked_guest_memory.cpp tests/native/test_pm4_mirror.cpp
git commit -m "perf(pm4): track dirty float constant ranges and reuse scratch reads"
```

---

### Task 2: Precompute `normal_spec_bits` in `VertexDeclaration` & Optimize `DecodeCapturedPacket` / `DeviceState`

**Files:**
- Modify: `port/src/graphics/guest/vertex_layout.h:10-21`
- Modify: `port/src/graphics/guest/vertex_layout.cpp:20-27`
- Modify: `port/src/graphics/guest/render_packet.cpp:123-165,246-356`
- Test: `tests/native/test_vertex_layout.cpp`
- Test: `tests/native/test_render_packets.cpp`

**Interfaces:**
- Consumes: `CapturedMemory::ReadUnambiguous(uint32_t address, uint32_t length) const`
- Produces:
  - `uint32_t VertexDeclaration::normal_spec_bits` in `port/src/graphics/guest/vertex_layout.h` (bit `1u << 6` when an attribute with `usage` in `{3, 6, 7}` has `type == 0x2a2187`, bit `1u` when `type == 0x2a2190 || type == 0x2a2390`)
  - `const VertexDeclaration& ReadVertexDeclarationInfo(uint8_t* base, uint32_t decl)` (used in Task 3)

- [ ] **Step 1: Write the failing test in `tests/native/test_vertex_layout.cpp` and `tests/native/test_render_packets.cpp`**

Add to `tests/native/test_vertex_layout.cpp`:
```cpp
SR_TEST(declaration_cache_precomputes_streams_used_and_normal_spec_bits) {
  VertexDeclarationCache cache;
  // Attribute 0: stream 0, offset 0, type 0x2a2187, usage 3 (NORMAL), index 0 -> bit 6 (64)
  // Attribute 1: stream 2, offset 4, type 0x2a2390, usage 6 (TANGENT), index 0 -> bit 0 (1)
  std::vector<uint8_t> bytes = {
      0, 0, 0, 0, 0, 0x2a, 0x21, 0x87, 0, 3, 0, 0,
      0, 2, 0, 4, 0, 0x2a, 0x23, 0x90, 0, 6, 0, 0,
  };
  const auto& decl = cache.Decode(bytes);
  SR_CHECK_EQ(decl.streams_used, (1u << 0) | (1u << 2));
  SR_CHECK_EQ(decl.normal_spec_bits, (1u << 6) | 1u);
}
```

Add to `tests/native/test_render_packets.cpp`:
```cpp
SR_TEST(decode_captured_packet_reads_contiguous_device_block_and_merges_mirror_bools_loops) {
  WorkBatch batch;
  WorkCmd cmd;
  cmd.op = Op::kDraw;
  cmd.u[0] = 4;
  cmd.u[2] = 3;
  cmd.device = 0x10000;
  // Populate a single contiguous capture covering the entire device struct [0x10000, 0x13000).
  batch.bytes.resize(0x3000, 0);
  auto put_be32 = [&](uint32_t addr, uint32_t val) {
    size_t off = addr - cmd.device;
    batch.bytes[off + 0] = uint8_t(val >> 24);
    batch.bytes[off + 1] = uint8_t(val >> 16);
    batch.bytes[off + 2] = uint8_t(val >> 8);
    batch.bytes[off + 3] = uint8_t(val);
  };
  const auto& layout = superman_returns::native::profile::kDevice;
  put_be32(cmd.device + layout.vs_bools, 0x11223344u);
  put_be32(cmd.device + layout.vs_loops, 0x55667788u);
  put_be32(cmd.device + layout.shader_a, 0xAABBCCDDu);
  put_be32(cmd.device + layout.shader_b, 0xEEFF0011u);
  batch.ranges.push_back({cmd.device, 0x3000, 0});
  cmd.range_first = 0;
  cmd.range_count = 1;

  superman_returns::native::Pm4Mirror mirror;
  // Override bool constant +1 via PM4 mirror while leaving +0 from device shadow.
  const uint8_t pm4[] = {0, 0, 0x49, 0x01, 0xDE, 0xAD, 0xBE, 0xEF};
  mirror.ScanCopy(nullptr, pm4, sizeof(pm4));

  RenderPacket packet;
  std::string error;
  SR_CHECK(ReplayCapturedRenderPacket(batch, cmd, mirror, packet, error));
  const auto* draw = GetDraw(packet);
  SR_CHECK(draw != nullptr);
  SR_CHECK_EQ(draw->shader_a, 0xAABBCCDDu);
  SR_CHECK_EQ(draw->shader_b, 0xEEFF0011u);
  uint32_t bool0 = 0, bool1 = 0, loop0 = 0;
  std::memcpy(&bool0, draw->constants.shared.data() + 256, 4);
  std::memcpy(&bool1, draw->constants.shared.data() + 260, 4);
  std::memcpy(&loop0, draw->constants.shared.data() + 304, 4);
  SR_CHECK_EQ(bool0, 0x11223344u);
  SR_CHECK_EQ(bool1, 0xDEADBEEFu);
  SR_CHECK_EQ(loop0, 0x55667788u);
}
```

- [ ] **Step 2: Run the test build to verify compilation fails (`normal_spec_bits` missing)**

Run: `cmake --build port/out/build/win-amd64-write-tracking --target sr_native_tests`
Expected: FAIL with `no member named 'normal_spec_bits' in 'superman_returns::graphics::guest::VertexDeclaration'`.

- [ ] **Step 3: Implement `normal_spec_bits` in `VertexDeclarationCache::Decode` and optimize `DeviceState` / `DecodeCapturedPacket` in `render_packet.cpp`**

In `port/src/graphics/guest/vertex_layout.h` & `vertex_layout.cpp`:
- Add `uint32_t normal_spec_bits = 0;` to `struct VertexDeclaration`.
- In `VertexDeclarationCache::Decode`: for each attribute, if `usage == 3 || usage == 6 || usage == 7`, set `if (type == 0x2a2187) entry.declaration.normal_spec_bits |= 1u << 6; if (type == 0x2a2190 || type == 0x2a2390) entry.declaration.normal_spec_bits |= 1u;`.

In `port/src/graphics/guest/render_packet.cpp`:
- In `DeviceState(DrawPacket& d, uint32_t device, const State& mirror)`:
  - Read the viewport using `d.memory.ReadUnambiguous(Address(device, layout.viewport), 24)` when available (falling back to `U32`).
  - Read `render_targets` (`16` bytes) via `d.memory.ReadUnambiguous(Address(device, layout.render_targets), 16)` when available.
- In `DecodeCapturedPacket`:
  - Move the unconditional `mirror.reg(kBoolConstantBase + i)` / `mirror.reg(kLoopConstantBase + i)` loops into the `else` branch of `if (cmd.device)` so `cmd.device != 0` writes bools (`256..288`) and loops (`304..432`) only once, using `memory.ReadUnambiguous` for `vs_bools` (`32` bytes), `vs_loops` (`64` bytes), and `ps_loops` (`64` bytes) when not written in `mirror`.
  - In the stream loop (`for (uint64_t i = cmd.stream_first; ...)`), cache `uint32_t cached_decl = 0; const std::vector<VertexAttribute>* cached_attrs = nullptr;` outside the loop so `Declaration(memory, decl)` is called only when `decl != cached_decl`.

- [ ] **Step 4: Build and run `sr_native_tests` to verify Task 2 passes**

Run: `cmake --build port/out/build/win-amd64-write-tracking --target sr_native_tests && .\port\out\build\win-amd64-write-tracking\tests\native\sr_native_tests.exe`
Expected: PASS.

- [ ] **Step 5: Commit Task 2**

```powershell
git add port/src/graphics/guest/vertex_layout.h port/src/graphics/guest/vertex_layout.cpp port/src/graphics/guest/render_packet.cpp tests/native/test_vertex_layout.cpp tests/native/test_render_packets.cpp
git commit -m "perf(replay): precompute declaration spec bits and deduplicate packet decode reads"
```

---

### Task 3: Front-End Capture Coalescing, Index Buffer Slot-16 Cache, and Worker Draw Preparation Optimization

**Files:**
- Modify: `port/src/native_renderer/native_renderer.h:235-285,820-845`
- Modify: `port/src/native_renderer/native_renderer.cpp:907-927,2150-2160,2550-2560,2655-2665,2725-2735,4405-4416,4640-4650,4980-4990,5365-5375,5659-5958,6530-6595,7330-7365,7710-7775,7885-7925,8171-8178,8316-8346,8835-8845`
- Test: `tests/native/test_render_packets.cpp`

**Interfaces:**
- Consumes: `VertexDeclaration::streams_used` and `VertexDeclaration::normal_spec_bits` from Task 2
- Produces:
  - Coalesced `Renderer::CaptureBytes(uint8_t* base, uint32_t address, uint32_t length)`
  - Index buffer caching in `front_stream_cache_[16]` inside `Renderer::DrawIndexedVertices`
  - Deduplicated `IASetVertexBuffers` via `bound_vertex_views_mask_` and single-lookup `BufferEntry* Renderer::ApplyBuffer(...)` in `Renderer::BindVertexStreams`

- [ ] **Step 1: Write a test in `tests/native/test_render_packets.cpp` verifying contiguous range coalescing semantics with `CapturedMemory`**

Add to `tests/native/test_render_packets.cpp`:
```cpp
SR_TEST(coalesced_capture_ranges_preserve_unambiguous_and_overlay_reads) {
  WorkBatch batch;
  WorkCmd cmd;
  cmd.range_first = 0;
  auto append_coalesced = [&](uint32_t range_first, uint32_t address, std::span<const uint8_t> data) {
    if (data.empty()) return;
    size_t offset = batch.bytes.size();
    batch.bytes.insert(batch.bytes.end(), data.begin(), data.end());
    if (batch.ranges.size() > range_first) {
      auto& last = batch.ranges.back();
      if (uint64_t(last.address) + last.length == address &&
          size_t(last.offset) + last.length == offset) {
        last.length += uint32_t(data.size());
        return;
      }
    }
    batch.ranges.push_back({address, uint32_t(data.size()), uint32_t(offset)});
  };
  const uint8_t part1[] = {1, 2, 3, 4};
  const uint8_t part2[] = {5, 6, 7, 8};
  append_coalesced(cmd.range_first, 0x2000, part1);
  append_coalesced(cmd.range_first, 0x2004, part2);
  cmd.range_count = uint32_t(batch.ranges.size()) - cmd.range_first;
  SR_CHECK_EQ(cmd.range_count, 1u);

  CapturedMemory mem;
  std::string error;
  SR_CHECK(CapturedMemory::Capture(batch, cmd, mem, error));
  auto span = mem.ReadUnambiguous(0x2000, 8);
  SR_CHECK_EQ(span.size(), 8u);
  SR_CHECK_EQ(span[0], 1u);
  SR_CHECK_EQ(span[7], 8u);
}
```

- [ ] **Step 2: Implement front-end and worker optimizations in `port/src/native_renderer/native_renderer.h` and `port/src/native_renderer/native_renderer.cpp`**

In `port/src/native_renderer/native_renderer.h`:
- Change `const BufferEntry* ApplyBuffer(...)` declaration to `BufferEntry* ApplyBuffer(...)`.
- Add `uint16_t bound_vertex_views_mask_ = 0;` alongside `fixed_state_cached_` in `Renderer`.

In `port/src/native_renderer/native_renderer.cpp`:
1. **Declaration helper (`ReadVertexDeclarationInfo`):**
   - Add `const graphics::guest::VertexDeclaration& ReadVertexDeclarationInfo(uint8_t* base, uint32_t decl)` at line 907 that returns the `VertexDeclaration` from `VertexDeclarationCache::Decode` (or populates a `thread_local graphics::guest::VertexDeclaration fallback` with `attributes`, `streams_used`, and `normal_spec_bits` on partial overlays), and make `ReadVertexDeclaration(base, decl)` return `ReadVertexDeclarationInfo(base, decl).attributes`.
2. **Coalesce contiguous ranges in `Renderer::CaptureBytes` (line 8171):**
   - After `batch_->bytes.insert(...)`, check if `batch_->ranges.size() > cur_.range_first`:
     ```cpp
     if (batch_->ranges.size() > cur_.range_first) {
       CaptureRange& last = batch_->ranges.back();
       if (uint64_t(last.address) + last.length == address &&
           size_t(last.offset) + last.length == offset) {
         last.length += length;
         return;
       }
     }
     ```
3. **Cache Index Buffer in `front_stream_cache_[16]` in `Renderer::DrawIndexedVertices` (line 8836):**
   - Compute `const uint32_t ib_format = (index32 ? 2u : 1u) | (endian << 2);`.
   - Probe `front_stream_cache_[16]` using `address_hash = address ^ (address >> 12) ^ (address >> 23)` and `set = (address_hash * 0x9E3779B1u) >> 26` (`first = set * 2`, matching the 64-set `std::array<FrontStreamCache, 128>` size! Note: also fix `PlanStreams` line 5701 if needed or keep set index `< 64`: `(address_hash * 0x9E3779B1u) >> 26` maps to `0..63` so `first = set * 2` is `0..126` within `std::array<FrontStreamCache, 128>`!).
   - Match `candidate.tracked && candidate.address == address && candidate.size == size && candidate.decl == reset_index && candidate.format == ib_format`.
   - On clean hit (`!RefreshTrackedBuffer(*sc.tracked)` and `!REXCVAR_GET(sr_native_debug_buffers_always_dirty)`), populate `cur_.index` directly (`key = sc.key`, `address`, `size`, `format = ib_format`, `reset_index`, `action = 0`) and set `ok = true`. Otherwise call `PlanBuffer` passing `cached_fields_match ? sc.tracked : nullptr, sc.key` and update `front_stream_cache_[16]`.
4. **Optimize `Renderer::PlanStreams` (line 5659):**
   - Replace the manual attribute loop with `const uint32_t streams_used = ReadVertexDeclarationInfo(base, decl).streams_used & 0xFFFFu;`.
   - Hoist `const bool allow_cache = !REXCVAR_GET(sr_native_debug_buffers_always_dirty);`, `const uint32_t dbg_vb_addr = uint32_t(REXCVAR_GET(sr_native_debug_vb_addr));`, and `const bool has_dbg_vs = !REXCVAR_GET(sr_native_debug_vs).empty();` outside the stream loop.
   - Iterate only set bits: `for (uint32_t mask = streams_used; mask; mask &= mask - 1) { const uint32_t s = uint32_t(std::countr_zero(mask)); ... }`.
   - Ensure the cache set index in `PlanStreams` is masked to `< 64` (`((address_hash * 0x9E3779B1u) >> 26) & 63u`) so `first = set * 2` never exceeds the 128-entry `front_stream_cache_[s]` array.
5. **Single declaration pass in `Renderer::UploadConstants` (lines 4410 & 4642):**
   - In `UploadConstants`, call `const auto& decl_info = ReadVertexDeclarationInfo(base, input_decl);` once at line 4411, populate `vertex_fetch_[...][3]`, and save `const uint32_t decl_normal_spec = decl_info.normal_spec_bits;`.
   - At line 4640, initialize `uint32_t spec = decl_normal_spec;` instead of calling `Load32(base, dev + kDev.vertex_decl)` and `ReadVertexDeclaration(base, decl)` a second time.
6. **Deduplicate `IASetVertexBuffers` and `BufferEntry` lookup in `Renderer::BindVertexStreams` (line 5824):**
   - Change `ApplyBuffer` return type to `BufferEntry*`.
   - In `BindVertexStreams`: only clear `vertex_fetch_[i][0..2]` when `dynamic_fetch` is true (since `UploadConstants` already clears `meta[3]` and non-dynamic draws leave `0..2` zeroed; or track `vertex_fetch_dirty_`).
   - Check `const uint16_t stream_bit = uint16_t(1u << sp.stream);` before calling `command_list_->IASetVertexBuffers`: if `!(bound_vertex_views_mask_ & stream_bit)` or `guest_vertex_views_[sp.stream].BufferLocation != view.BufferLocation || guest_vertex_views_[sp.stream].SizeInBytes != view.SizeInBytes || guest_vertex_views_[sp.stream].StrideInBytes != view.StrideInBytes`, issue `command_list_->IASetVertexBuffers(sp.stream, 1, &view)`, update `guest_vertex_views_[sp.stream] = view`, and set `bound_vertex_views_mask_ |= stream_bit`.
   - Reset `bound_vertex_views_mask_ = 0;` everywhere `fixed_state_cached_ = false;` is set (and set `bound_vertex_views_mask_ = 0xFFFFu;` right after `command_list_->IASetVertexBuffers(0, 16, guest_vertex_views_)` on debug resets).
   - At line 5902, replace `BufferEntry& entry = buffers_.at(sp.buffer.key);` with `BufferEntry& entry = *vb;`.

- [ ] **Step 3: Build `sr_native_tests` and the full `superman_returns` executable, then run `ctest`**

Run: `cmake --build port/out/build/win-amd64-write-tracking --target sr_native_tests superman_returns && ctest --test-dir port/out/build/win-amd64-write-tracking --output-on-failure`
Expected: 100% tests pass (`3/3` CTest suites, `487/487` native tests) and `superman_returns.exe` links cleanly.

- [ ] **Step 4: Commit Task 3**

```powershell
git add port/src/native_renderer/native_renderer.h port/src/native_renderer/native_renderer.cpp tests/native/test_render_packets.cpp
git commit -m "perf(renderer): coalesce captures, cache index plans, and deduplicate worker stream binds"
```
