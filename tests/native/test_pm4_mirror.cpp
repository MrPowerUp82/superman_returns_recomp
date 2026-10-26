// PM4 mirror (port/src/native_renderer/pm4_mirror.{h,cpp}): register state
// rebuilt from synthetic XDK command segments.

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "pm4_mirror.h"
#include "test_main.h"
#ifdef _WIN32
#include "checked_guest_memory.h"
#endif

namespace {

using superman_returns::native::Pm4Mirror;

// Big-endian PM4 stream builder.
struct Stream {
  std::vector<uint8_t> bytes;
  Stream &Dword(uint32_t v) {
    bytes.push_back(uint8_t(v >> 24));
    bytes.push_back(uint8_t(v >> 16));
    bytes.push_back(uint8_t(v >> 8));
    bytes.push_back(uint8_t(v));
    return *this;
  }
  // Type-0: `values.size()` registers from `index` (or all into one register).
  Stream &Type0(uint32_t index, std::initializer_list<uint32_t> values,
                bool one_register = false) {
    Dword(((uint32_t(values.size()) - 1) << 16) |
          (one_register ? 0x8000u : 0u) | index);
    for (uint32_t v : values)
      Dword(v);
    return *this;
  }
  Stream &Type3(uint32_t opcode, std::initializer_list<uint32_t> payload,
                bool predicated = false) {
    Dword(0xC0000000u | ((uint32_t(payload.size()) - 1) << 16) |
          (opcode << 8) | uint32_t(predicated));
    for (uint32_t v : payload)
      Dword(v);
    return *this;
  }
  uint32_t size() const { return uint32_t(bytes.size()); }
};

// Pm4Mirror::ScanCopy reads the segment from `words` and memory references
// (LOAD_ALU_CONSTANT, INDIRECT_BUFFER) at base + 0xA0000000 + physical.
struct GuestMemory {
  std::vector<uint8_t> page = std::vector<uint8_t>(0x1000);
  const uint8_t *base() const {
    // The mirror adds the 0xA0000000 physical view to `base`; point `base`
    // that far before this buffer (address arithmetic only, as the renderer
    // does with the 4 GB guest mapping).
    return reinterpret_cast<const uint8_t *>(
        reinterpret_cast<uintptr_t>(page.data()) - 0xA0000000u);
  }
  void Store(uint32_t offset, uint32_t v) {
    page[offset] = uint8_t(v >> 24);
    page[offset + 1] = uint8_t(v >> 16);
    page[offset + 2] = uint8_t(v >> 8);
    page[offset + 3] = uint8_t(v);
  }
};

} // namespace

SR_TEST(pm4_references_use_captured_reader_without_live_memory) {
  Stream stream;
  stream.Type3(0x2f, {0x1000, 0, 1});
  std::vector<uint8_t> captured{0x3f, 0x80, 0, 0};
  Pm4Mirror mirror;
  uint32_t reads = 0;
  mirror.ScanCopyUsing(
      stream.bytes.data(), stream.size(),
      [&](uint32_t address, uint32_t size) -> std::span<const uint8_t> {
        SR_CHECK_EQ(address, 0xa0001000);
        SR_CHECK_EQ(size, 4);
        ++reads;
        return captured;
      });
  SR_CHECK_EQ(reads, 1);
  SR_CHECK_EQ(mirror.reg(Pm4Mirror::kAluConstantBase), 0x3f800000);
}
SR_TEST(pm4_missing_capture_is_reported_without_dereferencing_guest) {
  Stream stream;
  stream.Type3(0x2f, {0x1000, 0, 1});
  Pm4Mirror mirror;
  mirror.ScanCopyUsing(
      stream.bytes.data(), stream.size(),
      [](uint32_t, uint32_t) -> std::span<const uint8_t> { return {}; });
  SR_CHECK_EQ(mirror.unreadable_alu_loads, 1);
  SR_CHECK_EQ(mirror.reg(Pm4Mirror::kAluConstantBase), 0);
}

