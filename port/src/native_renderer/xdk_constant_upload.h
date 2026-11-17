// Functional (non-matching) native rewrite of sub_821078C0, the XDK routine
// that writes a masked set of shader float constants into the command ring.
// Ported from the demo decomp (supermanReturns-demo-decomp,
// port/demo_runtime/native_constant_upload.cpp). The retail and demo generated
// bodies of 821078C0 are identical (same address, no differing offsets).
//
// Reconstructed behavior (821078C0..82107A2C):
//   r3 = device, r4 = 64-bit mask (bit 63 first, one bit = 4 float4), r5 =
//   first register index (dwords), r6 = source float4 array.
//   ring pointer p = [dev+40], limit = [dev+44].
//   do {
//     lz = cntlzd(mask); src += lz*64; reg = r5 + lz*16; mask <<= lz;
//     run = cntlzd(~mask); n = run*16 dwords;
//     if (p + (n+3)*4 >= limit)  -> sub_82107058 flush/grow (kept on the original)
//     pad p to 16-byte alignment with 0x80000000 words (p&15: 4->1, 0->2, 12->3)
//     *++p = ((n-1) << 16) | reg; r5 = reg + n;
//     copy run x 64 bytes (lvx/stvx, aligned) from src to p+4; mask <<= run;
//   } while (mask);
//   [dev+40] = p.
// A pre-pass computes every ring position; calls that would reach the flush
// path, a zero mask, overlapping source/ring ranges or ranges that cross the
// 0xE0000000 physical-offset boundary keep the translated original. The final
// volatile state (r4-r12, v0/v11-v13, cr0, cr6, xer.ca, lr) is reproduced;
// non-volatile registers are unchanged as after __restgprlr_25. Like the
// demo, the dead stack save area below r1 is not written.
//
// SR_NATIVE_CONSTANT_UPLOAD: 0 = translated original, 1 = native (default),
// 2 = shadow (native then original on the same inputs; ring bytes and
// registers compared, mismatches logged; the original's effects are kept).
// Default since 16/11/2026: shadow mode matched ~1M calls and title-flyover
// screenshots were correct; the A/B showed no measurable FPS gain on its own
// (docs/native-constant-upload-2026-11-16.md).
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <rex/logging.h>
#include <rex/ppc.h>

namespace superman_returns::native::constant_upload {

inline int Mode() {
  static const int mode = [] {
    const char* v = std::getenv("SR_NATIVE_CONSTANT_UPLOAD");
    if (!v || !*v) return 1;
    const int m = std::atoi(v);
    return m >= 0 && m <= 2 ? m : 1;
  }();
  return mode;
}

inline uint8_t* Host(uint8_t* base, uint32_t a) {
  return base + a + (a >= 0xE0000000u ? 0x1000u : 0u);
}
inline uint32_t Load32(uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, Host(base, a), 4);
  return __builtin_bswap32(v);
}
inline void Store32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(Host(base, a), &v, 4);
}
inline bool SameSide(uint64_t lo, uint64_t hi) {  // [lo, hi) does not cross 0xE0000000 or 4 GB
  if (hi > 0x100000000ull || lo >= hi) return false;
  return (lo >= 0xE0000000ull) == ((hi - 1) >= 0xE0000000ull);
}
inline int Clz64(uint64_t v) { return v == 0 ? 64 : __builtin_clzll(v); }
inline uint64_t Shl64(uint64_t v, uint64_t s) { return (s & 0x40) ? 0 : (v << (s & 0x7F)); }

struct Plan {
  uint64_t dest_lo, dest_hi, src_lo, src_hi;
};

