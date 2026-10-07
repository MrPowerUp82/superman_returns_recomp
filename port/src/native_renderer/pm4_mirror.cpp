// Ported from crazyriddler/rexglue-native-kit @136bc6c4,
// reference/conan/port/src/native/pm4_mirror.cpp (Conan native renderer).
// The kit ships no license file at that revision; parts derived from Xenia /
// ReXGlue keep their BSD license. Changes for Superman Returns are listed in
// docs/native-port-plan.md section 2.
//
#include "pm4_mirror.h"
#include "../graphics/guest/constant_snapshot.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace superman_returns::native {

const std::array<uint32_t,1024>& Pm4Mirror::FloatConstants(bool pixel) const {
  const uint32_t bank=pixel?1:0;
  const uint64_t version=pixel?ps_version:vs_version;
  if(float_cache_version_[bank]!=version) {
    std::string error;
    graphics::guest::CaptureFloatConstants({regs_+kAluConstantBase+bank*1024,1024},false,float_cache_[bank],error);
    float_cache_version_[bank]=version;
  }
  return float_cache_[bank];
}

namespace {

inline uint32_t LoadBE(const uint8_t *base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

// 0xA0000000 is the 1:1 cached view of guest physical memory.
constexpr uint32_t kPhysicalView = 0xA0000000u;

// True when [p, p + bytes) is committed, readable host memory. The packets are
// scanned from guest command buffers that can already be recycled or hold a
// stale address; a read there must not take the process down.
bool HostReadable(const uint8_t *p, size_t bytes) {
#ifdef _WIN32
  const uint8_t *end = p + bytes;
  while (p < end) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !mbi.Protect) {
      return false;
    }
    p = static_cast<const uint8_t *>(mbi.BaseAddress) + mbi.RegionSize;
  }
#endif
  return true;
}

} // namespace

void Pm4Mirror::ScanFrom(const uint8_t *base, const uint8_t *stream,
                         uint32_t begin, uint32_t end, int depth,
                         const MemoryReader *reader) {
  uint32_t p = begin;
  while (p + 4 <= end) {
    uint32_t header = LoadBE(stream, p);
    p += 4;
    ++packets;
    switch (header >> 30) {
    case 0: {
      uint32_t count = ((header >> 16) & 0x3FFF) + 1;
      uint32_t index = header & 0x7FFF;
      bool one_register = (header >> 15) & 1;
      if (p + count * 4 > end)
        return;
      for (uint32_t i = 0; i < count; ++i) {
        Write(one_register ? index : index + i, LoadBE(stream, p + 4 * i));
      }
      p += count * 4;
      dwords += count;
      break;
    }
    case 1: {
      if (p + 8 > end)
        return;
      Write(header & 0x7FF, LoadBE(stream, p));
      Write((header >> 11) & 0x7FF, LoadBE(stream, p + 4));
      p += 8;
      break;
    }
    case 2:
      break;
    case 3: {
      uint32_t opcode = (header >> 8) & 0x7F;
      uint32_t count = ((header >> 16) & 0x3FFF) + 1;
      if (p + count * 4 > end)
        return;
      uint32_t d0 = count > 0 ? LoadBE(stream, p) : 0;
      switch (opcode) {
      case 0x2D: { // SET_CONSTANT
        uint32_t index = d0 & 0x7FF;
        uint32_t first;
        switch ((d0 >> 16) & 0xFF) {
        case 0:
          first = kAluConstantBase + index;
          break;
        case 1:
          first = kFetchConstantBase + index;
          break;
        case 2:
          first = kBoolConstantBase + index;
          break;
        case 3:
          first = kLoopConstantBase + index;
          break;
        case 4:
          first = 0x2000 + index;
          break;
        default:
          first = kRegisterCount;
          break;
        }
        for (uint32_t i = 1; i < count; ++i)
          Write(first + i - 1, LoadBE(stream, p + 4 * i));
        break;
      }
      case 0x55:   // SET_CONSTANT2
      case 0x56: { // SET_SHADER_CONSTANTS
        uint32_t index = d0 & 0xFFFF;
        for (uint32_t i = 1; i < count; ++i)
          Write(index + i - 1, LoadBE(stream, p + 4 * i));
        break;
      }
      case 0x2F: { // LOAD_ALU_CONSTANT (from memory)
        if (count < 3)
          break;
        uint32_t address = d0 & 0x3FFFFFFF;
        uint32_t offset_type = LoadBE(stream, p + 4);
        uint32_t size = LoadBE(stream, p + 8) & 0xFFF;
        uint32_t index = offset_type & 0x7FF;
        uint32_t first;
        switch ((offset_type >> 16) & 0xFF) {
        case 0:
          first = kAluConstantBase + index;
          break;
        case 1:
          first = kFetchConstantBase + index;
          break;
        case 2:
          first = kBoolConstantBase + index;
          break;
        case 3:
          first = kLoopConstantBase + index;
          break;
        case 4:
          first = 0x2000 + index;
          break;
        default:
          first = kRegisterCount;
          break;
        }
        uint32_t src = kPhysicalView + (address & 0x1FFFFFFF);
        std::span<const uint8_t> captured;
        if (reader)
          captured = (*reader)(src, size * 4);
        if (reader ? captured.size() < size * 4
                   : !HostReadable(base + src, size * 4)) {
          ++unreadable_alu_loads;
          break;
        }
        for (uint32_t i = 0; i < size; ++i)
          Write(first + i, reader ? LoadBE(captured.data(), 4 * i)
                                  : LoadBE(base, src + 4 * i));
        break;
      }
      case 0x22:                        // DRAW_INDX
      case 0x36: {                      // DRAW_INDX_2
        if ((regs_[0x2208] & 7) == 6) { // RB_MODECONTROL edram_mode = copy
          last_copy_dest_info = regs_[0x231B];
          last_copy_dest_base = regs_[0x2319];
          ++copy_draws;
        }
        break;
      }
      case 0x3F:   // INDIRECT_BUFFER
      case 0x37: { // INDIRECT_BUFFER_PFD
        ++indirect_buffers;
        if (count < 2 || depth > 2 || !follow_indirect)
          break;
        uint32_t address = kPhysicalView + (d0 & 0x1FFFFFFF);
        uint32_t size = LoadBE(stream, p + 4) & 0xFFFFF;
        if (size > (1u << 18))
          break; // not a plausible command buffer
        indirect_dwords += size;
        if (reader) {
          auto captured = (*reader)(address, size * 4);
          if (captured.size() >= size * 4)
            ScanFrom(nullptr, captured.data(), 0, size * 4, depth + 1, reader);
          else
            ++unreadable_indirect_buffers;
        } else {
          ScanFrom(base, base, address, address + size * 4, depth + 1);
        }
        break;
      }
      default:
        break;
      }
      p += count * 4;
      dwords += count;
      break;
    }
    }
  }
}

} // namespace superman_returns::native
