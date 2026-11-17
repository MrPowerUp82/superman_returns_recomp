// Retail port of the demo decomp's port/demo_runtime/native_constant_upload_c.cpp
// (supermanReturns-demo-decomp). Mechanical changes: cvars -> environment
// variables, demo -> retail addresses (none: identical bodies).
// See docs/native-demo-ports-2026-11-16.md.
//
// Functional (non-matching) native rewrite of sub_82107520, the one-dword-per-
// bit sibling of sub_821078C0 / sub_82107760 that writes masked constants into
// the command ring (flight profile 12/11/2026: 0.72% own samples of
// FGGameRender). Native bring-up only: it does not count as matching
// decompilation.
//
// Reconstructed behavior (analysis/demo_image.bin 82107520..821075E0):
//   r3 = device, r4 = 64-bit mask (bit 63 first), r5 = first register, r6 = source
//   p = [dev+40]; src = r6 - 4
//   do {
//     lz = cntlzd(mask); reg = r5 + lz; src += lz*4; mask <<= lz;
//     run = cntlzd(~mask);
//     if (p + run*4 >= [dev+44]) -> sub_82107058 flush (kept on the original)
//     *++p = ((run-1) << 16) | reg; r5 = reg + run;
//     run times: *++p = *++src; mask <<= run;
//   } while (mask);
//   [dev+40] = p.
// Calls that would reach the flush path, a zero mask, overlapping ranges or
// ranges crossing a guest view boundary keep the translated original. The final
// volatile state (r4, r5, r8-r11, cr0, cr6, xer.ca, r12, lr) is reproduced.
//
// --sr_demo_native_constant_upload_c=0 original, 1 native (default), 2 shadow.
#include <atomic>
#include <cstring>
#include <vector>

#include <cstdlib>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

REX_EXTERN(__imp__sub_82107520);


namespace {
// SR_NATIVE_CONSTANT_UPLOAD_C: 0 = translated original, 1 = native (default since the 16/11/2026 shadow run and screenshots), 2 = shadow.
int Mode_sr_demo_native_constant_upload_c() {
  static const int mode = [] {
    const char* v = std::getenv("SR_NATIVE_CONSTANT_UPLOAD_C");
    if (!v || !*v) return 1;
    const int m = std::atoi(v);
    return m >= 0 && m <= 2 ? m : 1;
  }();
  return mode;
}
}  // namespace

namespace {

inline uint8_t* Host(uint8_t* base, uint32_t a) { return base + a + (a >= 0xE0000000u ? 0x1000u : 0u); }
inline uint32_t Load32(uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, Host(base, a), 4);
  return __builtin_bswap32(v);
}
inline void Store32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(Host(base, a), &v, 4);
}
inline int View(uint64_t a) {
  return a < 0x7F000000ull ? 0 : a < 0x80000000ull ? -1 : a < 0xA0000000ull ? 1
       : a < 0xC0000000ull ? 2 : a < 0xE0000000ull ? 3 : 4;
}
inline bool OneView(uint64_t lo, uint64_t hi) {
  if (hi > 0x100000000ull || lo >= hi) return false;
  const int v = View(lo);
  return v >= 0 && v == View(hi - 1);
}
inline int Clz64(uint64_t v) { return v == 0 ? 64 : __builtin_clzll(v); }
inline uint64_t Shl64(uint64_t v, uint64_t s) { return (s & 0x40) ? 0 : (v << (s & 0x7F)); }

struct Plan {
  uint64_t dest_lo, dest_hi, src_lo, src_hi;
};

bool PlanUpload(uint8_t* base, const PPCContext& ctx, Plan& plan) {
  uint64_t mask = ctx.r4.u64;
  const uint32_t dev = ctx.r3.u32;
  if (mask == 0 || View(dev) != 0 || View(uint64_t(dev) + 48) != 0) return false;
  uint64_t p = Load32(base, dev + 40);
  uint64_t src = uint64_t(ctx.r6.s64 - 4);
  plan.dest_lo = p + 4;
  plan.src_lo = ~0ull;
  plan.src_hi = 0;
  do {
    const uint64_t lz = uint64_t(Clz64(mask));
    const uint32_t limit = Load32(base, dev + 44);
    src += (lz << 2) & 0xFFFFFFFCu;
    mask = Shl64(mask, lz);
    const uint64_t run = uint64_t(Clz64(~mask));
    if (run == 0) return false;
    const uint64_t check = ((run << 2) & 0xFFFFFFFCu) + p;
    if (!(uint32_t(check) < limit)) return false;
    p += 4;
    plan.src_lo = std::min(plan.src_lo, src + 4);
    p += run * 4;
    src += run * 4;
    plan.src_hi = std::max(plan.src_hi, src + 4);
    mask = Shl64(mask, run);
  } while (mask != 0);
  plan.dest_hi = p + 4;
  if (!OneView(plan.dest_lo, plan.dest_hi) || !OneView(plan.src_lo, plan.src_hi)) return false;
  if (plan.dest_lo < plan.src_hi && plan.src_lo < plan.dest_hi) return false;
  if (plan.dest_lo < uint64_t(dev) + 48 && uint64_t(dev) + 40 < plan.dest_hi) return false;
  return true;
}

