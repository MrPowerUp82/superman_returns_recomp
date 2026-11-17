// Retail port of the demo decomp's port/demo_runtime/native_constant_copy.cpp
// (supermanReturns-demo-decomp). Mechanical changes: cvars -> environment
// variables, demo -> retail addresses (none: identical bodies).
// See docs/native-demo-ports-2026-11-16.md.
//
// Functional (non-matching) native rewrite of the XDK float-constant copies
// sub_820F4920 (vertex bank, +112 float4, dirty mask dev+0) and sub_820F4A30
// (pixel bank, +368 float4, dirty mask dev+8). Native bring-up only: it does
// not count as matching decompilation.
//
// The pinned reference renderer hooks the same routines (TryCopyConstantBank,
// aligned sources only, plus a native prefix for the rest). In the demo flight
// profile the translated original still took ~4% of FGGameRender, because most
// sources are not 16-byte aligned. port/demo_native/game_profile.h therefore
// marks the renderer's FLOAT_COPY roles absent and this file owns both hooks.
//
// Reconstructed behavior (analysis/demo_image.bin 820F4920..820F4A30 and
// 820F4A30..820F4B40; the routines differ only in bank and mask offsets):
//   std r7,48(r1); stw r3,-32(r1); dest = r3 + ((r4 + bank) << 4)
//   copy r6 float4 from r5 (unaligned: lvlx|lvrx) to dest (stvx), four per
//   iteration while possible, then one at a time
//   mask[dev + off] |= r7 (64-bit)
// The copy itself is a byte copy. The final volatile state (r4-r11, v0, v7-v13,
// cr0, cr6, xer.ca) is rebuilt from the last iteration using the exact
// lvlx/lvrx expressions the recompiler emits. Inputs with overlapping ranges,
// unaligned devices, counts above 256 or addresses at or above 0xA0000000 keep
// the translated original.
//
// --sr_demo_native_constant_copy=0 original, 1 native (default), 2 shadow
// (native then original on the same inputs; bank bytes, mask and registers
// compared; the original's effects are kept).
#include <atomic>
#include <cstring>
#include <vector>

#include <cstdlib>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>



namespace {
// SR_NATIVE_CONSTANT_COPY: 0 = translated original, 1 = native (default since the 16/11/2026 shadow run and screenshots), 2 = shadow.
int Mode_sr_demo_native_constant_copy() {
  static const int mode = [] {
    const char* v = std::getenv("SR_NATIVE_CONSTANT_COPY");
    if (!v || !*v) return 1;
    const int m = std::atoi(v);
    return m >= 0 && m <= 2 ? m : 1;
  }();
  return mode;
}
}  // namespace

