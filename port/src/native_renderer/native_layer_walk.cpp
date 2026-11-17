// Retail port of the 82783B08 walk from the demo decomp's
// port/demo_runtime/native_layer_walks.cpp (supermanReturns-demo-decomp): the
// lock-guarded element walk sub_82783948 (demo 82783B08). See that file and
// docs/native-demo-ports-2026-11-16.md.
//
// Shape (obj = r3, layer = r4, vec = r5, f1): obj+0 -> table of 92-byte layer
// records; each record is a lock (825FC108 enter, 825F6B90 leave, r4 =
// 0x82098FB8) followed at +44 by three 16-byte element vectors {begin, end};
// elements are 8 bytes {object, mask}. obj+4 -> per-layer enable bytes, obj+36
// -> per-layer listener (virtual +20 with phase 2 before, 3 after). The walk
// publishes a stack iterator {vector, cursor, erased byte} at w(0x829A93B4) so
// that callees can erase the current element; object flag bit 1 = ready (else
// virtual +28), bit 64 = "first visited in this walk". Per element, if
// w(obj+16) & mask, virtual +8(f1); then sub_827838D0(obj).
//
// Demo -> retail: return addresses 82783B70..82783CFC -> 827839B0..82783B3C,
// callees 825FC2E0 -> 825FC108, 825F6D48 -> 825F6B90, 82783A90 -> 827838D0,
// lock argument 0x82099498 -> 0x82098FB8 (the only data difference in the
// generated bodies), scene-node vtable 0x82021DA8 -> 0x82021768 (prefetch
// hint only: the +8/+12/+16 slots 82316CA8 / 82316CB0 / 8230F638 sit at
// 0x82021770 in the retail image).
//
// SR_NATIVE_LAYER_WALK: 0 = translated original, 1 = native (default),
// 2 = lockstep shadow (needs port/generated/traced/traced_82783948.inc from
// tools/analysis/trace_functions.py; without it mode 2 runs the original).
// SR_NATIVE_LAYER_WALK_PREFETCH=0 disables the software prefetch;
// SR_NATIVE_LAYER_WALK_SHADOW_FAULT=1 is the shadow's negative control.
#include <cstdlib>

#include "lockstep_shadow.h"
#include "native_guest_rewrites.h"