void NativeUpload(PPCContext& ctx, uint8_t* base) {
  uint64_t mask = ctx.r4.u64;
  const uint32_t dev = ctx.r3.u32;
  uint64_t p = Load32(base, dev + 40);
  uint64_t src = uint64_t(ctx.r6.s64 - 4);
  uint64_t r5 = ctx.r5.u64, lz = 0, r9 = 0, r10 = 0;
  do {
    lz = uint64_t(Clz64(mask));
    r9 = Load32(base, dev + 44);
    const uint64_t reg = lz + r5;
    src += (lz << 2) & 0xFFFFFFFCu;
    mask = Shl64(mask, lz);
    const uint64_t run = uint64_t(Clz64(~mask));
    p = uint32_t(p + 4);
    Store32(base, uint32_t(p), uint32_t((((run - 1) << 16) & 0xFFFF0000u) | reg));
    r5 = run + reg;
    for (uint64_t i = 0; i < run; ++i) {
      src = uint32_t(src + 4);
      r10 = Load32(base, uint32_t(src));
      p = uint32_t(p + 4);
      Store32(base, uint32_t(p), uint32_t(r10));
    }
    mask = Shl64(mask, run);
  } while (mask != 0);
  Store32(base, dev + 40, uint32_t(p));
  ctx.r4.u64 = p;
  ctx.r5.u64 = r5;
  ctx.r8.u64 = lz;
  ctx.r9.u64 = r9;
  ctx.r10.u64 = r10;
  ctx.r11.u64 = 0;
  ctx.xer.ca = 1;  // addic. r11,r11,-1 with r11 = 1
  ctx.cr0.compare<int32_t>(0, 0, ctx.xer);
  ctx.cr6.compare<uint64_t>(0, 0, ctx.xer);
  ctx.r12.u64 = uint32_t(ctx.lr);  // __restgprlr_26
  ctx.lr = ctx.r12.u64;
}

std::atomic<uint64_t> g_calls{0}, g_fallbacks{0}, g_mismatches{0};

}  // namespace

REX_HOOK_RAW(sub_82107520) {
  const int32_t mode = Mode_sr_demo_native_constant_upload_c();
  Plan plan{};
  if (mode == 0 || !PlanUpload(base, ctx, plan)) {
    if (mode != 0) g_fallbacks.fetch_add(1, std::memory_order_relaxed);
    __imp__sub_82107520(ctx, base);
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
  __imp__sub_82107520(ctx, base);
  const uint64_t calls = g_calls.fetch_add(1, std::memory_order_relaxed) + 1;
  const bool same =
      std::memcmp(ring.data(), Host(base, uint32_t(plan.dest_lo)), bytes) == 0 &&
      Load32(base, dev + 40) == native_ptr && ctx.r3.u64 == native.r3.u64 &&
      ctx.r4.u64 == native.r4.u64 && ctx.r5.u64 == native.r5.u64 && ctx.r6.u64 == native.r6.u64 &&
      ctx.r7.u64 == native.r7.u64 && ctx.r8.u64 == native.r8.u64 && ctx.r9.u64 == native.r9.u64 &&
      ctx.r10.u64 == native.r10.u64 && ctx.r11.u64 == native.r11.u64 &&
      ctx.r12.u64 == native.r12.u64 && ctx.lr == native.lr && ctx.xer.ca == native.xer.ca &&
      ctx.cr0.eq == native.cr0.eq && ctx.cr0.lt == native.cr0.lt && ctx.cr0.gt == native.cr0.gt &&
      ctx.cr6.eq == native.cr6.eq && ctx.cr6.lt == native.cr6.lt && ctx.cr6.gt == native.cr6.gt;
  if (!same) {
    const uint64_t k = g_mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (k <= 20)
      REXLOG_WARN("[native constants c] MISMATCH #{} dev={:08X} ptr {:08X}/{:08X} r4 {:X}/{:X} "
                  "r5 {:X}/{:X} r8 {:X}/{:X} r9 {:X}/{:X} r10 {:X}/{:X}", k, dev, Load32(base, dev + 40),
                  native_ptr, ctx.r4.u64, native.r4.u64, ctx.r5.u64, native.r5.u64, ctx.r8.u64,
                  native.r8.u64, ctx.r9.u64, native.r9.u64, ctx.r10.u64, native.r10.u64);
  }
  if ((calls & ((1u << 16) - 1)) == 0)
    REXLOG_INFO("[native constants c] shadow calls={} mismatches={} fallbacks={}", calls,
                g_mismatches.load(std::memory_order_relaxed), g_fallbacks.load(std::memory_order_relaxed));
}
