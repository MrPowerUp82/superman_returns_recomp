// Retail port of the demo decomp's port/demo_runtime/native_leaf_copies.cpp
// (supermanReturns-demo-decomp). Mechanical changes: cvars -> environment
// variables, demo -> retail addresses (82609490->826092B8).
// See docs/native-demo-ports-2026-11-16.md.
//
// Functional (non-matching) native rewrite of a hot guest leaf on the
// render thread (flight profile 12/11/2026: 1.25% and 1.37% own samples of
// FGGameRender). Native bring-up only: neither counts as matching
// decompilation.
//
// Demo only, not ported (825F6B38 is a different function in retail):
// sub_825F6B38 (analysis/demo_image.bin 825F6B38..825F6C10): memcpy(r3 = dst,
//   r4 = src, r5 = n), returns r3. n < 64: forward byte loop. n >= 64: bytes
//   until src is 4-aligned, then (only if dst is 4-aligned and n >= 16) 16-byte
//   blocks of lwz/stw, then a byte tail. Non-overlapping ranges in ordinary
//   virtual memory are copied with host memcpy; overlapping ranges, ranges that
//   reach 0x7F000000 (GPU/MMIO windows) or physical aliases keep the
//   translated original. The final volatile state (r5, r9-r11, cr6) of the
//   path the original would take is reproduced.
//
// sub_826092B8 (retail 826092B8..8260934C, demo 82609490..82609524): walks 8-byte records starting at r3,
//   terminated by a zero first halfword. For a record at p with id = hw(p):
//     mask[0x82998A00 + ((id >> 3) & ~3)] |= 1 << (id & 31)      (u32, BE)
//     if hw(p+2) != 0: table[0x82998B00 + id*4] = (p+8) + hw(p+4);
//                      next = table value + hw(p+2)
//     else:            table[...] = u32(p+8); next = p+12
//   and returns r3 = terminator + 8. Loads and stores are performed in the
//   original order, so aliasing between the list and the tables behaves the
//   same. r5-r11 and cr6 are reproduced.
//
// Cvars: 0 = translated original, 1 = native, 2 = shadow (native computed into
// a host overlay, original executed, memory and registers compared; the
// original's effects are kept).
#include <atomic>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <cstdlib>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

REX_EXTERN(__imp__sub_826092B8);


namespace {
// SR_NATIVE_RECORD_BITS: 0 = translated original, 1 = native (default since the 16/11/2026 shadow run and screenshots), 2 = shadow.
int Mode_sr_demo_native_record_bits() {
  static const int mode = [] {
    const char* v = std::getenv("SR_NATIVE_RECORD_BITS");
    if (!v || !*v) return 1;
    const int m = std::atoi(v);
    return m >= 0 && m <= 2 ? m : 1;
  }();
  return mode;
}
}  // namespace

namespace {

constexpr uint32_t kGuestLimit = 0x7F000000u;  // below GPU/MMIO windows and physical aliases
constexpr uint32_t kMaskBase = 0x82998A00u;
constexpr uint32_t kTableBase = 0x82998B00u;

// Same host translation as REX_RAW_ADDR (physical views at 0xE0000000+ are
// offset by 0x1000 on Windows).
inline const uint8_t* Host(const uint8_t* base, uint32_t a) {
  return base + a + (a >= 0xE0000000u ? 0x1000u : 0u);
}
inline uint8_t* Host(uint8_t* base, uint32_t a) {
  return base + a + (a >= 0xE0000000u ? 0x1000u : 0u);
}
inline bool InGpuWindow(uint32_t a) { return a >= 0x7F000000u && a < 0x80000000u; }
inline uint16_t Load16(const uint8_t* base, uint32_t a) {
  uint16_t v;
  std::memcpy(&v, Host(base, a), 2);
  return __builtin_bswap16(v);
}
inline uint32_t Load32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, Host(base, a), 4);
  return __builtin_bswap32(v);
}
inline void Store32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(Host(base, a), &v, 4);
}

// ---- record walk --------------------------------------------------------------

struct Overlay {
  std::unordered_map<uint32_t, uint32_t> words;  // aligned-by-caller u32 writes
};

template <bool kShadow>
struct RecordMemory {
  uint8_t* base;
  Overlay* overlay;
  // Halfword/word reads must see earlier native writes in shadow mode.
  uint16_t Hw(uint32_t a) const {
    if constexpr (kShadow) {
      const uint32_t w = a & ~3u;
      if (auto it = overlay->words.find(w); it != overlay->words.end())
        return uint16_t((it->second >> ((2 - (a & 2)) * 8)) & 0xFFFF);
    }
    return Load16(base, a);
  }
  uint32_t W(uint32_t a) const {
    if constexpr (kShadow) {
      if (!(a & 3))
        if (auto it = overlay->words.find(a); it != overlay->words.end()) return it->second;
    }
    return Load32(base, a);
  }
  void SetW(uint32_t a, uint32_t v) {
    if constexpr (kShadow) overlay->words[a] = v; else Store32(base, a, v);
  }
};