namespace {

using namespace lockstep;

int EnvInt(const char* name, int fallback) {
  const char* v = std::getenv(name);
  if (!v || !*v) return fallback;
  return std::atoi(v);
}
int LayerWalkMode() {
  static const int mode = [] { const int m = EnvInt("SR_NATIVE_LAYER_WALK", 1); return m >= 0 && m <= 2 ? m : 1; }();
  return mode;
}
bool PrefetchEnabled() {
  static const bool on = EnvInt("SR_NATIVE_LAYER_WALK_PREFETCH", 1) != 0;
  return on;
}

constexpr uint32_t kIterGlobal = 0x829A93B4u;
constexpr uint32_t kLockArg = 0x82098FB8u;
constexpr uint32_t kLockEnter = 0x825FC108u, kLockLeave = 0x825F6B90u;
constexpr uint32_t kAfterWalk = 0x827838D0u;
constexpr uint32_t kNodeVtable = 0x82021768u;

template <class Env>
__attribute__((always_inline)) inline void Ahead(Env& e, uint32_t cur, uint32_t end) {
  if constexpr (Env::kPrefetch) {
    if (!PrefetchEnabled()) return;
    const auto object = [&](uint32_t k) -> uint32_t { return cur + 8 * k < end ? e.R32(cur + 8 * k) : 0u; };
    if (const uint32_t o = object(8))
      for (uint32_t line = 0; line < 320; line += 64) e.Prefetch(o + line);
    const auto node = [&](uint32_t k) -> uint32_t { const uint32_t o = object(k); return o && e.R32(o) == kNodeVtable ? o : 0u; };
    if (const uint32_t o = node(6)) {
      if (const uint32_t t = e.R32(o + 144)) e.Prefetch(t + 12);
      if (const uint32_t m = e.R32(o + 148)) e.Prefetch(m), e.Prefetch(m + 63);
      if (const uint32_t d = e.R32(o + 228)) e.Prefetch(d);
    }
    if (const uint32_t o = node(4))
      if (const uint32_t t = e.R32(o + 144))
        if (const uint32_t link = e.R32(t + 12)) e.Prefetch(link + 12);
    if (const uint32_t o = node(2))
      if (const uint32_t t = e.R32(o + 144))
        if (const uint32_t link = e.R32(t + 12))
          if (const uint32_t n = e.R32(link + 12)) e.Prefetch(n), e.Prefetch(n + 64);
  }
}

template <class Env>
void Walk_82783948(Env& e, PPCContext& ctx) {
  const uint32_t r1 = ctx.r1.u32, frame = r1 - 176;
  const uint64_t lr = ctx.lr;
  SaveGprs(e, ctx, r1, 24);
  e.W64(r1 - 80, ctx.f31.u64);
  e.W32(frame, r1);
  const uint32_t obj = ctx.r3.u32, layer = ctx.r4.u32, sub = ctx.r5.u32;
  const uint64_t f1 = ctx.f1.u64;
  ctx.r1.u64 = frame;
  if (e.R8(e.R32(obj + 4) + layer) != 0) {
    const uint32_t count = e.R32(obj + 12), l4 = layer << 2, listeners = e.R32(obj + 36);
    e.W32(obj + 12, count + 1);
    const uint32_t h = e.R32(listeners + l4);
    if (h) {
      ctx.r3.u64 = h;
      ctx.r4.u64 = 2;
      VCall(e, ctx, h, 20, 0x827839B0, kR3 | kR4);
    }
    const uint32_t lock = e.R32(e.R32(obj)) + layer * 92;
    ctx.r3.u64 = lock;
    ctx.r4.s64 = int32_t(kLockArg);
    e.CallDirect(ctx, sub_825FC108, kLockEnter, 0x827839D0, kR3 | kR4);
    const uint32_t vec = e.R32(e.R32(obj)) + (sub << 4) + layer * 92 + 44;
    if ((int32_t(e.R32(vec + 4) - e.R32(vec)) >> 3) != 0) {
      e.W32(frame + 80, vec);
      uint32_t cur = e.R32(vec);
      e.W8(frame + 88, 0);
      e.W32(frame + 84, cur);
      e.W32(kIterGlobal, frame + 80);
      bool any = false;
      if (cur < e.R32(vec + 4)) {
        for (;;) {
          Ahead(e, cur, e.R32(vec + 4));
          const uint32_t el = e.R32(cur), mask = e.R32(cur + 4);
          if ((e.R32(el + 16) & 1) == 0) {
            ctx.r3.u64 = el;
            VCall(e, ctx, el, 28, 0x82783A68, kR3);
          }
          if (e.R32(obj + 16) & mask) {
            if (!any) e.W32(el + 16, e.R32(el + 16) | 64);
            ctx.f1.u64 = f1;
            ctx.r3.u64 = el;
            VCall(e, ctx, el, 8, 0x82783AA8, kR3 | kF1);
            any = true;
          }
          e.W32(el + 16, e.R32(el + 16) & ~64u);
          if (e.R8(frame + 88) == 0) {
            cur = e.R32(frame + 84) + 8;
            e.W32(frame + 84, cur);
          } else {
            cur = e.R32(frame + 84);
          }
          const uint32_t it = e.R32(frame + 80);
          e.W8(frame + 88, 0);
          if (!(cur < e.R32(it + 4))) break;
        }
      }
      e.W32(kIterGlobal, 0);
    }
    ctx.r3.u64 = lock;
    e.CallDirect(ctx, sub_825F6B90, kLockLeave, 0x82783B0C, kR3);
    const uint32_t h2 = e.R32(e.R32(obj + 36) + l4);
    if (h2) {
      ctx.r3.u64 = h2;
      ctx.r4.u64 = 3;
      VCall(e, ctx, h2, 20, 0x82783B34, kR3 | kR4);
    }
    ctx.r3.u64 = obj;
    e.CallDirect(ctx, sub_827838D0, kAfterWalk, 0x82783B3C, kR3);
  }
  Epilogue(ctx, r1, lr);
}

}  // namespace

#if __has_include("generated/traced/traced_82783948.inc")
#include "generated/traced/traced_82783948.inc"
#define SR_TRACED_82783948 Traced_82783948
#else
#define SR_TRACED_82783948 nullptr
#endif

namespace {
lockstep::Counters g_walk{"layer walk 82783948"};
}  // namespace

namespace superman_returns::native {
// sub_82783948 by SR_NATIVE_LAYER_WALK (called by the guest_cpu_profile wrapper).
void RunLayerWalk(PPCContext& ctx, uint8_t* base) {
  const int mode = LayerWalkMode();
  if (mode == 0 || lockstep::Guarded(ctx)) return __imp__sub_82783948(ctx, base);
  if (mode == 1) return lockstep::RunNative<Walk_82783948<lockstep::DirectEnv>>(ctx, base);
  // SR_NATIVE_LAYER_WALK_SHADOW_FAULT=1: negative control, the shadow's native
  // walk publishes a wrong iterator address, so every walk must mismatch.
  static const bool fault = EnvInt("SR_NATIVE_LAYER_WALK_SHADOW_FAULT", 0) != 0;
  lockstep::Shadow(ctx, base, Walk_82783948<lockstep::ShadowEnv>, SR_TRACED_82783948, __imp__sub_82783948, g_walk,
                   fault ? kIterGlobal : 0u);
}
}  // namespace superman_returns::native