namespace {

constexpr uint64_t kLimit = 0xA0000000ull;

inline uint64_t Load64(const uint8_t* base, uint32_t a) {
  uint64_t v;
  std::memcpy(&v, base + a, 8);
  return __builtin_bswap64(v);
}
inline void Store64(uint8_t* base, uint32_t a, uint64_t v) {
  v = __builtin_bswap64(v);
  std::memcpy(base + a, &v, 8);
}

// REX_RAW_ADDR: physical views at 0xE0000000+ are offset by 0x1000 on Windows.
inline uint8_t* Host(uint8_t* base, uint32_t a) { return base + a + (a >= 0xE0000000u ? 0x1000u : 0u); }
// Guest views: [0xA0000000, 0xC0000000), [0xC0000000, 0xE0000000), [0xE0000000, 4 GB)
// all show physical memory, as does [0, 0x40000000). Addresses in
// [0x40000000, 0xA0000000) cannot alias it.
inline int View(uint64_t a) { return a < 0xA0000000ull ? 0 : a < 0xC0000000ull ? 1 : a < 0xE0000000ull ? 2 : 3; }
inline bool NoPhysicalAlias(uint64_t a, uint64_t n) { return a >= 0x40000000ull && a + n <= 0xA0000000ull; }

inline simde__m128i Lvlx(uint8_t* base, uint32_t ea) {
  return simde_mm_shuffle_epi8(
      simde_mm_load_si128(reinterpret_cast<const simde__m128i*>(Host(base, ea & ~0xFu))),
      simde_mm_load_si128(reinterpret_cast<const simde__m128i*>(&VectorMaskL[(ea & 0xF) * 16])));
}
inline simde__m128i Lvrx(uint8_t* base, uint32_t ea) {
  if (!(ea & 0xF)) return simde_mm_setzero_si128();
  return simde_mm_shuffle_epi8(
      simde_mm_load_si128(reinterpret_cast<const simde__m128i*>(Host(base, ea & ~0xFu))),
      simde_mm_load_si128(reinterpret_cast<const simde__m128i*>(&VectorMaskR[(ea & 0xF) * 16])));
}
inline void Put(PPCVRegister& v, simde__m128i x) {
  simde_mm_store_si128(reinterpret_cast<simde__m128i*>(v.u8), x);
}

std::atomic<uint64_t> g_refuse[6]{};  // count/start, device align, device range, source range, stack, overlap

[[gnu::always_inline]] inline bool AdmissibleArgs(uint64_t count, uint64_t start, uint64_t device,
                                                  uint64_t source, uint64_t stack, uint32_t bank) {
  auto refuse = [](int i) { g_refuse[i].fetch_add(1, std::memory_order_relaxed); return false; };
  if (count > 256 || start > 256 - count) return refuse(0);
  if (device & 15) return refuse(1);
  if (device >= kLimit) return refuse(2);
  if (stack < 32 || stack + 56 > kLimit) return refuse(4);
  const uint64_t dest = device + (bank + start) * 16, bytes = count * 16;
  if (dest + bytes > kLimit) return refuse(2);
  // lvrx of the block after the last source float4 reads up to 16 more bytes.
  if (source + bytes + 16 > 0x100000000ull || View(source) != View(source + bytes + 15)) return refuse(3);
  const auto overlaps = [](uint64_t a, uint64_t n, uint64_t b, uint64_t m) {
    return n && m && a < b + m && b < a + n;
  };
  if (overlaps(dest, bytes, stack - 32, 4) || overlaps(dest, bytes, stack + 48, 8)) return refuse(5);
  if (source >= kLimit) {
    // Physical source: the destination, the dirty masks and the stack slots
    // must be in views that cannot alias physical memory (no hidden overlap).
    if (!NoPhysicalAlias(dest, bytes) || !NoPhysicalAlias(stack - 32, 88) ||
        !NoPhysicalAlias(device, 16))
      return refuse(3);
    return true;
  }
  if (overlaps(source, bytes, dest, bytes) || overlaps(source, bytes, stack - 32, 4) ||
      overlaps(source, bytes, stack + 48, 8)) return refuse(5);
  return true;
}

[[gnu::always_inline]] inline bool Admissible(const PPCContext& ctx, uint32_t bank) {
  return AdmissibleArgs(ctx.r6.u64, ctx.r4.u64, ctx.r3.u64, ctx.r5.u64, ctx.r1.u32, bank);
}

[[gnu::always_inline]] inline void NativeCopy(PPCContext& ctx, uint8_t* base, uint32_t bank, uint32_t mask_offset) {
  const uint32_t device = ctx.r3.u32, source = ctx.r5.u32, stack = ctx.r1.u32;
  const uint32_t count = ctx.r6.u32;
  const uint32_t dest = device + ((ctx.r4.u32 + bank) << 4);
  Store64(base, stack + 48, ctx.r7.u64);
  const uint32_t saved_device = __builtin_bswap32(ctx.r3.u32);
  std::memcpy(base + stack - 32, &saved_device, 4);
  if (count) std::memcpy(base + dest, Host(base, source), size_t(count) * 16);

  ctx.r8.s64 = 128;
  uint32_t r11 = source, r10 = dest, r9 = count;
  if (count > 3) {
    const uint32_t groups = ((count - 4) >> 2) + 1;
    const uint32_t s = source + (groups - 1) * 64, d = dest + (groups - 1) * 64;
    const simde__m128i v13a = Lvrx(base, s + 16), v12a = Lvlx(base, s + 16), v11a = Lvrx(base, s + 32);
    const simde__m128i v9 = Lvrx(base, s + 48), v10 = Lvlx(base, s + 32), v8 = Lvlx(base, s + 48);
    const simde__m128i v7 = Lvrx(base, s + 64);
    Put(ctx.v0, simde_mm_or_si128(Lvlx(base, s), v13a));
    Put(ctx.v13, simde_mm_or_si128(v12a, v11a));
    Put(ctx.v12, simde_mm_or_si128(v10, v9));
    Put(ctx.v11, simde_mm_or_si128(v8, v7));
    Put(ctx.v10, v10);
    Put(ctx.v9, v9);
    Put(ctx.v8, v8);
    Put(ctx.v7, v7);
    ctx.r4.u64 = uint64_t(d) + 32;
    ctx.r5.s64 = 64;
    ctx.r6.u64 = uint64_t(d) + 48;
    ctx.r7.u64 = uint64_t(d) + 16;
    ctx.r8.s64 = 0;
    ctx.xer.ca = 1;
    ctx.cr0.compare<int32_t>(0, 0, ctx.xer);
    r11 = source + groups * 64;
    r10 = dest + groups * 64;
    r9 = count - groups * 4;
  }
  ctx.cr6.compare<uint32_t>(r9, 0, ctx.xer);
  if (r9) {
    const uint32_t s = r11 + (r9 - 1) * 16;
    const simde__m128i v13 = Lvrx(base, s + 16);
    Put(ctx.v13, v13);
    Put(ctx.v0, simde_mm_or_si128(Lvlx(base, s), v13));
    ctx.r8.u64 = s;
    ctx.r7.u64 = s;
    ctx.r6.s64 = 16;
    ctx.xer.ca = 1;
    ctx.cr0.compare<int32_t>(0, 0, ctx.xer);
  }
  (void)r10;
  ctx.r11.u64 = device;
  ctx.r9.u64 = Load64(base, stack + 48);
  ctx.r10.u64 = Load64(base, device + mask_offset) | ctx.r9.u64;
  Store64(base, device + mask_offset, ctx.r10.u64);
}

std::atomic<uint64_t> g_calls{0}, g_fallbacks{0}, g_mismatches{0};

bool SameVec(const PPCVRegister& a, const PPCVRegister& b) { return std::memcmp(a.u8, b.u8, 16) == 0; }

template <uint32_t kBank, uint32_t kMaskOffset>
void Run(PPCContext& ctx, uint8_t* base, void (*original)(PPCContext&, uint8_t*)) {
  const int32_t mode = Mode_sr_demo_native_constant_copy();
  if (mode == 0 || !Admissible(ctx, kBank)) {
    if (mode != 0) g_fallbacks.fetch_add(1, std::memory_order_relaxed);
    original(ctx, base);
    return;
  }
  if (mode == 1) {
    NativeCopy(ctx, base, kBank, kMaskOffset);
    return;
  }
  const uint32_t device = ctx.r3.u32;
  const uint32_t dest = device + ((ctx.r4.u32 + kBank) << 4);
  const size_t bytes = size_t(ctx.r6.u32) * 16;
  const uint64_t saved_mask = Load64(base, device + kMaskOffset);
  PPCContext native = ctx;
  NativeCopy(native, base, kBank, kMaskOffset);
  std::vector<uint8_t> bank(base + dest, base + dest + bytes);
  const uint64_t native_mask = Load64(base, device + kMaskOffset);
  Store64(base, device + kMaskOffset, saved_mask);
  original(ctx, base);
  const uint64_t calls = g_calls.fetch_add(1, std::memory_order_relaxed) + 1;
  bool same = (bytes == 0 || std::memcmp(bank.data(), base + dest, bytes) == 0) &&
              Load64(base, device + kMaskOffset) == native_mask;
  const PPCRegister* a[] = {&ctx.r4, &ctx.r5, &ctx.r6, &ctx.r7, &ctx.r8, &ctx.r9, &ctx.r10, &ctx.r11};
  const PPCRegister* b[] = {&native.r4, &native.r5, &native.r6, &native.r7, &native.r8, &native.r9,
                            &native.r10, &native.r11};
  for (int i = 0; i < 8; ++i) same = same && a[i]->u64 == b[i]->u64;
  same = same && ctx.r3.u64 == native.r3.u64 && ctx.xer.ca == native.xer.ca &&
         ctx.cr0.eq == native.cr0.eq && ctx.cr0.lt == native.cr0.lt && ctx.cr0.gt == native.cr0.gt &&
         ctx.cr6.eq == native.cr6.eq && ctx.cr6.lt == native.cr6.lt && ctx.cr6.gt == native.cr6.gt &&
         SameVec(ctx.v0, native.v0) && SameVec(ctx.v7, native.v7) && SameVec(ctx.v8, native.v8) &&
         SameVec(ctx.v9, native.v9) && SameVec(ctx.v10, native.v10) && SameVec(ctx.v11, native.v11) &&
         SameVec(ctx.v12, native.v12) && SameVec(ctx.v13, native.v13);
  if (!same) {
    const uint64_t k = g_mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (k <= 20)
      REXLOG_WARN("[native constant copy] MISMATCH #{} bank={} dev={:08X} start={} src={:08X} "
                  "count={} r7 {:X}/{:X} r8 {:X}/{:X}", k, kBank, device, native.r4.u64, ctx.r5.u32,
                  bytes / 16, ctx.r7.u64, native.r7.u64, ctx.r8.u64, native.r8.u64);
  }
  if ((calls & ((1u << 20) - 1)) == 0)
    REXLOG_INFO("[native constant copy] shadow calls={} mismatches={} fallbacks={} "
                "(count {} dev_align {} dev_range {} src_range {} stack {} overlap {}) last_src={:08X}",
                calls, g_mismatches.load(std::memory_order_relaxed),
                g_fallbacks.load(std::memory_order_relaxed), g_refuse[0].load(), g_refuse[1].load(),
                g_refuse[2].load(), g_refuse[3].load(), g_refuse[4].load(), g_refuse[5].load(), ctx.r5.u32);
}

}  // namespace

namespace superman_returns::native {
// sub_820F4920 (pixel = false) / sub_820F4A30 (pixel = true) by
// SR_NATIVE_CONSTANT_COPY. Returns false in mode 0, so the caller keeps its
// existing path; otherwise the copy (native, shadow or the translated
// fallback for refused inputs) has run.
bool RunConstantCopy(PPCContext& ctx, uint8_t* base, bool pixel,
                     void (*original)(PPCContext&, uint8_t*)) {
  if (Mode_sr_demo_native_constant_copy() == 0) return false;
  if (pixel) Run<368, 8>(ctx, base, original);
  else Run<112, 0>(ctx, base, original);
  return true;
}
}  // namespace superman_returns::native
