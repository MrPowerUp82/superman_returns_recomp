// PM4 mirror (port/src/native_renderer/pm4_mirror.{h,cpp}): register state
// rebuilt from synthetic XDK command segments.

#include <cstdint>
#include <cstring>
#include <vector>

#include "pm4_mirror.h"
#include "test_main.h"

namespace {

using superman_returns::native::Pm4Mirror;

// Big-endian PM4 stream builder.
struct Stream {
  std::vector<uint8_t> bytes;
  Stream& Dword(uint32_t v) {
    bytes.push_back(uint8_t(v >> 24));
    bytes.push_back(uint8_t(v >> 16));
    bytes.push_back(uint8_t(v >> 8));
    bytes.push_back(uint8_t(v));
    return *this;
  }
  // Type-0: `values.size()` registers from `index` (or all into one register).
  Stream& Type0(uint32_t index, std::initializer_list<uint32_t> values, bool one_register = false) {
    Dword(((uint32_t(values.size()) - 1) << 16) | (one_register ? 0x8000u : 0u) | index);
    for (uint32_t v : values) Dword(v);
    return *this;
  }
  Stream& Type3(uint32_t opcode, std::initializer_list<uint32_t> payload) {
    Dword(0xC0000000u | ((uint32_t(payload.size()) - 1) << 16) | (opcode << 8));
    for (uint32_t v : payload) Dword(v);
    return *this;
  }
  uint32_t size() const { return uint32_t(bytes.size()); }
};

// Pm4Mirror::ScanCopy reads the segment from `words` and memory references
// (LOAD_ALU_CONSTANT, INDIRECT_BUFFER) at base + 0xA0000000 + physical.
struct GuestMemory {
  std::vector<uint8_t> page = std::vector<uint8_t>(0x1000);
  const uint8_t* base() const {
    // The mirror adds the 0xA0000000 physical view to `base`; point `base`
    // that far before this buffer (address arithmetic only, as the renderer
    // does with the 4 GB guest mapping).
    return reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(page.data()) -
                                            0xA0000000u);
  }
  void Store(uint32_t offset, uint32_t v) {
    page[offset] = uint8_t(v >> 24);
    page[offset + 1] = uint8_t(v >> 16);
    page[offset + 2] = uint8_t(v >> 8);
    page[offset + 3] = uint8_t(v);
  }
};

}  // namespace

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

SR_TEST(set_constant_targets_each_register_file) {
  Pm4Mirror m;
  Stream s;
  s.Type3(0x2D, {(0u << 16) | 8, 0x3F800000});   // ALU constant dword 8
  s.Type3(0x2D, {(1u << 16) | 6, 0x1234});       // fetch constant dword 6
  s.Type3(0x2D, {(2u << 16) | 1, 0x5});          // bool
  s.Type3(0x2D, {(3u << 16) | 2, 0x7});          // loop
  s.Type3(0x2D, {(4u << 16) | 0x208, 0x6});      // register 0x2208
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
  s.Type0(Pm4Mirror::kAluConstantBase + 0x400, {1, 2, 3, 4});  // PS c0
  const uint64_t vs = m.vs_version, ps = m.ps_version;
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.vs_version, vs);
  SR_CHECK(m.ps_version > ps);
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

SR_TEST(copy_mode_draw_records_resolve_destination) {
  Pm4Mirror m;
  Stream s;
  s.Type0(0x2208, {6});                   // RB_MODECONTROL: copy
  s.Type0(0x2319, {0x1000});              // RB_COPY_DEST_BASE
  s.Type0(0x231B, {0x302});               // RB_COPY_DEST_INFO
  s.Type3(0x22, {0, 0x3F});               // DRAW_INDX
  s.Type0(0x2319, {0}).Type0(0x231B, {0});  // the XDK resets them afterwards
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.copy_draws, 1u);
  SR_CHECK_EQ(m.last_copy_dest_base, 0x1000u);
  SR_CHECK_EQ(m.last_copy_dest_info, 0x302u);
}

SR_TEST(truncated_packet_stops_the_scan) {
  Pm4Mirror m;
  Stream s;
  s.Type0(0x2000, {5});
  s.Dword((3u << 16) | 0x2001).Dword(1);  // claims 4 values, has 1
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.reg(0x2000), 5u);
  SR_CHECK_EQ(m.reg(0x2001), 0u);
}

SR_TEST(out_of_range_register_is_ignored) {
  Pm4Mirror m;
  Stream s;
  s.Type3(0x2D, {(9u << 16) | 1, 0x99});  // unknown constant type
  m.ScanCopy(nullptr, s.bytes.data(), s.size());
  SR_CHECK_EQ(m.reg(Pm4Mirror::kRegisterCount), 0u);
  SR_CHECK_EQ(m.packets, 1u);
}