SR_TEST(type0_writes_consecutive_registers) {
  Pm4Mirror m;
  Stream s;
  s.Type0(0x2200, {0x11, 0x22, 0x33});
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.reg(0x2200), 0x11u);
  SR_CHECK_EQ(m.reg(0x2201), 0x22u);
  SR_CHECK_EQ(m.reg(0x2202), 0x33u);
  SR_CHECK_EQ(m.packets, 1u);
}

SR_TEST(type0_one_register_mode_keeps_the_last_value) {
  Pm4Mirror m;
  Stream s;
  s.Type0(0x2104, {1, 2, 3}, true);
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.reg(0x2104), 3u);
  SR_CHECK_EQ(m.reg(0x2105), 0u);
}

SR_TEST(type1_writes_two_registers) {
  Pm4Mirror m;
  Stream s;
  s.Dword(0x40000000u | (0x105u << 11) | 0x104u).Dword(0xAAAA).Dword(0xBBBB);
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.reg(0x104), 0xAAAAu);
  SR_CHECK_EQ(m.reg(0x105), 0xBBBBu);
}

SR_TEST(padding_and_invalid_pm4_markers_do_not_consume_payloads) {
  Pm4Mirror m;
  Stream s;
  s.Dword(0).Dword(0x0BADF00Du);
  s.Type0(0x2100, {0x66});
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.reg(0), 0u);
  SR_CHECK_EQ(m.reg(0x2100), 0x66u);
  SR_CHECK_EQ(m.packets, 3u);
}

SR_TEST(scan_does_not_wrap_when_less_than_one_dword_remains) {
  Pm4Mirror m;
  m.Scan(nullptr, 0xFFFFFFFCu, 0xFFFFFFFFu);
  SR_CHECK_EQ(m.packets, 0u);
}

SR_TEST(reg_rmw_combines_immediate_and_register_operands) {
  Pm4Mirror m;
  Stream s;
  s.Type0(0x104, {0xAA});
  s.Type0(0x105, {0x0F, 0xF0});
  s.Type3(0x21, {0x104, 0x3F, 0x40}); // (0xAA & 0x3F) | 0x40
  s.Type3(0x21, {0xC0000104u, 0x105, 0x106}); // both operands from registers
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.reg(0x104), 0xFAu);
  SR_CHECK_EQ(m.reg(0x105), 0x0Fu);
}

SR_TEST(predicated_type3_writes_respect_bin_mask_and_select) {
  Pm4Mirror m;
  Stream s;
  s.Type3(0x55, {0x2208, 1}, true);  // default masks allow execution
  s.Type3(0x50, {0, 1});             // mask low bit only
  s.Type3(0x51, {0, 2});             // select a different bit
  s.Type3(0x55, {0x2208, 6}, true);  // blocked
  s.Type3(0x55, {0x2209, 7});        // unpredicated always executes
  s.Type3(0x62, {1});                // select low bit
  s.Type3(0x55, {0x2208, 6}, true);  // allowed again
  s.Type3(0x60, {0});                // mask no bits
  s.Type3(0x21, {0x104, 0, 99}, true); // blocked RMW
  s.Type3(0x61, {0xFFFFFFFF});      // high half only doesn't match
  s.Type3(0x63, {0xFFFFFFFF});      // now high halves match
  s.Type3(0x55, {0x220A, 10}, true);
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.reg(0x2208), 6u);
  SR_CHECK_EQ(m.reg(0x2209), 7u);
  SR_CHECK_EQ(m.reg(0x220A), 10u);
  SR_CHECK_EQ(m.reg(0x104), 0u);
}

SR_TEST(predicated_indirect_buffer_skips_reader_and_preserves_counts) {
  Stream primary;
  primary.Type3(0x50, {0, 0});
  primary.Type3(0x3F, {0x200, 2}, true);
  Pm4Mirror m;
  unsigned callbacks = 0;
  m.ScanCopyUsing(primary.bytes.data(), primary.size(),
      [&](uint32_t, uint32_t) -> std::span<const uint8_t> {
        ++callbacks;
        return {};
      });
  SR_CHECK_EQ(callbacks, 0u);
  SR_CHECK_EQ(m.indirect_buffers, 0u);
  SR_CHECK_EQ(m.unreadable_indirect_buffers, 0u);
  SR_CHECK_EQ(m.packets, 2u);
}

