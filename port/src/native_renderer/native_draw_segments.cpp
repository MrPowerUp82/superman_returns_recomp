// Retail port of the demo decomp's port/demo_runtime/native_draw_segments.cpp
// (supermanReturns-demo-decomp). Mechanical changes: cvars -> environment
// variables, demo -> retail addresses (822F54C8->822F5290, 8261D0A8->8261CED0, 0x822F5644u->0x822F540Cu, 0x822F56DCu->0x822F54A4u).
// See docs/native-demo-ports-2026-11-16.md.
//
// Functional (non-matching) native rewrite of sub_822F5290, which submits the
// draw segments of one model instance through sub_8261CED0 (flight profile
// 13/11/2026: 1.49% own samples of FGGameRender). Native bring-up only.
//
// Reconstructed behavior (analysis/demo_image.bin 822F5290..822F5714),
// r3 = instance, r4 = pass (3 = all), r5 = model, r6 = segment table or 0:
//   lists = b(model+108) ? (w(model+12), w(model+8)) : (0, 0)
//   flag = b(0x8297013B) read once into r11; after every call r11 = 0 and the
//   byte is cleared, so only the first submission sees the flag.
//   for pass p < w(inst+212) where r4 == 3 or p == r4, if w(inst + 4*(p+54)):
//     a = (flag == 1 || p == 0); b = (flag == 0 && p != 0)
//     with a segment table: for each of its h(tab+10) entries, the vertex /
//       material indices come from h(tab + h(w(tab+12) + 2*(count*p + i)))
//       (pass 1 re-reads entry 2*i); otherwise one call per entry of list 2.
//     sub_8261CED0(r3..r10 = mesh, material, &pass slot, pass params, 1,
//       w(inst+232), b, flag) with stack arguments at r1+87 (a), +92, +100,
//       +111 (0) in a 272-byte frame.                    (lr 0x822F5644 / 0x822F56DC)
// Compiled code calls this routine; only the ABI is kept for volatile state.
//
// --sr_demo_native_draw_segments=0 original, 1 native (default), 2 shadow (a
// dry run predicts every call's register and stack arguments; the original
// runs with a trace of its sub_8261CED0 calls; both lists are compared).
#include <atomic>
#include <cstring>
#include <vector>

#include <cstdlib>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

REX_EXTERN(__imp__sub_822F5290);
REX_EXTERN(__imp__sub_8261CED0);
REX_EXTERN(sub_8261CED0);


namespace {
// SR_NATIVE_DRAW_SEGMENTS: 0 = translated original, 1 = native (default since the 16/11/2026 shadow run and screenshots), 2 = shadow.
int Mode_sr_demo_native_draw_segments() {
  static const int mode = [] {
    const char* v = std::getenv("SR_NATIVE_DRAW_SEGMENTS");
    if (!v || !*v) return 1;
    const int m = std::atoi(v);
    return m >= 0 && m <= 2 ? m : 1;
  }();
  return mode;
}
}  // namespace

