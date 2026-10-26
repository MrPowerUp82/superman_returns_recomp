// Ported from crazyriddler/rexglue-native-kit @136bc6c4,
// reference/conan/port/src/native/pm4_mirror.cpp (Conan native renderer).
// The kit ships no license file at that revision; parts derived from Xenia /
// ReXGlue keep their BSD license. Changes for Superman Returns are listed in
// docs/native-port-plan.md section 2.
//
#include "pm4_mirror.h"
#include "../graphics/guest/constant_snapshot.h"
#include <algorithm>
#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "checked_guest_memory.h"
#endif

namespace superman_returns::native {

const std::array<uint32_t,1024>& Pm4Mirror::FloatConstants(bool pixel) const {
  const uint32_t bank=pixel?1:0;
  const uint64_t version=pixel?ps_version:vs_version;
  if(float_cache_version_[bank]!=version) {
    const auto* src = regs_ + kAluConstantBase + bank * 1024;
    auto& dst = float_cache_[bank];
    const uint32_t start = std::min(float_dirty_min_[bank], 1024u);
    const uint32_t end = std::min(float_dirty_max_[bank], 1024u);
    for(uint32_t i = start; i < end; ++i) {
      const uint32_t v = src[i];
      dst[i] = ((v & 0x7f800000u) == 0x7f800000u && (v & 0x007fffffu)) ? 0u : v;
    }
    float_dirty_min_[bank] = 1024;
    float_dirty_max_[bank] = 0;
    float_cache_version_[bank] = version;
  }
  return float_cache_[bank];
}
const std::shared_ptr<const std::array<uint32_t,1024>>& Pm4Mirror::ShareFloatConstants(bool pixel) const {
  const uint32_t bank=pixel?1:0;
  const uint64_t version=pixel?ps_version:vs_version;
  if(!float_snapshots_[bank] || float_snapshot_version_[bank]!=version) {
    float_snapshots_[bank]=std::make_shared<const std::array<uint32_t,1024>>(FloatConstants(pixel));
    float_snapshot_version_[bank]=version;
  }
  return float_snapshots_[bank];
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
bool HostReadable(const uint8_t *p, size_t bytes, Pm4Mirror::AluReadProfile* profile) {
#ifdef _WIN32
  const uint8_t *end = p + bytes;
  while (p < end) {
    MEMORY_BASIC_INFORMATION mbi;
    if (profile) ++profile->queries;
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

void Pm4Mirror::WriteRange(uint32_t first,const uint8_t* words,uint32_t count) {
  // Constant packets usually write an entire bank. Keep the same number of
  // version changes, but reduce it once per bank so endian conversion and
  // comparison can vectorize instead of branching on every register.
  if(first>=kAluConstantBase && first<kAluConstantBase+0x800 &&
     count<=kAluConstantBase+0x800-first) {
    while(count) {
      const bool pixel=first>=kAluConstantBase+0x400;
      const uint32_t bank=pixel?1:0;
      const uint32_t end=kAluConstantBase+(pixel?0x800:0x400);
      const uint32_t length=std::min(count,end-first);
      const uint32_t bank_offset=(first-kAluConstantBase)&0x3FFu;
      uint32_t first_change=length,last_change=0;
      uint32_t changes=0;
      auto* destination=regs_+first;
      for(size_t i=0;i<length;++i) {
        uint32_t value;
        std::memcpy(&value,words+i*4,4);
        value=__builtin_bswap32(value);
        if(destination[i]!=value) {
          ++changes;
          first_change=std::min(first_change,uint32_t(i));
          last_change=uint32_t(i+1);
          destination[i]=value;
        }
      }
      if(changes) {
        float_dirty_min_[bank]=std::min(float_dirty_min_[bank],bank_offset+first_change);
        float_dirty_max_[bank]=std::max(float_dirty_max_[bank],bank_offset+last_change);
      }
      std::memset(written_+first-kAluConstantBase,1,length);
      (pixel?ps_version:vs_version)+=changes;
      first+=length;words+=length*4;count-=length;
    }
    return;
  }
  for(uint32_t i=0;i<count;++i) Write(first+i,LoadBE(words,i*4));
}

void Pm4Mirror::ScanFrom(const uint8_t *base, const uint8_t *stream,
                         uint32_t begin, uint32_t end, int depth,
                         const MemoryReader *reader) {
#ifdef _WIN32
  // Each nested indirect buffer gets a separate, owned scratch slot until the
  // outer scan completes. Recycle the slots between top-level command scans.
  thread_local CheckedGuestReads indirect_reads;
  if (!depth && !reader)
    indirect_reads.Reset();
#endif
  uint32_t p = begin;
  while (p <= end && end - p >= 4) {
    uint32_t header = LoadBE(stream, p);
    p += 4;
    ++packets;
    // Xenia's command processor treats these as invalid/padding markers and
    // advances one dword. Decoding them as type-0 writes would desynchronize
    // the mirror from the GPU after a padded command segment.
    if (!header || header == 0x0BADF00Du)
      continue;
    switch (header >> 30) {
    case 0: {
      uint32_t count = ((header >> 16) & 0x3FFF) + 1;
      uint32_t index = header & 0x7FFF;
      bool one_register = (header >> 15) & 1;
      if (end - p < count * 4)
        return;
      if(one_register) for(uint32_t i=0;i<count;++i) Write(index,LoadBE(stream,p+4*i));
      else WriteRange(index,stream+p,count);
      p += count * 4;
      dwords += count;
      break;
    }
    case 1: {
      if (end - p < 8)
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
      if (end - p < count * 4)
        return;
      // As in the Xenos command processor, type-3 predication applies to
      // register writes and indirect buffers as well as to draws.
      if ((header & 1) && !(bin_mask_ & bin_select_)) {
        p += count * 4;
        dwords += count;
        break;
      }
      uint32_t d0 = count > 0 ? LoadBE(stream, p) : 0;
      switch (opcode) {
      case 0x21: { // REG_RMW: destination = (destination & and) | or
        if (count < 3)
          break;
        const uint32_t index = d0 & 0x1FFF;
        const uint32_t and_operand = LoadBE(stream, p + 4);
        const uint32_t or_operand = LoadBE(stream, p + 8);
        const uint32_t and_value = (d0 & 0x80000000u)
            ? reg(and_operand & 0x1FFF) : and_operand;
        const uint32_t or_value = (d0 & 0x40000000u)
            ? reg(or_operand & 0x1FFF) : or_operand;
        Write(index, (reg(index) & and_value) | or_value);
        break;
      }
      case 0x50: // SET_BIN_MASK (high, low)
        if (count >= 2)
          bin_mask_ = (uint64_t(d0) << 32) | LoadBE(stream, p + 4);
        break;
      case 0x51: // SET_BIN_SELECT (high, low)
        if (count >= 2)
          bin_select_ = (uint64_t(d0) << 32) | LoadBE(stream, p + 4);
        break;
      case 0x60: // SET_BIN_MASK_LO
        bin_mask_ = (bin_mask_ & 0xFFFFFFFF00000000ull) | d0;
        break;
      case 0x61: // SET_BIN_MASK_HI
        bin_mask_ = (bin_mask_ & 0xFFFFFFFFull) | (uint64_t(d0) << 32);
        break;
      case 0x62: // SET_BIN_SELECT_LO
        bin_select_ = (bin_select_ & 0xFFFFFFFF00000000ull) | d0;
        break;
      case 0x63: // SET_BIN_SELECT_HI
        bin_select_ = (bin_select_ & 0xFFFFFFFFull) | (uint64_t(d0) << 32);
        break;
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
        WriteRange(first,stream+p+4,count-1);
        break;
      }
      case 0x55:   // SET_CONSTANT2
      case 0x56: { // SET_SHADER_CONSTANTS
        uint32_t index = d0 & 0xFFFF;
        WriteRange(index,stream+p+4,count-1);
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
        const auto* live_source = reinterpret_cast<const uint8_t*>(
            reinterpret_cast<uintptr_t>(base) + uintptr_t(src));
        std::span<const uint8_t> captured;
        const uint8_t* source = nullptr;
        if (reader)
          captured = (*reader)(src, size * 4);
        bool readable;
        if (reader) {
          readable = captured.size() >= size * 4;
          source = captured.data();
        } else {
          auto* profile = alu_read_profile.enabled ? &alu_read_profile : nullptr;
          const auto begin = profile ? std::chrono::steady_clock::now()
                                    : std::chrono::steady_clock::time_point{};
#ifdef _WIN32
          if (bounded_alu_reads) {
            // WriteRange consumes this span synchronously. No source span
            // survives into the next ALU load or a nested command scan.
            captured = indirect_reads.ReadScratch(live_source, size * 4);
            readable = captured.size() >= size * 4;
            source = captured.data();
            if (profile) ++profile->bounded_calls;
          } else
#endif
          {
            readable = HostReadable(live_source, size * 4, profile);
            source = live_source;
          }
          if (profile) {
            const auto end = std::chrono::steady_clock::now();
            ++profile->calls;
            profile->bytes += size * 4;
            profile->failures += !readable;
            profile->validation_ns += uint64_t(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
          }
        }
        if (!readable) {
          ++unreadable_alu_loads;
          break;
        }
        WriteRange(first,source,size);
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
        if (!size || size > (1u << 18))
          break; // not a plausible command buffer
        indirect_dwords += size;
        if (reader) {
          auto captured = (*reader)(address, size * 4);
          if (captured.size() >= size * 4)
            ScanFrom(nullptr, captured.data(), 0, size * 4, depth + 1, reader);
          else
            ++unreadable_indirect_buffers;
        } else {
#ifdef _WIN32
          // Guest command pages may be recycled while the renderer consumes
          // them. Copy the full indirect stream before parsing so an invalid
          // or partially unreadable page cannot crash the process or mutate
          // register state from a truncated buffer.
          const auto* live_source = reinterpret_cast<const uint8_t*>(
              reinterpret_cast<uintptr_t>(base) + uintptr_t(address));
          const auto captured = indirect_reads.Read(live_source, size * 4);
          if (captured.size() >= size * 4)
            ScanFrom(base, captured.data(), 0, size * 4, depth + 1);
          else
            ++unreadable_indirect_buffers;
#else
          ScanFrom(base, base, address, address + size * 4, depth + 1);
#endif
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