SR_TEST(set_constant_targets_each_register_file) {
  Pm4Mirror m;
  Stream s;
  s.Type3(0x2D, {(0u << 16) | 8, 0x3F800000}); // ALU constant dword 8
  s.Type3(0x2D, {(1u << 16) | 6, 0x1234});     // fetch constant dword 6
  s.Type3(0x2D, {(2u << 16) | 1, 0x5});        // bool
  s.Type3(0x2D, {(3u << 16) | 2, 0x7});        // loop
  s.Type3(0x2D, {(4u << 16) | 0x208, 0x6});    // register 0x2208
  const uint64_t vs_version = m.vs_version;
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.reg(Pm4Mirror::kAluConstantBase + 8), 0x3F800000u);
  SR_CHECK_EQ(m.reg(Pm4Mirror::kFetchConstantBase + 6), 0x1234u);
  SR_CHECK_EQ(m.reg(Pm4Mirror::kBoolConstantBase + 1), 0x5u);
  SR_CHECK_EQ(m.reg(Pm4Mirror::kLoopConstantBase + 2), 0x7u);
  SR_CHECK_EQ(m.reg(0x2208), 0x6u);
  SR_CHECK(m.vs_version > vs_version);
  SR_CHECK(m.written(Pm4Mirror::kAluConstantBase + 8));
  SR_CHECK(!m.written(Pm4Mirror::kAluConstantBase + 9));
}

SR_TEST(pixel_constant_writes_bump_only_the_ps_version) {
  Pm4Mirror m;
  Stream s;
  s.Type0(Pm4Mirror::kAluConstantBase + 0x400, {1, 2, 3, 4}); // PS c0
  const uint64_t vs = m.vs_version, ps = m.ps_version;
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.vs_version, vs);
  SR_CHECK(m.ps_version > ps);
}
SR_TEST(rewriting_identical_constants_preserves_versions_but_tracks_zero_writes) {
  Pm4Mirror m;
  Stream vs, ps;
  vs.Type0(Pm4Mirror::kAluConstantBase, {0, 0x3f800000});
  ps.Type0(Pm4Mirror::kAluConstantBase + 0x400, {0, 0x40000000});
  m.ScanCopy(nullptr, vs.bytes.data(), vs.size());
  m.ScanCopy(nullptr, ps.bytes.data(), ps.size());
  const auto vv=m.vs_version, pv=m.ps_version;
  m.ScanCopy(nullptr, vs.bytes.data(), vs.size());
  m.ScanCopy(nullptr, ps.bytes.data(), ps.size());
  SR_CHECK_EQ(m.vs_version,vv);
  SR_CHECK_EQ(m.ps_version,pv);
  SR_CHECK(m.written(Pm4Mirror::kAluConstantBase));
  SR_CHECK(m.written(Pm4Mirror::kAluConstantBase + 0x400));
  Stream change;
  change.Type0(Pm4Mirror::kAluConstantBase, {7});
  m.ScanCopy(nullptr, change.bytes.data(), change.size());
  SR_CHECK(m.vs_version>vv);
  SR_CHECK_EQ(m.ps_version,pv);
}
SR_TEST(cached_float_constants_refresh_on_change_and_preserve_raw_mirror) {
  Pm4Mirror m;
  Stream values;
  values.Type0(Pm4Mirror::kAluConstantBase, {0x7fc01234,0x7f800000,0x80000000,0x3f800000});
  m.ScanCopy(nullptr,values.bytes.data(),values.size());
  const auto& first=m.FloatConstants(false);
  SR_CHECK_EQ(first[0],0u);
  SR_CHECK_EQ(first[1],0x7f800000u);
  SR_CHECK_EQ(first[2],0x80000000u);
  SR_CHECK_EQ(m.reg(Pm4Mirror::kAluConstantBase),0x7fc01234u);
  auto retained=first;
  Stream change;
  change.Type0(Pm4Mirror::kAluConstantBase+3,{0x40000000});
  m.ScanCopy(nullptr,change.bytes.data(),change.size());
  SR_CHECK_EQ(m.FloatConstants(false)[3],0x40000000u);
  SR_CHECK_EQ(retained[3],0x3f800000u);
  SR_CHECK_EQ(m.FloatConstants(true)[3],0u);
  m=Pm4Mirror{};
  SR_CHECK_EQ(m.FloatConstants(false)[3],0u);
}

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