// Pre-pass: true when the native path applies (no flush, admissible ranges).
inline bool PlanUpload(uint8_t* base, const PPCContext& ctx, Plan& plan) {
  uint64_t mask = ctx.r4.u64;
  if (mask == 0) return false;
  const uint32_t dev = ctx.r3.u32;
  if (dev >= 0x7F000000u) return false;
  uint64_t p = Load32(base, dev + 40);
  uint64_t src = uint64_t(ctx.r6.s64 - 4);
  plan.dest_lo = p;
  plan.src_lo = ~0ull;
  plan.src_hi = 0;
  do {
    const uint64_t lz = uint64_t(Clz64(mask));
    const uint32_t limit = Load32(base, dev + 44);
    src += (lz << 6) & 0xFFFFFFC0u;
    mask = Shl64(mask, lz);
    const uint64_t run = uint64_t(Clz64(~mask));
    const uint64_t n = (run << 4) & 0xFFFFFFF0u;
    const uint64_t check = (((n + 3) << 2) & 0xFFFFFFFCu) + p;
    if (!(uint32_t(check) < limit)) return false;
    const uint32_t align = uint32_t(p) & 15;
    p += align == 4 ? 4 : align == 0 ? 8 : align == 12 ? 12 : 0;
    p += 4;  // header
    if (run == 0) return false;
    plan.src_lo = std::min(plan.src_lo, (src + 4) & ~0xFull);
    p += run * 64;
    src += run * 64;
    plan.src_hi = std::max(plan.src_hi, ((src - 64 + 52) & ~0xFull) + 16);
    mask = Shl64(mask, run);
  } while (mask != 0);
  plan.dest_hi = ((p - 64 + 52) & ~0xFull) + 16;
  if (!SameSide(plan.dest_lo, plan.dest_hi) || !SameSide(plan.src_lo, plan.src_hi)) return false;
  if (plan.dest_lo < plan.src_hi && plan.src_lo < plan.dest_hi) return false;
  return plan.dest_hi <= 0x100000000ull;
}

inline simde__m128i LoadVec(uint8_t* base, uint32_t ea) {
  ea &= ~0xFu;
  return simde_mm_shuffle_epi8(simde_mm_load_si128(reinterpret_cast<simde__m128i*>(Host(base, ea))),
                               simde_mm_load_si128(reinterpret_cast<const simde__m128i*>(VectorMaskL)));
}
inline void StoreVec(uint8_t* base, uint32_t ea, simde__m128i v) {
  ea &= ~0xFu;
  simde_mm_store_si128(reinterpret_cast<simde__m128i*>(Host(base, ea)),
                       simde_mm_shuffle_epi8(v, simde_mm_load_si128(reinterpret_cast<const simde__m128i*>(VectorMaskL))));
}

// Executes the planned upload and leaves the final volatile state in ctx.
inline void NativeUpload(PPCContext& ctx, uint8_t* base) {
  uint64_t mask = ctx.r4.u64;
  const uint32_t dev = ctx.r3.u32;
  uint64_t p = Load32(base, dev + 40);
  uint64_t r5 = ctx.r5.u64;
  uint64_t src = uint64_t(ctx.r6.s64 - 4);
  const uint32_t pad = 0x80000000u;
  simde__m128i v0{}, v13{}, v12{}, v11{};
  do {
    const uint64_t lz = uint64_t(Clz64(mask));
    src += (lz << 6) & 0xFFFFFFC0u;
    const uint64_t reg = ((lz << 4) & 0xFFFFFFF0u) + r5;
    mask = Shl64(mask, lz);
    const uint64_t run = uint64_t(Clz64(~mask));
    const uint64_t n = (run << 4) & 0xFFFFFFF0u;
    const uint32_t align = uint32_t(p) & 15;
    const int pads = align == 4 ? 1 : align == 0 ? 2 : align == 12 ? 3 : 0;
    for (int i = 0; i < pads; ++i) { p = uint32_t(p + 4); Store32(base, uint32_t(p), pad); }
    p = uint32_t(p + 4);
    Store32(base, uint32_t(p), uint32_t((((n - 1) << 16) & 0xFFFF0000u) | reg));
    r5 = n + reg;
    for (uint64_t i = 0; i < run; ++i) {
      v0 = LoadVec(base, uint32_t(src + 4));
      v13 = LoadVec(base, uint32_t(src + 20));
      v12 = LoadVec(base, uint32_t(src + 36));
      v11 = LoadVec(base, uint32_t(src + 52));
      src += 64;
      StoreVec(base, uint32_t(p + 4), v0);
      StoreVec(base, uint32_t(p + 20), v13);
      StoreVec(base, uint32_t(p + 36), v12);
      StoreVec(base, uint32_t(p + 52), v11);
      p += 64;
    }
    mask = Shl64(mask, run);
  } while (mask != 0);
  Store32(base, dev + 40, uint32_t(p));
  ctx.r4.u64 = p;
  ctx.r5.u64 = r5;
  ctx.r7.u64 = p - 64 + 4;
  ctx.r8.u64 = src - 64 + 52;
  ctx.r9.u64 = p - 64 + 52;
  ctx.r10.u64 = p - 64 + 36;
  ctx.r11.u64 = p - 64 + 20;
  simde_mm_store_si128(reinterpret_cast<simde__m128i*>(ctx.v0.u8), v0);
  simde_mm_store_si128(reinterpret_cast<simde__m128i*>(ctx.v13.u8), v13);
  simde_mm_store_si128(reinterpret_cast<simde__m128i*>(ctx.v12.u8), v12);
  simde_mm_store_si128(reinterpret_cast<simde__m128i*>(ctx.v11.u8), v11);
  ctx.xer.ca = 1;  // addic. r28,r28,-1 with r28 = 1
  ctx.cr0.compare<int32_t>(0, 0, ctx.xer);
  ctx.cr6.compare<uint64_t>(0, 0, ctx.xer);
  ctx.r12.u64 = uint32_t(ctx.lr);  // __restgprlr_25: lwz r12,-8(r1); mtlr r12
  ctx.lr = ctx.r12.u64;
}