struct WalkRegs {
  uint32_t r3, r5, r9, r10, r11;
};

template <bool kShadow>
WalkRegs WalkRecords(RecordMemory<kShadow>& m, uint32_t r3_in, uint32_t r5_in, uint32_t r9_in) {
  uint32_t p = r3_in, r3 = p + 8, r5 = r5_in, r9 = r9_in;
  for (;;) {
    if (InGpuWindow(p) || InGpuWindow(r3)) [[unlikely]] {
      REXLOG_ERROR("[native record bits] record pointer {:08X} in the GPU window; walk stopped", p);
      return {r3, r5, r9, 0, p};
    }
    const uint32_t id = m.Hw(p);
    const uint32_t mask_word = kMaskBase + ((id >> 3) & 0x1FFFFFFCu);
    r5 = m.W(mask_word);
    m.SetW(mask_word, (1u << (id & 31)) | r5);
    if (m.Hw(p + 2) != 0) {
      const uint32_t value = uint32_t(m.Hw(p + 4)) + r3;
      const uint32_t slot = uint32_t(m.Hw(p)) << 2;
      m.SetW(kTableBase + slot, value);
      r9 = slot;
      p = uint32_t(m.Hw(p + 2)) + value;
    } else {
      const uint32_t slot = uint32_t(m.Hw(p)) << 2;
      r9 = m.W(r3);
      m.SetW(kTableBase + slot, r9);
      p = r3 + 4;
    }
    const uint32_t next = m.Hw(p);
    r3 = p + 8;
    if (next == 0) return {r3, r5, r9, 0, p};
  }
}

std::atomic<uint64_t> g_walk_calls{0}, g_walk_mismatches{0};

}  // namespace

namespace superman_returns::native {
// sub_826092B8 by SR_NATIVE_RECORD_BITS (called by the guest_cpu_profile wrapper).
void RunRecordBits(PPCContext& ctx, uint8_t* base) {
  const int32_t mode = Mode_sr_demo_native_record_bits();
  // The chain is game data; a GPU/MMIO-window pointer is checked inside the
  // walk (WalkRecords) instead of by a second read-only pass, which doubled
  // the cost of these long lists (flight profile: 1.36% -> 1.92%).
  if (mode == 0 || InGpuWindow(ctx.r3.u32)) {
    __imp__sub_826092B8(ctx, base);
    return;
  }
  if (mode == 1) {
    RecordMemory<false> m{base, nullptr};
    const WalkRegs r = WalkRecords(m, ctx.r3.u32, ctx.r5.u32, ctx.r9.u32);
    ctx.r3.u64 = r.r3; ctx.r5.u64 = r.r5; ctx.r9.u64 = r.r9; ctx.r10.u64 = r.r10;
    ctx.r11.u64 = r.r11; ctx.r6.u64 = 1; ctx.r7.u64 = kMaskBase; ctx.r8.u64 = kTableBase;
    ctx.cr6.compare<uint32_t>(0, 0, ctx.xer);
    return;
  }
  Overlay overlay;
  RecordMemory<true> m{base, &overlay};
  const uint32_t r3_in = ctx.r3.u32;
  const WalkRegs r = WalkRecords(m, ctx.r3.u32, ctx.r5.u32, ctx.r9.u32);
  __imp__sub_826092B8(ctx, base);
  const uint64_t calls = g_walk_calls.fetch_add(1, std::memory_order_relaxed) + 1;
  bool same = ctx.r3.u32 == r.r3 && ctx.r5.u32 == r.r5 && ctx.r9.u32 == r.r9 &&
              ctx.r10.u32 == r.r10 && ctx.r11.u32 == r.r11 && ctx.r6.u32 == 1 &&
              ctx.r7.u32 == kMaskBase && ctx.r8.u32 == kTableBase && ctx.cr6.eq;
  for (const auto& [address, value] : overlay.words)
    if (Load32(base, address) != value) { same = false; break; }
  if (!same) {
    const uint64_t k = g_walk_mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (k <= 20)
      REXLOG_WARN("[native record bits] MISMATCH #{} list={:08X} r3 {:08X}/{:08X} r9 {:08X}/{:08X} "
                  "r11 {:08X}/{:08X} words={}", k, r3_in, ctx.r3.u32, r.r3, ctx.r9.u32, r.r9,
                  ctx.r11.u32, r.r11, overlay.words.size());
  }
  if ((calls & ((1u << 10) - 1)) == 0)
    REXLOG_INFO("[native record bits] shadow calls={} mismatches={}", calls,
                g_walk_mismatches.load(std::memory_order_relaxed));
}
}  // namespace superman_returns::native