SR_TEST(load_alu_constant_reads_guest_memory) {
  GuestMemory mem;
  mem.Store(0x100, 0xDEADBEEF);
  mem.Store(0x104, 0xCAFEF00D);
  Pm4Mirror m;
  Stream s;
  // address (physical 0x100), {type 0 = ALU, index 16}, size 2 dwords.
  s.Type3(0x2F, {0x100, (0u << 16) | 16, 2});
  m.ScanCopy(mem.base(), s.bytes.data(), s.size());
  SR_CHECK_EQ(m.reg(Pm4Mirror::kAluConstantBase + 16), 0xDEADBEEFu);
  SR_CHECK_EQ(m.reg(Pm4Mirror::kAluConstantBase + 17), 0xCAFEF00Du);
}

SR_TEST(pm4_alu_read_profile_counts_live_validation_without_changing_constants) {
  GuestMemory mem;
  mem.Store(0x100, 0xDEADBEEF);mem.Store(0x104, 0xCAFEF00D);
  Stream s;s.Type3(0x2F, {0x100, 16, 2});
  Pm4Mirror m;m.alu_read_profile.enabled=true;
  m.ScanCopy(mem.base(),s.bytes.data(),s.size());
  const auto version=m.vs_version;
  m.ScanCopy(mem.base(),s.bytes.data(),s.size());
  SR_CHECK_EQ(m.reg(Pm4Mirror::kAluConstantBase+16),0xDEADBEEFu);
  SR_CHECK_EQ(m.reg(Pm4Mirror::kAluConstantBase+17),0xCAFEF00Du);
  SR_CHECK_EQ(m.vs_version,version);
  SR_CHECK_EQ(m.alu_read_profile.calls,2u);
  SR_CHECK_EQ(m.alu_read_profile.bytes,16u);
  SR_CHECK_EQ(m.alu_read_profile.failures,0u);
#ifdef _WIN32
  SR_CHECK(m.alu_read_profile.queries>=2u);
#endif
}

SR_TEST(pm4_alu_read_profile_stays_idle_when_disabled_or_using_owned_reader) {
  GuestMemory mem;mem.Store(0x100,0x12345678);
  Stream s;s.Type3(0x2F,{0x100,16,1});
  Pm4Mirror m;
  SR_CHECK(!m.alu_read_profile.enabled);
  m.ScanCopy(mem.base(),s.bytes.data(),s.size());
  SR_CHECK_EQ(m.alu_read_profile.calls,0u);
  SR_CHECK_EQ(m.alu_read_profile.validation_ns,0u);
  m.alu_read_profile.enabled=true;
  m.ScanCopyUsing(s.bytes.data(),s.size(),[&](uint32_t address,uint32_t length) {
    SR_CHECK_EQ(address,0xA0000100u);
    return std::span<const uint8_t>(mem.page.data()+0x100,length);
  });
  SR_CHECK_EQ(m.reg(Pm4Mirror::kAluConstantBase+16),0x12345678u);
  SR_CHECK_EQ(m.alu_read_profile.calls,0u);
  SR_CHECK_EQ(m.alu_read_profile.queries,0u);
  SR_CHECK_EQ(m.alu_read_profile.validation_ns,0u);
}