namespace {

constexpr uint32_t kFlagBase = 0x82970000u;  // byte flag at +315
constexpr uint32_t kLrTable = 0x822F540Cu, kLrList = 0x822F54A4u;

inline uint8_t* Host(uint8_t* base, uint32_t a) { return base + a + (a >= 0xE0000000u ? 0x1000u : 0u); }
inline uint32_t W(uint8_t* base, uint32_t a) { uint32_t v; std::memcpy(&v, Host(base, a), 4); return __builtin_bswap32(v); }
inline uint32_t H(uint8_t* base, uint32_t a) { uint16_t v; std::memcpy(&v, Host(base, a), 2); return __builtin_bswap16(v); }
inline uint32_t B(uint8_t* base, uint32_t a) { return *Host(base, a); }
inline void SetW(uint8_t* base, uint32_t a, uint32_t v) { v = __builtin_bswap32(v); std::memcpy(Host(base, a), &v, 4); }
inline void SetB(uint8_t* base, uint32_t a, uint32_t v) { *Host(base, a) = uint8_t(v); }

struct DrawCall {
  uint64_t r3, r4, r5, r6, r7, r8, r9, r10;
  uint32_t s87, s92, s100, s111, lr;
  bool operator==(const DrawCall&) const = default;
};

thread_local std::vector<DrawCall>* t_trace = nullptr;
std::atomic<int> g_trace_active{0};

// Walks the submissions. kCall: perform them through ctx (native mode);
// otherwise append the predicted arguments to `out` (dry run, reads only).
template <bool kCall>
void Walk(PPCContext& ctx, uint8_t* base, std::vector<DrawCall>* out) {
  const uint64_t r31 = ctx.r3.u64, r16 = ctx.r4.u64, r5in = ctx.r5.u64, r19 = ctx.r6.u64;
  const uint32_t r1 = ctx.r1.u32, frame = r1 - 272;
  if constexpr (kCall) {
    SetW(base, frame, r1);  // stwu r1,-272(r1)
    ctx.r1.u64 = frame;
  }
  const uint64_t r23 = 0;
  const bool has_lists = B(base, uint32_t(r5in) + 108) != 0;
  const uint64_t r17 = has_lists ? W(base, uint32_t(r5in) + 12) : 0;
  const uint64_t r15 = has_lists ? W(base, uint32_t(r5in) + 8) : 0;
  if (W(base, uint32_t(r31) + 212) == 0) {
    if constexpr (kCall) ctx.r1.u64 = r1;
    return;
  }
  const uint32_t r18 = kFlagBase;
  uint64_t r11 = B(base, r18 + 315);
  const auto submit = [&](const DrawCall& call) {
    if constexpr (kCall) {
      ctx.r3.u64 = call.r3; ctx.r4.u64 = call.r4; ctx.r5.u64 = call.r5; ctx.r6.u64 = call.r6;
      ctx.r7.u64 = call.r7; ctx.r8.u64 = call.r8; ctx.r9.u64 = call.r9; ctx.r10.u64 = call.r10;
      SetW(base, frame + 100, call.s100);
      SetB(base, frame + 87, call.s87);
      SetB(base, frame + 111, call.s111);
      SetW(base, frame + 92, call.s92);
      ctx.lr = call.lr;
      sub_8261CED0(ctx, base);
      SetB(base, r18 + 315, 0);
    } else {
      out->push_back(call);
    }
    r11 = 0;
  };
  for (uint64_t r24 = 0;; ) {
    if (uint32_t(r16) == 3 || uint32_t(r24) == uint32_t(r16)) {
      const uint64_t r25 = (((r24 + 54) << 2) & 0xFFFFFFFCu) + r31;
      if (W(base, uint32_t(r25)) != 0) {
        const uint32_t r9f = uint32_t(r11) & 0xFF;
        const uint64_t r21 = (r9f == 1 || uint32_t(r24) == 0) ? 1 : 0;
        const uint64_t r20 = r11;
        const uint64_t r22 = (r9f == 0 && uint32_t(r24) != 0) ? 1 : 0;
        if (uint32_t(r19) != 0) {
          uint32_t count = H(base, uint32_t(r19) + 10);
          if (count != 0) {
            const uint64_t r28 = (((r24 + 59) << 2) & 0xFFFFFFFCu) + r31;
            const uint64_t r27 = (((r24 + ((r24 << 1) & 0xFFFFFFFEu)) << 2) & 0xFFFFFFFCu) + r31;
            const uint64_t t = r16 + 14;
            const uint64_t r26 = (((t + ((t << 1) & 0xFFFFFFFEu)) << 2) & 0xFFFFFFFCu) + r31;
            uint64_t r29 = 0, r30 = 0;
            do {
              uint64_t x = uint64_t(int64_t(int32_t(count)) * int64_t(int32_t(r24)));
              const uint32_t r9 = W(base, uint32_t(r19) + 12);
              x = x + r30;
              x = (uint32_t(x) << 1) & 0xFFFFFFFEu;
              x = H(base, uint32_t(x) + r9);
              const uint64_t r10 = H(base, uint32_t(x) + uint32_t(r19));
              uint64_t idx = r10;
              if (uint32_t(r24) == 1) {
                idx = H(base, uint32_t(r29) + r9);
                idx = H(base, uint32_t(idx) + uint32_t(r19));
              }
              const uint32_t r5 = W(base, uint32_t(r31) + 248);
              const uint32_t r14 = W(base, uint32_t(r27) + 160);
              DrawCall call{};
              call.r3 = W(base, ((uint32_t(r10) << 2) & 0xFFFFFFFCu) + uint32_t(r17));
              call.r4 = W(base, ((uint32_t(idx) << 2) & 0xFFFFFFFCu) + r5);
              call.r5 = r25;
              call.r6 = r28;
              call.r7 = 1;
              call.r8 = W(base, uint32_t(r31) + 232);
              call.r9 = r22;
              call.r10 = r20;
              call.s87 = uint32_t(r21) & 0xFF;
              call.s92 = r14;
              call.s100 = uint32_t(r26);
              call.s111 = uint32_t(r23) & 0xFF;
              call.lr = kLrTable;
              submit(call);
              r30 = r30 + 1;
              r29 = r29 + 2;
              count = H(base, uint32_t(r19) + 10);
            } while (uint32_t(r30) < count);
          }
        } else if (uint32_t(r15) != 0) {
          const uint64_t t = r16 + 14;
          const uint64_t r26 = (((r24 + ((r24 << 1) & 0xFFFFFFFEu)) << 2) & 0xFFFFFFFCu) + r31;
          const uint64_t r25b = (((t + ((t << 1) & 0xFFFFFFFEu)) << 2) & 0xFFFFFFFCu) + r31;
          const uint64_t r28 = r31 + 236, r27 = r31 + 216;
          uint64_t r30 = 0, r29 = r15;
          do {
            DrawCall call{};
            const uint32_t s92 = W(base, uint32_t(r26) + 160);
            const uint32_t r4base = W(base, uint32_t(r31) + 248);
            call.r3 = W(base, uint32_t(r30) + uint32_t(r17));
            call.r4 = W(base, r4base + uint32_t(r30));
            call.r5 = r27;
            call.r6 = r28;
            call.r7 = 1;
            call.r8 = W(base, uint32_t(r31) + 232);
            call.r9 = r22;
            call.r10 = r20;
            call.s87 = uint32_t(r21) & 0xFF;
            call.s92 = s92;
            call.s100 = uint32_t(r25b);
            call.s111 = uint32_t(r23) & 0xFF;
            call.lr = kLrList;
            submit(call);
            r29 = r29 - 1;
            r30 = r30 + 4;
          } while (uint32_t(r29) != 0);
        }
      }
    }
    r24 = r24 + 1;
    if (!(uint32_t(r24) < W(base, uint32_t(r31) + 212))) break;
  }
  if constexpr (kCall) {
    ctx.r1.u64 = r1;
    ctx.r12.u64 = uint32_t(ctx.lr);
  }
}

std::atomic<uint64_t> g_calls{0}, g_mismatches{0};

}  // namespace