struct Counters {
  std::atomic<uint64_t> calls{0}, fallbacks{0}, mismatches{0};
};
inline Counters& GetCounters() {
  static Counters counters;
  return counters;
}

inline bool SameVec(const PPCVRegister& a, const PPCVRegister& b) { return std::memcmp(a.u8, b.u8, 16) == 0; }

// Runs sub_821078C0 according to Mode(). `original` is the translated routine.
template <typename Original>
inline void Run(PPCContext& ctx, uint8_t* base, Original&& original) {
  const int mode = Mode();
  Plan plan{};
  auto& counters = GetCounters();
  if (mode == 0 || !PlanUpload(base, ctx, plan)) {
    if (mode == 2) counters.fallbacks.fetch_add(1, std::memory_order_relaxed);
    original(ctx, base);
    return;
  }
  if (mode == 1) {
    NativeUpload(ctx, base);
    return;
  }
  const uint32_t dev = ctx.r3.u32;
  const uint32_t saved_ptr = Load32(base, dev + 40);
  PPCContext native = ctx;
  NativeUpload(native, base);
  const size_t bytes = size_t(plan.dest_hi - plan.dest_lo);
  std::vector<uint8_t> ring(bytes);
  std::memcpy(ring.data(), Host(base, uint32_t(plan.dest_lo)), bytes);
  const uint32_t native_ptr = Load32(base, dev + 40);
  Store32(base, dev + 40, saved_ptr);
  original(ctx, base);
  const uint64_t calls = counters.calls.fetch_add(1, std::memory_order_relaxed) + 1;
  const bool same =
      std::memcmp(ring.data(), Host(base, uint32_t(plan.dest_lo)), bytes) == 0 &&
      Load32(base, dev + 40) == native_ptr && ctx.r3.u64 == native.r3.u64 &&
      ctx.r4.u64 == native.r4.u64 && ctx.r5.u64 == native.r5.u64 && ctx.r6.u64 == native.r6.u64 &&
      ctx.r7.u64 == native.r7.u64 && ctx.r8.u64 == native.r8.u64 && ctx.r9.u64 == native.r9.u64 &&
      ctx.r10.u64 == native.r10.u64 && ctx.r11.u64 == native.r11.u64 &&
      ctx.r12.u64 == native.r12.u64 && ctx.lr == native.lr && ctx.xer.ca == native.xer.ca &&
      ctx.cr0.eq == native.cr0.eq && ctx.cr0.lt == native.cr0.lt && ctx.cr0.gt == native.cr0.gt &&
      ctx.cr6.eq == native.cr6.eq && ctx.cr6.lt == native.cr6.lt && ctx.cr6.gt == native.cr6.gt &&
      SameVec(ctx.v0, native.v0) && SameVec(ctx.v11, native.v11) && SameVec(ctx.v12, native.v12) &&
      SameVec(ctx.v13, native.v13);
  if (!same) {
    const uint64_t k = counters.mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (k <= 20)
      REXLOG_WARN("[native constant upload] MISMATCH #{} dev={:08X} ptr {:08X}/{:08X} r4 {:X}/{:X} "
                  "r5 {:X}/{:X} r8 {:X}/{:X} r11 {:X}/{:X}", k, dev, Load32(base, dev + 40), native_ptr,
                  ctx.r4.u64, native.r4.u64, ctx.r5.u64, native.r5.u64, ctx.r8.u64, native.r8.u64,
                  ctx.r11.u64, native.r11.u64);
  }
  if ((calls & ((1u << 18) - 1)) == 0)
    REXLOG_INFO("[native constant upload] shadow calls={} mismatches={} fallbacks={}", calls,
                counters.mismatches.load(std::memory_order_relaxed),
                counters.fallbacks.load(std::memory_order_relaxed));
}

}  // namespace superman_returns::native::constant_upload