#ifdef _WIN32
SR_TEST(pm4_bounded_alu_reads_refresh_live_bytes_and_match_query_state) {
  GuestMemory mem;mem.Store(0x100,0x3f800000);mem.Store(0x104,0x7fc01234);
  Stream load;load.Type3(0x2F,{0x100,16,2});
  Pm4Mirror reference,m;
  SR_CHECK(!m.bounded_alu_reads);
  m.bounded_alu_reads=true;m.alu_read_profile.enabled=true;
  for(unsigned round=0;round<2;++round) {
    mem.Store(0x100,round?0x40000000:0x3f800000);
    reference.ScanCopy(mem.base(),load.bytes.data(),load.size());
    m.ScanCopy(mem.base(),load.bytes.data(),load.size());
    for(uint32_t i=0;i<Pm4Mirror::kRegisterCount;++i)SR_CHECK_EQ(m.reg(i),reference.reg(i));
    SR_CHECK_EQ(m.vs_version,reference.vs_version);
    SR_CHECK_EQ(m.ps_version,reference.ps_version);
  }
  SR_CHECK_EQ(m.reg(Pm4Mirror::kAluConstantBase+16),0x40000000u);
  SR_CHECK_EQ(m.alu_read_profile.calls,2u);
  SR_CHECK_EQ(m.alu_read_profile.bounded_calls,2u);
  SR_CHECK_EQ(m.alu_read_profile.queries,0u);
  SR_CHECK_EQ(m.alu_read_profile.bytes,16u);
  SR_CHECK_EQ(m.alu_read_profile.failures,0u);
  // A zero-length load changes no register and does not read its source.
  const auto version=m.vs_version;
  Stream zero;zero.Type3(0x2F,{0x100,16,0});
  m.ScanCopy(mem.base(),zero.bytes.data(),zero.size());
  SR_CHECK_EQ(m.vs_version,version);
  SR_CHECK_EQ(m.unreadable_alu_loads,0u);
  const auto calls=m.alu_read_profile.calls;
  m.ScanCopyUsing(load.bytes.data(),load.size(),[&](uint32_t,uint32_t length) {
    return std::span<const uint8_t>(mem.page.data()+0x100,length);
  });
  SR_CHECK_EQ(m.alu_read_profile.calls,calls);
}