REX_HOOK_RAW(sub_8261CED0) {
  if (g_trace_active.load(std::memory_order_relaxed) && t_trace &&
      (uint32_t(ctx.lr) == kLrTable || uint32_t(ctx.lr) == kLrList)) {
    const uint32_t r1 = ctx.r1.u32;
    t_trace->push_back({ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64, ctx.r7.u64, ctx.r8.u64, ctx.r9.u64,
                        ctx.r10.u64, B(base, r1 + 87), W(base, r1 + 92), W(base, r1 + 100), B(base, r1 + 111),
                        uint32_t(ctx.lr)});
  }
  __imp__sub_8261CED0(ctx, base);
}

namespace superman_returns::native {
// sub_822F5290 by SR_NATIVE_DRAW_SEGMENTS (called by the guest_cpu_profile wrapper).
void RunDrawSegments(PPCContext& ctx, uint8_t* base) {
  const int32_t mode = Mode_sr_demo_native_draw_segments();
  const auto window = [](uint32_t a) { return a >= 0x7F000000u && a < 0x80000000u; };
  if (mode == 0 || ctx.r1.u32 < 0x1000u || window(ctx.r3.u32) || window(ctx.r5.u32) || window(ctx.r6.u32)) {
    __imp__sub_822F5290(ctx, base);
    return;
  }
  if (mode == 1) {
    const uint64_t lr = ctx.lr;
    Walk<true>(ctx, base, nullptr);
    ctx.lr = lr;
    ctx.r12.u64 = uint32_t(lr);
    return;
  }
  std::vector<DrawCall> predicted, traced;
  PPCContext dry = ctx;
  Walk<false>(dry, base, &predicted);
  std::vector<DrawCall>* outer = t_trace;
  t_trace = &traced;
  g_trace_active.fetch_add(1, std::memory_order_relaxed);
  __imp__sub_822F5290(ctx, base);
  g_trace_active.fetch_sub(1, std::memory_order_relaxed);
  t_trace = outer;
  const uint64_t calls = g_calls.fetch_add(1, std::memory_order_relaxed) + 1;
  if (traced != predicted) {
    const uint64_t k = g_mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (k <= 20) {
      size_t i = 0;
      while (i < traced.size() && i < predicted.size() && traced[i] == predicted[i]) ++i;
      REXLOG_WARN("[native draw segments] MISMATCH #{} calls {}/{} first_diff={}", k, traced.size(),
                  predicted.size(), i);
    }
  }
  if ((calls & ((1u << 16) - 1)) == 0)
    REXLOG_INFO("[native draw segments] shadow calls={} mismatches={}", calls,
                g_mismatches.load(std::memory_order_relaxed));
}
}  // namespace superman_returns::native