SR_TEST(pm4_bounded_alu_read_rejects_partial_source_without_partial_register_write) {
  SYSTEM_INFO system;GetSystemInfo(&system);
  const size_t page_size=system.dwPageSize;
  auto* pages=static_cast<uint8_t*>(VirtualAlloc(nullptr,2*page_size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
  SR_CHECK(pages!=nullptr);if(!pages)return;
  DWORD old=0;
  const bool protected_ok=VirtualProtect(pages+page_size,page_size,PAGE_NOACCESS,&old)!=0;
  SR_CHECK(protected_ok);
  if(protected_ok) {
    auto* base=reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(pages)-0xA0000000u);
    Pm4Mirror m;m.bounded_alu_reads=true;m.alu_read_profile.enabled=true;
    Stream seed;seed.Type0(Pm4Mirror::kAluConstantBase+16,{11,22});
    m.ScanCopy(nullptr,seed.bytes.data(),seed.size());
    const auto version=m.vs_version;
    Stream load;load.Type3(0x2F,{uint32_t(page_size-4),16,2});
    m.ScanCopy(base,load.bytes.data(),load.size());
    SR_CHECK_EQ(m.unreadable_alu_loads,1u);
    SR_CHECK_EQ(m.reg(Pm4Mirror::kAluConstantBase+16),11u);
    SR_CHECK_EQ(m.reg(Pm4Mirror::kAluConstantBase+17),22u);
    SR_CHECK_EQ(m.vs_version,version);
    SR_CHECK_EQ(m.alu_read_profile.calls,1u);
    SR_CHECK_EQ(m.alu_read_profile.bounded_calls,1u);
    SR_CHECK_EQ(m.alu_read_profile.queries,0u);
    SR_CHECK_EQ(m.alu_read_profile.failures,1u);
  }
  SR_CHECK(VirtualFree(pages,0,MEM_RELEASE));
}

SR_TEST(pm4_alu_read_profile_counts_failed_region_and_preserves_registers) {
  SYSTEM_INFO system;GetSystemInfo(&system);
  const size_t page_size=system.dwPageSize;
  auto* pages=static_cast<uint8_t*>(VirtualAlloc(nullptr,2*page_size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
  SR_CHECK(pages!=nullptr);if(!pages)return;
  DWORD old=0;
  const bool protected_ok=VirtualProtect(pages+page_size,page_size,PAGE_NOACCESS,&old)!=0;
  SR_CHECK(protected_ok);
  if(protected_ok) {
    auto* base=reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(pages)-0xA0000000u);
    Pm4Mirror m;m.alu_read_profile.enabled=true;
    Stream seed;seed.Type0(Pm4Mirror::kAluConstantBase+16,{11,22});
    m.ScanCopy(nullptr,seed.bytes.data(),seed.size());
    const auto version=m.vs_version;
    Stream load;load.Type3(0x2F,{uint32_t(page_size-4),16,2});
    m.ScanCopy(base,load.bytes.data(),load.size());
    SR_CHECK_EQ(m.unreadable_alu_loads,1u);
    SR_CHECK_EQ(m.reg(Pm4Mirror::kAluConstantBase+16),11u);
    SR_CHECK_EQ(m.reg(Pm4Mirror::kAluConstantBase+17),22u);
    SR_CHECK_EQ(m.vs_version,version);
    SR_CHECK_EQ(m.alu_read_profile.calls,1u);
    SR_CHECK_EQ(m.alu_read_profile.bytes,8u);
    SR_CHECK_EQ(m.alu_read_profile.queries,2u);
    SR_CHECK_EQ(m.alu_read_profile.failures,1u);
  }
  SR_CHECK(VirtualFree(pages,0,MEM_RELEASE));
}
#endif

SR_TEST(indirect_buffer_is_followed) {
  GuestMemory mem;
  // Indirect buffer at physical 0x200: one type-0 write of 0x2100 = 0x77.
  mem.Store(0x200, 0x2100);
  mem.Store(0x204, 0x77);
  Pm4Mirror m;
  Stream s;
  s.Type3(0x3F, {0x200, 2});
  m.ScanCopy(mem.base(), s.bytes.data(), s.size());
  SR_CHECK_EQ(m.indirect_buffers, 1u);
  SR_CHECK_EQ(m.reg(0x2100), 0x77u);
  m.follow_indirect = false;
  mem.Store(0x204, 0x88);
  m.ScanCopy(mem.base(), s.bytes.data(), s.size());
  SR_CHECK_EQ(m.reg(0x2100), 0x77u);
}

SR_TEST(nested_live_indirect_buffers_preserve_outer_stream) {
  GuestMemory mem;
  // First buffer contains a register write, a nested dispatch and a final
  // write. The nested load must not overwrite the captured outer buffer.
  mem.Store(0x200, 0x2100);
  mem.Store(0x204, 0x11);
  mem.Store(0x208, 0xC0013F00u);
  mem.Store(0x20C, 0x300);
  mem.Store(0x210, 2);
  mem.Store(0x214, 0x2102);
  mem.Store(0x218, 0x33);
  mem.Store(0x300, 0x2101);
  mem.Store(0x304, 0x22);
  Stream root;
  root.Type3(0x3F, {0x200, 7});
  Pm4Mirror m;
  m.ScanCopy(mem.base(), root.bytes.data(), root.size());
  SR_CHECK_EQ(m.reg(0x2100), 0x11u);
  SR_CHECK_EQ(m.reg(0x2101), 0x22u);
  SR_CHECK_EQ(m.reg(0x2102), 0x33u);
  SR_CHECK_EQ(m.unreadable_indirect_buffers, 0u);
  SR_CHECK_EQ(m.indirect_buffers, 2u);
}

#ifdef _WIN32
SR_TEST(live_indirect_rejects_unreadable_tail_without_partial_writes) {
  SYSTEM_INFO system;
  GetSystemInfo(&system);
  const size_t page_size = system.dwPageSize;
  auto* pages = static_cast<uint8_t*>(VirtualAlloc(
      nullptr, 2 * page_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  SR_CHECK(pages != nullptr);
  if (!pages) return;
  DWORD old = 0;
  const bool protected_ok = VirtualProtect(
      pages + page_size, page_size, PAGE_NOACCESS, &old) != 0;
  SR_CHECK(protected_ok);
  if (protected_ok) {
    // A complete register write is present in the readable page, but the
    // indirect command requests one more dword from the inaccessible page.
    pages[page_size - 8] = 0;
    pages[page_size - 7] = 0;
    pages[page_size - 6] = 0x21;
    pages[page_size - 5] = 0;
    pages[page_size - 4] = 0;
    pages[page_size - 3] = 0;
    pages[page_size - 2] = 0;
    pages[page_size - 1] = 0x66;
    const auto* base = reinterpret_cast<const uint8_t*>(
        reinterpret_cast<uintptr_t>(pages) - 0xA0000000u);
    Stream s;
    s.Type3(0x3F, {uint32_t(page_size - 8), 3});
    Pm4Mirror m;
    m.ScanCopy(base, s.bytes.data(), s.size());
    SR_CHECK_EQ(m.unreadable_indirect_buffers, 1u);
    SR_CHECK_EQ(m.reg(0x2100), 0u);
  }
  SR_CHECK(VirtualFree(pages, 0, MEM_RELEASE));
}
#endif

SR_TEST(copy_mode_draw_records_resolve_destination) {
  Pm4Mirror m;
  Stream s;
  s.Type0(0x2208, {6});                    // RB_MODECONTROL: copy
  s.Type0(0x2319, {0x1000});               // RB_COPY_DEST_BASE
  s.Type0(0x231B, {0x302});                // RB_COPY_DEST_INFO
  s.Type3(0x22, {0, 0x3F});                // DRAW_INDX
  s.Type0(0x2319, {0}).Type0(0x231B, {0}); // the XDK resets them afterwards
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.copy_draws, 1u);
  SR_CHECK_EQ(m.last_copy_dest_base, 0x1000u);
  SR_CHECK_EQ(m.last_copy_dest_info, 0x302u);
}

SR_TEST(truncated_packet_stops_the_scan) {
  Pm4Mirror m;
  Stream s;
  s.Type0(0x2000, {5});
  s.Dword((3u << 16) | 0x2001).Dword(1); // claims 4 values, has 1
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.reg(0x2000), 5u);
  SR_CHECK_EQ(m.reg(0x2001), 0u);
}

SR_TEST(out_of_range_register_is_ignored) {
  Pm4Mirror m;
  Stream s;
  s.Type3(0x2D, {(9u << 16) | 1, 0x99}); // unknown constant type
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.reg(Pm4Mirror::kRegisterCount), 0u);
  SR_CHECK_EQ(m.packets, 1u);
}

SR_TEST(captured_indirect_load_survives_guest_reuse) {
  Stream primary, indirect, constants;
  primary.Type3(0x3F, {0x200, 4});
  indirect.Type3(0x2F, {0x100, 0, 1});
  constants.Dword(0x3f800000);
  Pm4Mirror m;
  m.ScanCopyUsing(
      primary.bytes.data(), primary.size(),
      [&](uint32_t address, uint32_t size) -> std::span<const uint8_t> {
        if (address == 0xa0000200 && size == indirect.size())
          return indirect.bytes;
        if (address == 0xa0000100 && size == constants.size())
          return constants.bytes;
        return {};
      });
  indirect.bytes.clear();
  constants.bytes.clear();
  SR_CHECK_EQ(m.reg(Pm4Mirror::kAluConstantBase), 0x3f800000u);
  SR_CHECK_EQ(m.unreadable_indirect_buffers, 0u);
}

SR_TEST(missing_captured_indirect_is_reported) {
  Stream primary;
  primary.Type3(0x3F, {0x200, 2});
  Pm4Mirror m;
  m.ScanCopyUsing(
      primary.bytes.data(), primary.size(),
      [](uint32_t, uint32_t) -> std::span<const uint8_t> { return {}; });
  SR_CHECK_EQ(m.unreadable_indirect_buffers, 1u);
}
