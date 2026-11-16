// Functional (non-matching) native rewrites of two hot guest routines, ported
// from the demo decomp (supermanReturns-demo-decomp, port/demo_runtime/
// native_light_test.cpp and native_instance_lod.cpp). See those files for the
// reconstructed behavior and exactness notes, and
// docs/native-guest-rewrites-2026-11-16.md for the retail mapping.
//
//  * sub_8231A5F8 (demo 8231A830): light influence test + box test 8231A408.
//    The generated retail body is identical to the demo's, data addresses
//    included (only call-return LR values differ; the rewrite sets none).
//  * sub_823055A8 (demo 823057E0): per-instance visibility/LOD/light mask.
//    Identical except the threshold constants: 0x82913CD0..CF8 -> 0x82913CC0..CE8
//    and 0x82020A28..A30 -> 0x820203E0..3E8; callees 82319340 -> 82319108,
//    8231A830 -> 8231A5F8; return addresses 82305B14 -> 823058DC and
//    82305D6C -> 82305B34. The accessors 82775BD8/82775BF0 it inlines are
//    identical to the demo's 82775D98/82775DB0. guest_cpu_profile.cpp owns the
//    823055A8 symbol and calls RunInstanceLod.
//  * sub_82319108 (demo 82319340): 5-plane sphere-vs-frustum test. Identical
//    except one constant, 0x820219A0 -> 0x82021360. Also owned by the
//    guest_cpu_profile wrapper, which calls RunFrustumCull.
//
// SR_NATIVE_LIGHT_TEST / SR_NATIVE_INSTANCE_LOD / SR_NATIVE_FRUSTUM_CULL: 0 = translated original
// (default), 1 = native, 2 = shadow (original result kept, native compared).
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "native_guest_rewrites.h"

REX_EXTERN(__imp__sub_8231A5F8);
REX_EXTERN(__imp__sub_823055A8);
REX_EXTERN(__imp__sub_82319108);
REX_EXTERN(sub_8231A5F8);
REX_EXTERN(sub_82319108);
REX_EXTERN(sub_8278D258);
REX_EXTERN(__imp__sub_82305B88);

namespace {

int EnvMode(const char* name) {
  const char* v = std::getenv(name);
  if (!v || !*v) return 0;
  const int m = std::atoi(v);
  return m >= 0 && m <= 2 ? m : 0;
}
int LightTestMode() {
  static const int mode = EnvMode("SR_NATIVE_LIGHT_TEST");
  return mode;
}
int InstanceLodMode() {
  static const int mode = EnvMode("SR_NATIVE_INSTANCE_LOD");
  return mode;
}
int ObjectLodMode() {
  static const int mode = EnvMode("SR_NATIVE_OBJECT_LOD");
  return mode;
}
int FrustumCullMode() {
  static const int mode = EnvMode("SR_NATIVE_FRUSTUM_CULL");
  return mode;
}

namespace light_test {

using V = simde__m128;
using VI = simde__m128i;

inline uint32_t Ld32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
inline void St32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + a, &v, 4);
}
inline VI Mask() { return simde_mm_load_si128(reinterpret_cast<const VI*>(VectorMaskL)); }
inline V LdV(const uint8_t* base, uint32_t a) {
  return simde_mm_castsi128_ps(simde_mm_shuffle_epi8(
      simde_mm_load_si128(reinterpret_cast<const VI*>(base + (a & ~0xFu))), Mask()));
}
inline void StV(uint8_t* base, uint32_t a, V v) {
  simde_mm_store_si128(reinterpret_cast<VI*>(base + (a & ~0xFu)),
                       simde_mm_shuffle_epi8(simde_mm_castps_si128(v), Mask()));
}
template <int Imm>
inline V Splat(V v) {
  return simde_mm_castsi128_ps(simde_mm_shuffle_epi32(simde_mm_castps_si128(v), Imm));
}
inline V Perm(V a, V b, V m) {
  return simde_mm_castsi128_ps(rex::ppc::simde_mm_perm_epi8_(
      simde_mm_castps_si128(a), simde_mm_castps_si128(b), simde_mm_castps_si128(m)));
}
// lvsl rN with constant N followed by vspltw ...,0 (shuffle 0xFF).
inline V LvslSplat(uint32_t n) {
  const VI t = simde_mm_load_si128(reinterpret_cast<const VI*>(&VectorShiftTableL[(n & 0xF) * 16]));
  return simde_mm_castsi128_ps(simde_mm_shuffle_epi32(t, 0xFF));
}
template <int Lanes, int Perm_>
inline V Rlimi(V dst, V src) {  // vrlimi128 as translated
  return simde_mm_blend_ps(dst, simde_mm_permute_ps(src, Perm_), Lanes);
}
inline V Dp3(V a, V b) { return simde_mm_dp_ps(a, b, 0xEF); }
inline V Dp4(V a, V b) { return simde_mm_dp_ps(a, b, 0xFF); }
inline V MAdd(V a, V b, V c) { return simde_mm_add_ps(simde_mm_mul_ps(a, b), c); }
inline float Lane(V v, int lane) {
  alignas(16) float f[4];
  simde_mm_store_ps(f, v);
  return f[lane];
}
inline V Splat3Floats(uint8_t* base, uint32_t a, float x, float yzw, bool last_int_zero) {
  // Stores {x, yzw, yzw, yzw or int 0} as guest floats and reloads it as a vector.
  float vals[4] = {x, yzw, yzw, yzw};
  for (int i = 0; i < 4; ++i) {
    uint32_t u;
    std::memcpy(&u, &vals[i], 4);
    if (i == 3 && last_int_zero) u = 0;
    St32(base, a + 4 * i, u);
  }
  return LdV(base, a);
}

constexpr uint32_t kBoxFlags = 0x829702E0;
constexpr uint32_t kLightFlags = 0x82970300;
constexpr uint32_t kLightS5 = 0x829702F0;
constexpr uint32_t kPermMask = 0x829112A0;

float Const(uint8_t* base, uint32_t a) {
  const uint32_t u = Ld32(base, a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

// sub_8231A640 with the box corners passed by value. `scratch` is a 16-byte
// aligned guest address used only for the one-time static initialisation,
// as the original builds those vectors on its stack.
bool BoxTest(PPCContext& ctx, uint8_t* base, V lo, V hi, uint32_t scratch) {
  uint32_t w = Ld32(base, kBoxFlags);
  ctx.fpscr.disableFlushMode();
  const float zero = Const(base, 0x820013B8), one = Const(base, 0x820013C0);
  V s1;
  if (!(w & 1)) {
    w |= 1;
    s1 = Splat3Floats(base, scratch, one, zero, false);
    St32(base, kBoxFlags, w);
    StV(base, 0x829702D0, s1);
  } else {
    s1 = LdV(base, 0x829702D0);
  }
  ctx.fpscr.enableFlushMode();
  const V ext = simde_mm_sub_ps(hi, lo);
  V s2;
  if (!(w & 2)) {
    ctx.fpscr.disableFlushModeUnconditional();
    w |= 2;
    s2 = Splat3Floats(base, scratch, zero, zero, true);
    St32(base, kBoxFlags, w);
    StV(base, 0x829702C0, s2);
  } else {
    s2 = LdV(base, 0x829702C0);
  }
  V s3;
  if (!(w & 4)) {
    ctx.fpscr.disableFlushMode();
    w |= 4;
    s3 = Splat3Floats(base, scratch, one, one, true);
    St32(base, kBoxFlags, w);
    StV(base, 0x829702B0, s3);
  } else {
    s3 = LdV(base, 0x829702B0);
  }
  V s4;
  if (!(w & 8)) {
    ctx.fpscr.enableFlushMode();
    s4 = simde_mm_sub_ps(s3, s2);
    w |= 8;
    St32(base, kBoxFlags, w);
    StV(base, 0x829702A0, s4);
  } else {
    s4 = LdV(base, 0x829702A0);
  }
  ctx.fpscr.enableFlushMode();
  const V sum = simde_mm_add_ps(ext, s4);
  const V mn = simde_mm_min_ps(lo, s2);
  const V mx = simde_mm_max_ps(hi, s3);
  ctx.fpscr.disableFlushModeUnconditional();
  const double s1y = double(Lane(s1, 2));
  ctx.fpscr.enableFlushModeUnconditional();
  const V d = simde_mm_sub_ps(simde_mm_sub_ps(mx, mn), sum);
  ctx.fpscr.disableFlushModeUnconditional();
  if (!(double(Lane(d, 3)) < s1y)) return false;
  ctx.fpscr.disableFlushMode();
  if (!(double(Lane(d, 2)) < double(Lane(s1, 2)))) return false;
  ctx.fpscr.disableFlushMode();
  return double(Lane(lo, 1)) < double(Lane(s1, 3));
}

// Path coverage counters (reported by the shadow mode).
std::atomic<uint64_t> g_cyl_reject{0}, g_box1_reject{0}, g_no_box{0}, g_box2{0}, g_box2_pass{0},
    g_light_loop{0};

uint32_t NativeLightTest(PPCContext& ctx, uint8_t* base) {
  const uint32_t o = ctx.r3.u32, p_addr = ctx.r4.u32, r_addr = ctx.r5.u32;
  const uint32_t l = ctx.r6.u32, q_addr = ctx.r7.u32, e_addr = ctx.r8.u32, m_addr = ctx.r9.u32;
  const uint32_t entry = ctx.r1.u32;
  // Parameter-home stores into the caller's frame (stw rX,404..452(r1-384)).
  St32(base, entry + 36, r_addr);
  St32(base, entry + 44, l);
  St32(base, entry + 20, o);
  St32(base, entry + 52, q_addr);
  St32(base, entry + 60, e_addr);
  St32(base, entry + 68, m_addr);
  const uint32_t frame = entry - 384;  // 16-byte aligned like the original's
  const uint32_t scratch = frame + 80;

  const V p = LdV(base, p_addr);
  V r = LdV(base, r_addr);
  const V light = LdV(base, o + ((l + 36) << 4));
  V rr = Splat<0x00>(light);
  ctx.fpscr.enableFlushMode();
  V v = simde_mm_sub_ps(p, light);
  V axis = LdV(base, o + 656);
  rr = simde_mm_add_ps(r, rr);
  const V along = Dp3(v, axis);
  rr = simde_mm_mul_ps(rr, rr);
  axis = simde_mm_mul_ps(axis, along);
  v = simde_mm_sub_ps(v, axis);
  v = Dp3(v, v);
  if (simde_mm_movemask_ps(simde_mm_cmpgt_ps(rr, v)) == 0) {
    g_cyl_reject.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }
  if (l) g_light_loop.fetch_add(1, std::memory_order_relaxed);

  uint32_t lw = Ld32(base, kLightFlags);
  V s5;
  if (!(lw & 1)) {
    lw |= 1;
    ctx.fpscr.disableFlushModeUnconditional();
    s5 = Splat3Floats(base, scratch, Const(base, 0x820013C0), Const(base, 0x820013B8), false);
    St32(base, kLightFlags, lw);
    StV(base, kLightS5, s5);
  } else {
    s5 = LdV(base, kLightS5);
  }

  r = LdV(base, r_addr);
  V a = LdV(base, o + 672);
  V b = LdV(base, o + 688);
  ctx.fpscr.enableFlushMode();
  a = simde_mm_add_ps(b, a);
  const V c656 = LdV(base, o + 656);
  const V row1 = LdV(base, o + 304), row0 = LdV(base, o + 288), row2 = LdV(base, o + 320);
  const V pmask = LdV(base, kPermMask);
  const V a5 = simde_mm_add_ps(a, c656);
  a = simde_mm_add_ps(a, c656);
  V lo = simde_mm_sub_ps(p, simde_mm_mul_ps(a5, r));
  V hi = simde_mm_add_ps(p, simde_mm_mul_ps(a, r));
  lo = Rlimi<1, 147>(lo, s5);
  {
    const V t12 = Dp4(lo, row1), t8 = Dp4(lo, row0);
    hi = Rlimi<1, 147>(hi, s5);
    const V t6 = Dp4(lo, row2);
    lo = Perm(t8, t12, pmask);
    const V u12 = Dp4(hi, row1), u11 = Dp4(hi, row0), u10 = Dp4(hi, row2);
    lo = Rlimi<2, 228>(lo, t6);
    hi = Perm(u11, u12, pmask);
    hi = Rlimi<2, 228>(hi, u10);
  }
  if (l) {
    const V sel = LvslSplat(12);
    uint32_t k = o + 336;
    for (uint32_t n = l; n; --n, k += 16) {
      const V adj = LdV(base, k);
      ctx.fpscr.enableFlushMode();
      lo = simde_mm_add_ps(simde_mm_mul_ps(lo, Perm(adj, adj, sel)), adj);
      hi = simde_mm_add_ps(simde_mm_mul_ps(hi, Perm(adj, adj, sel)), adj);
    }
  }
  if (!BoxTest(ctx, base, lo, hi, scratch)) {
    g_box1_reject.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }
  if (q_addr == 0) {
    g_no_box.fetch_add(1, std::memory_order_relaxed);
    return 1;
  }
  g_box2.fetch_add(1, std::memory_order_relaxed);

  // Oriented box around Q.
  V q = LdV(base, q_addr);
  const V s5b = LdV(base, kLightS5);
  const V m0 = LdV(base, m_addr);
  q = Rlimi<1, 147>(q, s5b);
  const V sel0 = LvslSplat(0), sel8 = LvslSplat(8), sel12 = LvslSplat(12), sel4 = LvslSplat(4);
  const V m1 = LdV(base, m_addr + 16);
  ctx.fpscr.enableFlushMode();
  V centre = simde_mm_mul_ps(m0, Perm(q, q, sel0));
  const V qw = Perm(q, q, sel12);
  const V m3 = LdV(base, m_addr + 48);
  const V pm = LdV(base, kPermMask);
  const V qy = Perm(q, q, sel4);
  const V m2 = LdV(base, m_addr + 32);
  const V qz = Perm(q, q, sel8);
  const V lrow2 = LdV(base, o + 320);
  centre = MAdd(m1, qy, centre);
  centre = MAdd(m2, qz, centre);
  const V lrow1 = LdV(base, o + 304);
  centre = MAdd(m3, qw, centre);
  const V lrow0 = LdV(base, o + 288);
  centre = Rlimi<1, 228>(centre, q);
  V box = centre;
  const V c3 = Dp4(box, lrow1), c2 = Dp4(box, lrow0);
  const V ev = LdV(base, e_addr), qv = LdV(base, q_addr);
  const V c1 = Dp4(box, lrow2);
  const V ez = simde_mm_sub_ps(Splat<0x55>(ev), Splat<0x55>(qv));
  box = Perm(c2, c3, pm);
  const V ex = simde_mm_sub_ps(Splat<0xFF>(ev), Splat<0xFF>(qv));
  const V ey = simde_mm_sub_ps(Splat<0xAA>(ev), Splat<0xAA>(qv));
  box = Rlimi<2, 228>(box, c1);
  V axes[3] = {simde_mm_mul_ps(m0, ex), simde_mm_mul_ps(m1, ey), simde_mm_mul_ps(m2, ez)};
  for (V& ax : axes) {
    ctx.fpscr.enableFlushMode();
    const V a12 = Dp4(ax, lrow1), a11 = Dp4(ax, lrow0), a10 = Dp4(ax, lrow2);
    ax = Rlimi<2, 228>(Perm(a11, a12, pm), a10);
  }
  if (l) {
    uint32_t k = o + 336;
    for (uint32_t n = l; n; --n, k += 16) {
      const V adj = LdV(base, k);
      ctx.fpscr.enableFlushMode();
      box = simde_mm_add_ps(simde_mm_mul_ps(box, Perm(adj, adj, sel12)), adj);
      const V scale = Perm(adj, adj, sel12);
      for (V& ax : axes) {
        ctx.fpscr.enableFlushMode();
        ax = simde_mm_mul_ps(ax, scale);
      }
    }
  }
  V bmin = box, bmax = box;
  for (const V& ax : axes) {
    ctx.fpscr.enableFlushMode();
    const V lo_c = simde_mm_add_ps(bmin, ax);
    const V hi_c = simde_mm_add_ps(bmax, ax);
    bmin = simde_mm_min_ps(bmin, lo_c);
    bmax = simde_mm_max_ps(bmax, hi_c);
  }
  const bool pass = BoxTest(ctx, base, bmin, bmax, scratch);
  if (pass) g_box2_pass.fetch_add(1, std::memory_order_relaxed);
  return pass ? 1 : 0;
}

std::atomic<uint64_t> g_calls{0}, g_mismatches{0};

}  // namespace light_test

namespace instance_lod {

constexpr uint32_t kFrameSize = 432;
constexpr uint32_t kMaxLods = 5;

inline uint32_t Ld32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
inline void St32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + a, &v, 4);
}
inline float LdF(const uint8_t* base, uint32_t a) {
  const uint32_t u = Ld32(base, a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
inline void StF(uint8_t* base, uint32_t a, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  St32(base, a, u);
}
inline simde__m128i Mask() {
  return simde_mm_load_si128(reinterpret_cast<const simde__m128i*>(VectorMaskL));
}
inline simde__m128 LdV(const uint8_t* base, uint32_t a) {
  return simde_mm_castsi128_ps(simde_mm_shuffle_epi8(
      simde_mm_load_si128(reinterpret_cast<const simde__m128i*>(base + (a & ~0xFu))), Mask()));
}
inline void StV(uint8_t* base, uint32_t a, simde__m128 v) {
  simde_mm_store_si128(reinterpret_cast<simde__m128i*>(base + (a & ~0xFu)),
                       simde_mm_shuffle_epi8(simde_mm_castps_si128(v), Mask()));
}
template <int Imm>
inline simde__m128 Splat(simde__m128 v) {
  return simde_mm_castsi128_ps(simde_mm_shuffle_epi32(simde_mm_castps_si128(v), Imm));
}
inline float Lane(simde__m128 v, int lane) {
  alignas(16) float f[4];
  simde_mm_store_ps(f, v);
  return f[lane];
}
inline int MoveMask(simde__m128 v) { return simde_mm_movemask_ps(v); }
inline simde__m128 NegSub(simde__m128 a, simde__m128 b, simde__m128 c) {  // vnmsubfp
  return simde_mm_xor_ps(simde_mm_sub_ps(simde_mm_mul_ps(a, b), c),
                         simde_mm_castsi128_ps(simde_mm_set1_epi32(int(0x80000000))));
}
inline simde__m128 MAdd(simde__m128 a, simde__m128 b, simde__m128 c) {  // vmaddfp
  return simde_mm_add_ps(simde_mm_mul_ps(a, b), c);
}
inline float MulS(float a, float b) { return float(double(a) * double(b)); }  // fmuls

// Returns false when the layout is outside what this rewrite handles; the
// caller then runs the original.
bool NativeInstanceLod(PPCContext& ctx, uint8_t* base) {
  const uint32_t o = ctx.r3.u32;
  const uint32_t lods = Ld32(base, o + 212);
  // lods == 0 would make the original read a stale stack word as the cursor.
  if (lods == 0 || lods > kMaxLods) return false;

  const uint32_t entry_r1 = ctx.r1.u32;
  const uint64_t entry_lr = ctx.lr;
  const uint32_t f = entry_r1 - kFrameSize;  // guest frame (stwu r1,-432(r1))
  St32(base, f, entry_r1);
  const uint32_t b = Ld32(base, ctx.r4.u32 + 68);

  // Locals in the guest frame that callees read through pointers.
  ctx.fpscr.disableFlushMode();
  const float radius = LdF(base, b + 52);
  StF(base, f + 112, radius);
  St32(base, f + 116, 0); St32(base, f + 120, 0); St32(base, f + 124, 0);
  StF(base, f + 92, LdF(base, 0x820013C0));
  StV(base, f + 112, Splat<0xFF>(LdV(base, f + 112)));
  StF(base, f + 80, LdF(base, b + 40));
  StF(base, f + 84, LdF(base, b + 44));
  StF(base, f + 88, LdF(base, b + 48));
  StF(base, f + 96, LdF(base, b + 16));
  StF(base, f + 100, LdF(base, b + 20));
  StF(base, f + 104, LdF(base, b + 24));
  St32(base, f + 108, 0);
  StV(base, f + 192, LdV(base, f + 96));
  StF(base, f + 96, LdF(base, b + 28));
  StF(base, f + 100, LdF(base, b + 32));
  StF(base, f + 104, LdF(base, b + 36));
  St32(base, f + 108, 0);
  StV(base, f + 176, LdV(base, f + 96));
  const simde__m128 sphere = LdV(base, f + 80);  // v126

  uint32_t cursor[kMaxLods] = {};
  for (uint32_t i = 0; i < lods; ++i) {
    cursor[i] = Ld32(base, o + 236 + 4 * i);
    St32(base, o + 216 + 4 * i, 0);
    St32(base, f + 80 + 4 * i, cursor[i]);
  }

  // 82775DB0(G, 0): camera = [[[G+96]]] + 112; position at +48.
  const uint32_t g = Ld32(base, 0x829A9384);
  const uint32_t cam = Ld32(base, Ld32(base, Ld32(base, g + 96))) + 112;
  uint32_t r3 = cam;

  const uint32_t count = Ld32(base, o + 152);
  const uint32_t light_owner = Ld32(base, 0x82970190);
  const uint32_t lights = Ld32(base, light_owner + 720);

  const float f12 = MulS(LdF(base, 0x82913CCC), LdF(base, 0x82913CE0));
  const float t128 = MulS(LdF(base, 0x82913CC0), LdF(base, 0x820203E0));
  StF(base, f + 128, t128);
  const float f13 = MulS(LdF(base, 0x82913CD0), LdF(base, 0x82913CE8));
  const float t144 = MulS(f12, LdF(base, 0x820203E4));
  StF(base, f + 144, t144);
  const float h = LdF(base, 0x820203E8);
  ctx.fpscr.enableFlushModeUnconditional();
  const simde__m128 s128 = simde_mm_set1_ps(t128);
  const simde__m128 v123 = simde_mm_mul_ps(s128, s128);
  ctx.fpscr.disableFlushModeUnconditional();
  const float t160 = MulS(f13, h);
  StF(base, f + 160, t160);
  const simde__m128 v125 = LdV(base, cam + 48);
  const simde__m128 s144 = simde_mm_set1_ps(t144);
  ctx.fpscr.enableFlushModeUnconditional();
  const simde__m128 v122 = simde_mm_mul_ps(s144, s144);
  const simde__m128 v124 = simde_mm_set1_ps(t160);

  const uint32_t table_flag = Ld32(base, 0x829701A8);
  if (base[0x82970260] == 0 && table_flag == 0) base[0x82970260] = 1;
  const uint32_t table_index = Ld32(base, o + 64);
  uint32_t entry = 0x8296FEA0;
  if (table_flag != 0 && table_index <= 31 &&
      Ld32(base, entry + 16 + table_index * 20) != 0xFFFFFFFFu)
    entry += table_index * 20;
  const uint32_t light_mask = Ld32(base, entry + 16);

  ctx.fpscr.disableFlushMode();
  const float zero = LdF(base, 0x820013B8);
  StF(base, f + 160, zero);
  St32(base, f + 164, 0); St32(base, f + 168, 0); St32(base, f + 172, 0);
  StF(base, f + 144, zero);
  St32(base, f + 148, 0); St32(base, f + 152, 0); St32(base, f + 156, 0);
  simde__m128 v127 = Splat<0xFF>(LdV(base, f + 160));
  StV(base, f + 160, Splat<0xFF>(LdV(base, f + 144)));

  uint32_t flags = 0;  // r25
  for (uint32_t n = 0; n < count; ++n) {
    const uint32_t m = Ld32(base, o + 148) + n * 64;  // r31
    const uint32_t planes = Ld32(base, 0x82970154);
    ctx.fpscr.enableFlushMode();
    simde__m128 c = simde_mm_mul_ps(LdV(base, m), Splat<0xFF>(sphere));
    c = MAdd(LdV(base, m + 16), Splat<0xAA>(sphere), c);
    c = MAdd(LdV(base, m + 32), Splat<0x55>(sphere), c);
    c = MAdd(LdV(base, m + 48), Splat<0x00>(sphere), c);
    StV(base, f + 96, c);

    ctx.r1.u64 = f;
    ctx.r3.u64 = planes;
    ctx.r4.u64 = f + 96;
    ctx.r5.u64 = f + 112;
    ctx.lr = 0x823058DC;
    sub_82319108(ctx, base);
    r3 = ctx.r3.u32;
    if (r3 & 0xFF) {
      flags |= 1;
      const uint32_t lod_count = Ld32(base, o + 212);
      uint32_t lod = 0;
      ctx.fpscr.enableFlushMode();
      simde__m128 d = simde_mm_sub_ps(LdV(base, f + 96), v125);
      if (lod_count > 1) {
        d = simde_mm_dp_ps(d, d, 0xEF);  // squared distance, all lanes
        const simde__m128 one = simde_mm_cvtepi32_ps(simde_mm_set1_epi32(1));
        const simde__m128 half =
            simde_mm_mul_ps(one, simde_mm_castsi128_ps(simde_mm_set1_epi32(int(0x3F000000))));
        simde__m128 y = simde_mm_div_ps(simde_mm_set1_ps(1), simde_mm_sqrt_ps(d));
        v127 = d;
        const simde__m128 is_zero = simde_mm_cmpeq_ps(simde_mm_setzero_ps(), d);
        simde__m128 t = simde_mm_mul_ps(d, simde_mm_mul_ps(y, y));
        y = simde_mm_add_ps(y, simde_mm_mul_ps(simde_mm_mul_ps(y, half), simde_mm_sub_ps(one, t)));
        t = simde_mm_mul_ps(d, simde_mm_mul_ps(y, y));
        const simde__m128 ry = simde_mm_mul_ps(simde_mm_mul_ps(y, half), simde_mm_sub_ps(one, t));
        const simde__m128 rad = LdV(base, f + 112);
        y = simde_mm_add_ps(y, ry);
        const simde__m128 dist = simde_mm_mul_ps(d, y);
        const simde__m128 safe = simde_mm_or_ps(simde_mm_andnot_ps(is_zero, dist),
                                                simde_mm_and_ps(is_zero, simde_mm_setzero_ps()));
        simde__m128 r = simde_mm_div_ps(simde_mm_set1_ps(1), safe);
        simde__m128 e = NegSub(r, safe, one);
        e = MAdd(r, e, r);
        simde__m128 inv = NegSub(e, safe, one);
        inv = MAdd(e, inv, e);
        const simde__m128 size = simde_mm_mul_ps(inv, rad);  // radius / distance
        bool check_near = true;
        if (lod_count > 2) {
          if (MoveMask(simde_mm_cmpgt_ps(d, v122)) != 0 ||
              MoveMask(simde_mm_cmpgt_ps(v124, size)) == 0xF) {
            lod = 2;
            check_near = false;
          }
        }
        if (check_near) {
          ctx.fpscr.enableFlushMode();
          if (MoveMask(simde_mm_cmpgt_ps(v127, v123)) != 0) lod = 1;
        }
      }
      r3 = lod;
      const uint32_t dst = cursor[lod];
      if (Ld32(base, o + 232) == 2) {
        StV(base, f + 160, v127);
        ctx.fpscr.disableFlushMode();
        const float d2 = LdF(base, f + 160);
        cursor[lod] = dst + 32;
        std::memmove(base + dst, base + m + 48, 16);
        StF(base, dst + 12, d2);
        std::memmove(base + dst + 16, base + Ld32(base, o + 228) + n * 16, 16);
      } else {
        cursor[lod] = dst + 64;
        std::memmove(base + dst, base + m, 64);
      }
      St32(base, f + 80 + 4 * lod, cursor[lod]);
      const uint32_t counter = o + (lod + 54) * 4;
      St32(base, counter, Ld32(base, counter) + 1);
    }

    uint8_t used = 0;  // r27
    for (int32_t l = int32_t(lights) - 1; l >= 0; --l) {
      const uint32_t bit = l < 32 ? (1u << l) : 0u;
      if (!(bit & light_mask)) continue;
      std::memmove(base + f + 144, base + f + 96, 16);
      ctx.r1.u64 = f;
      ctx.r3.u64 = light_owner;
      ctx.r4.u64 = f + 144;
      ctx.r5.u64 = f + 112;
      ctx.r6.u64 = uint64_t(uint32_t(l));
      ctx.r7.u64 = f + 192;
      ctx.r8.u64 = f + 176;
      ctx.r9.u64 = Ld32(base, o + 148) + n * 64;
      ctx.lr = 0x82305B34;
      sub_8231A5F8(ctx, base);
      r3 = ctx.r3.u32;
      if (!(r3 & 0xFF)) break;
      used = uint8_t(used | bit);
      flags |= l < 31 ? (2u << l) : 0u;
    }
    base[Ld32(base, o + 204) + n] = used;
  }
  St32(base, o + 48, flags);

  ctx.r1.u64 = entry_r1;
  ctx.lr = entry_lr;
  ctx.r3.u64 = r3;
  return true;
}

// ---- shadow comparison ----------------------------------------------------

struct Outputs {
  uint32_t r3 = 0, flags = 0;
  std::array<uint32_t, kMaxLods> counters{};
  std::vector<uint8_t> buckets, light_bytes;
};

bool Capture(uint8_t* base, uint32_t o, uint32_t r3, const std::array<uint32_t, kMaxLods>& start,
             Outputs& out) {
  const uint32_t lods = Ld32(base, o + 212);
  if (lods > kMaxLods) return false;
  const uint32_t stride = Ld32(base, o + 232) == 2 ? 32 : 64;
  out.r3 = r3;
  out.flags = Ld32(base, o + 48);
  out.buckets.clear();
  for (uint32_t i = 0; i < lods; ++i) {
    out.counters[i] = Ld32(base, o + 216 + 4 * i);
    const uint32_t bytes = out.counters[i] * stride;
    out.buckets.insert(out.buckets.end(), base + start[i], base + start[i] + bytes);
  }
  const uint32_t n = Ld32(base, o + 152), bytes_at = Ld32(base, o + 204);
  out.light_bytes.assign(base + bytes_at, base + bytes_at + n);
  return true;
}

void Restore(uint8_t* base, uint32_t o, const std::array<uint32_t, kMaxLods>& start,
             const Outputs& in) {
  const uint32_t lods = Ld32(base, o + 212);
  const uint32_t stride = Ld32(base, o + 232) == 2 ? 32 : 64;
  St32(base, o + 48, in.flags);
  size_t offset = 0;
  for (uint32_t i = 0; i < lods; ++i) {
    St32(base, o + 216 + 4 * i, in.counters[i]);
    const uint32_t bytes = in.counters[i] * stride;
    std::memcpy(base + start[i], in.buckets.data() + offset, bytes);
    offset += bytes;
  }
  std::memcpy(base + Ld32(base, o + 204), in.light_bytes.data(), in.light_bytes.size());
}

std::atomic<uint64_t> g_calls{0}, g_mismatches{0}, g_fallbacks{0};

}  // namespace instance_lod

namespace frustum_cull {

constexpr uint32_t kStaticVector = 0x82970270;
constexpr uint32_t kStaticFlag = 0x82970280;
constexpr uint32_t kConstX = 0x82021360;
constexpr uint32_t kConstY = 0x820013B8;

inline uint32_t Load32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
inline void Store32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + a, &v, 4);
}
inline simde__m128 LoadVector(const uint8_t* base, uint32_t a) {
  const simde__m128i raw = simde_mm_load_si128(reinterpret_cast<const simde__m128i*>(base + (a & ~0xFu)));
  return simde_mm_castsi128_ps(
      simde_mm_shuffle_epi8(raw, simde_mm_load_si128(reinterpret_cast<const simde__m128i*>(VectorMaskL))));
}
inline float Lane(simde__m128 v, int lane) {
  alignas(16) float f[4];
  simde_mm_store_ps(f, v);
  return f[lane];
}

uint32_t NativeCull(PPCContext& ctx, uint8_t* base) {
  uint32_t flag = Load32(base, kStaticFlag);
  if (!(flag & 1)) {
    Store32(base, kStaticFlag, flag | 1);
    Store32(base, kStaticVector + 0, Load32(base, kConstX));
    Store32(base, kStaticVector + 4, Load32(base, kConstY));
    Store32(base, kStaticVector + 8, 0);
    Store32(base, kStaticVector + 12, 0);
  }
  const simde__m128 k = LoadVector(base, kStaticVector);
  const simde__m128 point = LoadVector(base, ctx.r4.u32);
  const simde__m128 bias = LoadVector(base, ctx.r5.u32);
  ctx.fpscr.enableFlushMode();
  simde__m128 m = simde_mm_shuffle_ps(k, k, 0xFF);  // splat guest element 0
  uint32_t plane = ctx.r3.u32;
  for (int i = 0; i < 5; ++i, plane += 16) {
    simde__m128 d = simde_mm_dp_ps(LoadVector(base, plane), point, 0xFF);
    d = simde_mm_add_ps(d, bias);
    m = simde_mm_min_ps(m, d);
  }
  ctx.fpscr.disableFlushModeUnconditional();
  const double lhs = double(Lane(m, 3));   // guest element 0 of the minimum
  const double rhs = double(Lane(k, 2));   // guest element 1 of K
  return lhs < rhs ? 0u : 1u;
}

std::atomic<uint64_t> g_calls{0}, g_mismatches{0};

}  // namespace frustum_cull

namespace object_lod {
// New functional rewrite (no demo counterpart): sub_82305B88, the
// single-object sibling of 823055A8. Reconstructed from the retail generated
// code (port/generated/default, sub_82305B88). Ghidra 12.1.3 + XEXLoaderWV
// could not decompile it: its PowerPC:BE:64:A2ALT language lacks VMX128, so
// the vector body is "bad instruction data" there. o = r3:
//   owner = [[[o+88]+12]+12] (early return, r3 = o, if a link is 0)
//   if [o+220] == 0: [o+220] = 8278D258(1856, 0, [0x8296D894], 2, 16, 0, 0, 0)
//                    initialised to {vtable 0x820217A8, 0, 2, 0} (or 0)
//   q = [o+220]; q+8 = q+4 = 0; q->vt[28](q, [o+8]); q->vt[48](q, o+128)
//   [o+16] &= ~0x80; K = static splat of [0x820013C0] (0x82970360, set once
//   under bit 0 of 0x82970370); b = [owner+68]; f+144 = splat(b+52)
//   p = {b+40, b+44, b+48, K} -> f+176; c = M(o+128..o+191) * p with the w
//   lane kept from p -> f+112
//   visible = 82319108([0x829A9390], f+112, f+144); !visible -> [o+16] |= 0x80
//   flags = visible; light mask from the 0x8296FEA0 table as in 823055A8;
//   for l = lights-1..0 in the mask: 8231A5F8(light owner, copy of c at f+128,
//   f+144, l, 0, 0, 0), stop at the first rejection, else flags |= 2 << l
//   [o+48] = flags; o->vt[88](o, copy of c at f+160); [o+16] |= 0x200
// The guest calls keep their arguments, a 256-byte guest frame and the
// original return addresses in LR; r3 is the last call's result (or o on the
// early return), as in the original. Non-volatile registers are never
// modified, so the __savegprlr_25 save area is not reproduced (dead).
using light_test::Ld32;
using light_test::LdV;
using light_test::LvslSplat;
using light_test::Perm;
using light_test::St32;
using light_test::StV;
using V = simde__m128;

constexpr uint32_t kFrame = 256;

// REX_CALL_INDIRECT_FUNC from the generated pch: the dispatch table follows
// the image (base 0x82000000, size 0x9E0000), code 0x820F0000 + 0x81F5B8 plus
// a 0x10000 thunk reserve; unregistered targets resolve through the runtime.
inline void CallIndirect(PPCContext& ctx, uint8_t* base, uint32_t target, uint32_t lr) {
  ctx.r11.u64 = target;
  ctx.ctr.u64 = target;
  ctx.lr = lr;
  PPCFunc* fn = nullptr;
  if (uint32_t(target - 0x820F0000u) < 0x81F5B8u + 0x10000u)
    fn = *reinterpret_cast<PPCFunc**>(base + 0x82000000ull + 0x9E0000ull +
                                      uint64_t(target - 0x820F0000u) * 2);
  if (!fn) {
    ctx.last_indirect_target = target;
    fn = rex::runtime::ResolveIndirectFunction(target);
  }
  fn(ctx, base);
}
inline void CopyF(uint8_t* base, uint32_t dst, uint32_t src) {  // lfs + stfs
  uint32_t u = Ld32(base, src);
  float f;
  std::memcpy(&f, &u, 4);
  const float g = float(double(f));
  std::memcpy(&u, &g, 4);
  St32(base, dst, u);
}
inline V Blend1(V dst, V src) {  // vrlimi128 dst,src,1,0 as translated
  return simde_mm_blend_ps(dst, simde_mm_permute_ps(src, 228), 1);
}

// Transform of the bounds sphere: writes f+128..140 scratch, f+144 (radius
// splat), f+112 (centre) and f+176 (local point), in the original's order.
// K must already be initialised.
void Transform(PPCContext& ctx, uint8_t* base, uint32_t o, uint32_t b, uint32_t f, V k) {
  ctx.fpscr.disableFlushMode();
  CopyF(base, f + 128, b + 52);
  const V m0 = LdV(base, o + 128);
  St32(base, f + 132, 0); St32(base, f + 136, 0); St32(base, f + 140, 0);
  const V sx = LvslSplat(0), sy = LvslSplat(4), sz = LvslSplat(8), sw = LvslSplat(12);
  const V m2 = LdV(base, o + 160);
  StV(base, f + 144, light_test::Splat<0xFF>(LdV(base, f + 128)));
  CopyF(base, f + 128, b + 40);
  CopyF(base, f + 132, b + 44);
  CopyF(base, f + 136, b + 48);
  CopyF(base, f + 140, b + 52);
  const V m1 = LdV(base, o + 144);
  V p = LdV(base, f + 128);
  StV(base, f + 112, p);
  p = Blend1(p, k);
  const V px = Perm(p, p, sx), py = Perm(p, p, sy), pz = Perm(p, p, sz);
  StV(base, f + 112, p);
  StV(base, f + 176, p);
  ctx.fpscr.enableFlushModeUnconditional();
  V c = simde_mm_mul_ps(m0, px);
  c = simde_mm_add_ps(simde_mm_mul_ps(m1, py), c);
  const V pw = Perm(p, p, sw);
  c = simde_mm_add_ps(simde_mm_mul_ps(m2, pz), c);
  const V m3 = LdV(base, o + 176);
  c = simde_mm_add_ps(simde_mm_mul_ps(m3, pw), c);
  c = Blend1(c, p);
  StV(base, f + 112, c);
}

uint32_t LightMask(uint8_t* base, uint32_t o) {
  const uint32_t table_flag = Ld32(base, 0x829701A8u);
  if (base[0x82970260u] == 0 && table_flag == 0) base[0x82970260u] = 1;
  const uint32_t table_index = Ld32(base, o + 64);
  uint32_t entry = 0x8296FEA0u;
  if (table_flag != 0 && table_index <= 31 &&
      Ld32(base, entry + 16 + table_index * 20) != 0xFFFFFFFFu)
    entry += table_index * 20;
  return Ld32(base, entry + 16);
}

void NativeObjectLod(PPCContext& ctx, uint8_t* base) {
  const uint32_t o = ctx.r3.u32;
  const uint32_t entry_r1 = ctx.r1.u32;
  const uint64_t entry_lr = ctx.lr;
  const uint32_t f = entry_r1 - kFrame;
  St32(base, f, entry_r1);            // stwu r1,-256(r1)
  const uint32_t t = Ld32(base, Ld32(base, o + 88) + 12);
  St32(base, entry_r1 + 20, o);       // stw r29,276(r1)
  if (t == 0) return;
  const uint32_t owner = Ld32(base, t + 12);  // r28
  if (owner == 0) return;
  ctx.r1.u64 = f;

  if (Ld32(base, o + 220) == 0) {
    St32(base, f + 84, 0);
    ctx.r10.u64 = 0; ctx.r9.u64 = 0; ctx.r8.u64 = 0; ctx.r7.u64 = 16; ctx.r6.u64 = 2;
    ctx.r11.u64 = 0x82970000u;
    ctx.r5.u64 = Ld32(base, 0x8296D894u);
    ctx.r4.u64 = 0; ctx.r3.u64 = 1856;
    ctx.lr = 0x82305BF8;
    sub_8278D258(ctx, base);
    const uint32_t q = ctx.r3.u32;
    if (q != 0) {
      St32(base, q + 4, 0);
      St32(base, q + 8, 2);
      St32(base, q + 12, 0);
      St32(base, q, 0x820217A8u);
    }
    St32(base, o + 220, q);
  }
  {
    const uint32_t q = Ld32(base, o + 220);
    ctx.r3.u64 = q;
    ctx.r4.u64 = Ld32(base, o + 8);
    St32(base, q + 8, 0);
    St32(base, q + 4, 0);
    CallIndirect(ctx, base, Ld32(base, Ld32(base, q) + 28), 0x82305C48);
  }
  {
    const uint32_t q = Ld32(base, o + 220);
    ctx.r3.u64 = q;
    ctx.r4.u64 = o + 128;
    CallIndirect(ctx, base, Ld32(base, Ld32(base, q) + 48), 0x82305C64);
  }

  uint32_t flags = 1;  // r27
  St32(base, f + 96, 1);
  St32(base, o + 16, Ld32(base, o + 16) & ~0x80u);
  const uint32_t b = Ld32(base, owner + 68);
  const uint32_t kflag = Ld32(base, 0x82970370u);
  V k;
  if (!(kflag & 1)) {
    ctx.fpscr.disableFlushMode();
    CopyF(base, f + 128, 0x820013C0u);
    St32(base, f + 132, 0); St32(base, f + 136, 0); St32(base, f + 140, 0);
    St32(base, 0x82970370u, kflag | 1);
    k = light_test::Splat<0xFF>(LdV(base, f + 128));
    StV(base, 0x82970360u, k);
  } else {
    k = LdV(base, 0x82970360u);
  }
  Transform(ctx, base, o, b, f, k);

  ctx.r3.u64 = Ld32(base, 0x829A9390u);
  ctx.r4.u64 = f + 112;
  ctx.r5.u64 = f + 144;
  ctx.lr = 0x82305DD4;
  sub_82319108(ctx, base);
  if ((ctx.r3.u32 & 0xFF) == 0) {
    St32(base, o + 16, Ld32(base, o + 16) | 0x80u);
    flags = 0;
  }

  const uint32_t light_owner = Ld32(base, 0x82970190u);  // r28
  const uint32_t lights = Ld32(base, light_owner + 720);
  const uint32_t light_mask = LightMask(base, o);
  for (int32_t l = int32_t(lights) - 1; l >= 0; --l) {
    const uint32_t bit = l < 32 ? (1u << l) : 0u;
    if (!(bit & light_mask)) continue;
    std::memmove(base + f + 128, base + f + 112, 16);
    ctx.r1.u64 = f;
    ctx.r3.u64 = light_owner;
    ctx.r4.u64 = f + 128;
    ctx.r5.u64 = f + 144;
    ctx.r6.u64 = uint64_t(uint32_t(l));
    ctx.r7.u64 = 0; ctx.r8.u64 = 0; ctx.r9.u64 = 0;
    ctx.lr = 0x82305EB0;
    sub_8231A5F8(ctx, base);
    if (!(ctx.r3.u32 & 0xFF)) break;
    flags |= l < 31 ? (2u << l) : 0u;
  }

  St32(base, o + 48, flags);
  std::memmove(base + f + 160, base + f + 112, 16);
  ctx.r1.u64 = f;
  ctx.r3.u64 = o;
  ctx.r4.u64 = f + 160;
  CallIndirect(ctx, base, Ld32(base, Ld32(base, o) + 88), 0x82305EFC);
  St32(base, o + 16, Ld32(base, o + 16) | 0x200u);
  ctx.r1.u64 = entry_r1;
  ctx.lr = entry_lr;
}

std::atomic<uint64_t> g_calls{0}, g_mismatches{0}, g_hidden{0}, g_lit{0};

// Shadow: the original runs and its effects are kept. Its dead frame still
// holds the centre (f+112), radius splat (f+144) and local point (f+176); the
// native transform is recomputed from the same inputs, which are read after
// the original returns (o+128.. is written by the vt[48] call), and the
// visibility and light flags are recomputed with the native leaf rewrites.
void Shadow(PPCContext& ctx, uint8_t* base) {
  const uint32_t o = ctx.r3.u32, r1 = ctx.r1.u32;
  const uint64_t lr = ctx.lr;
  __imp__sub_82305B88(ctx, base);
  const uint32_t t = Ld32(base, Ld32(base, o + 88) + 12);
  if (t == 0 || Ld32(base, t + 12) == 0) return;
  const uint32_t f = r1 - kFrame;
  alignas(16) uint8_t original[48];
  std::memcpy(original, base + f + 112, 16);
  std::memcpy(original + 16, base + f + 144, 16);
  std::memcpy(original + 32, base + f + 176, 16);
  const uint32_t flags_original = Ld32(base, o + 48);
  const uint32_t o16 = Ld32(base, o + 16);
  // Saved so the caller sees exactly the original's exit state.
  const uint64_t r3 = ctx.r3.u64, r4 = ctx.r4.u64, r5 = ctx.r5.u64, r6 = ctx.r6.u64,
                 r7 = ctx.r7.u64, r8 = ctx.r8.u64, r9 = ctx.r9.u64;

  const uint32_t b = Ld32(base, Ld32(base, t + 12) + 68);
  Transform(ctx, base, o, b, f, LdV(base, 0x82970360u));
  const bool same_vectors = std::memcmp(original, base + f + 112, 16) == 0 &&
                            std::memcmp(original + 16, base + f + 144, 16) == 0 &&
                            std::memcmp(original + 32, base + f + 176, 16) == 0;
  ctx.r1.u64 = f;
  ctx.r3.u64 = Ld32(base, 0x829A9390u);
  ctx.r4.u64 = f + 112;
  ctx.r5.u64 = f + 144;
  uint32_t flags = frustum_cull::NativeCull(ctx, base) & 0xFF ? 1u : 0u;
  if (!flags) g_hidden.fetch_add(1, std::memory_order_relaxed);
  const uint32_t light_owner = Ld32(base, 0x82970190u);
  const uint32_t lights = Ld32(base, light_owner + 720);
  const uint32_t light_mask = LightMask(base, o);
  for (int32_t l = int32_t(lights) - 1; l >= 0; --l) {
    const uint32_t bit = l < 32 ? (1u << l) : 0u;
    if (!(bit & light_mask)) continue;
    std::memmove(base + f + 128, base + f + 112, 16);
    ctx.r1.u64 = f;
    ctx.r3.u64 = light_owner;
    ctx.r4.u64 = f + 128;
    ctx.r5.u64 = f + 144;
    ctx.r6.u64 = uint64_t(uint32_t(l));
    ctx.r7.u64 = 0; ctx.r8.u64 = 0; ctx.r9.u64 = 0;
    if (!(light_test::NativeLightTest(ctx, base) & 0xFF)) break;
    flags |= l < 31 ? (2u << l) : 0u;
  }
  if (flags > 1) g_lit.fetch_add(1, std::memory_order_relaxed);
  const bool same_hidden = ((o16 & 0x80u) != 0) == ((flags & 1u) == 0);
  ctx.r3.u64 = r3; ctx.r4.u64 = r4; ctx.r5.u64 = r5; ctx.r6.u64 = r6;
  ctx.r7.u64 = r7; ctx.r8.u64 = r8; ctx.r9.u64 = r9;
  ctx.r1.u64 = r1;
  ctx.lr = lr;
  const uint64_t calls = g_calls.fetch_add(1, std::memory_order_relaxed) + 1;
  if (!same_vectors || flags != flags_original || !same_hidden) {
    const uint64_t n = g_mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 20)
      REXLOG_WARN("[native object lod] MISMATCH #{} obj={:08X} vectors_equal={} flags {:08X}/{:08X} "
                  "hidden_bit_equal={}", n, o, same_vectors, flags_original, flags, same_hidden);
  }
  if ((calls & ((1u << 16) - 1)) == 0)
    REXLOG_INFO("[native object lod] shadow calls={} mismatches={} hidden={} lit={}", calls,
                g_mismatches.load(std::memory_order_relaxed), g_hidden.load(), g_lit.load());
}
}  // namespace object_lod

}  // namespace

REX_HOOK_RAW(sub_8231A5F8) {
  using namespace light_test;
  const int32_t mode = LightTestMode();
  if (mode == 1) {
    ctx.r3.u64 = NativeLightTest(ctx, base);
    return;
  }
  if (mode != 2) {
    __imp__sub_8231A5F8(ctx, base);
    return;
  }
  const uint32_t r3 = ctx.r3.u32, r4 = ctx.r4.u32, r5 = ctx.r5.u32, r6 = ctx.r6.u32,
                 r7 = ctx.r7.u32, r8 = ctx.r8.u32, r9 = ctx.r9.u32, r1 = ctx.r1.u32;
  const uint64_t lr = ctx.lr;
  const uint32_t native = NativeLightTest(ctx, base);
  ctx.r3.u64 = r3; ctx.r4.u64 = r4; ctx.r5.u64 = r5; ctx.r6.u64 = r6;
  ctx.r7.u64 = r7; ctx.r8.u64 = r8; ctx.r9.u64 = r9; ctx.r1.u64 = r1; ctx.lr = lr;
  __imp__sub_8231A5F8(ctx, base);
  const uint64_t calls = g_calls.fetch_add(1, std::memory_order_relaxed) + 1;
  if ((ctx.r3.u32 & 0xFF) != native) {
    const uint64_t n = g_mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 20)
      REXLOG_WARN("[native light test] MISMATCH #{} owner={:08X} light={} box={:08X} "
                  "original={} native={}", n, r3, r6, r7, ctx.r3.u32, native);
  }
  if ((calls & ((1u << 18) - 1)) == 0)
    REXLOG_INFO("[native light test] shadow calls={} mismatches={} cylinder_reject={} "
                "box1_reject={} no_box_accept={} box2={} box2_accept={} light_index_nonzero={}",
                calls, g_mismatches.load(std::memory_order_relaxed), g_cyl_reject.load(),
                g_box1_reject.load(), g_no_box.load(), g_box2.load(), g_box2_pass.load(),
                g_light_loop.load());
}

namespace superman_returns::native {

void RunInstanceLod(PPCContext& ctx, uint8_t* base) {
  using namespace instance_lod;
  const int32_t mode = InstanceLodMode();
  if (mode == 1) {
    if (!NativeInstanceLod(ctx, base)) {
      g_fallbacks.fetch_add(1, std::memory_order_relaxed);
      __imp__sub_823055A8(ctx, base);
    }
    return;
  }
  if (mode != 2) {
    __imp__sub_823055A8(ctx, base);
    return;
  }
  // Shadow: original first (its result is kept), then native, compare, restore.
  const uint32_t o = ctx.r3.u32, r4 = ctx.r4.u32, r1 = ctx.r1.u32;
  const uint64_t lr = ctx.lr;
  const uint32_t lods = Ld32(base, o + 212);
  std::array<uint32_t, kMaxLods> start{};
  for (uint32_t i = 0; i < lods && i < kMaxLods; ++i) start[i] = Ld32(base, o + 236 + 4 * i);
  __imp__sub_823055A8(ctx, base);
  Outputs original, native;
  if (lods == 0 || lods > kMaxLods || !Capture(base, o, ctx.r3.u32, start, original)) return;
  // Only r1, r3 and LR are observable by the caller (volatile registers are
  // ABI-dead; non-volatile ones are not modified by either version). Do not
  // copy the whole context: its cached FPSCR mode must stay in sync with MXCSR.
  ctx.r3.u64 = o;
  ctx.r4.u64 = r4;
  ctx.r1.u64 = r1;
  ctx.lr = lr;
  const bool ran = NativeInstanceLod(ctx, base);
  const bool captured = ran && Capture(base, o, ctx.r3.u32, start, native);
  const bool same = captured && native.r3 == original.r3 && native.flags == original.flags &&
                    native.counters == original.counters && native.buckets == original.buckets &&
                    native.light_bytes == original.light_bytes;
  Restore(base, o, start, original);
  ctx.r3.u64 = original.r3;
  ctx.r1.u64 = r1;
  ctx.lr = lr;
  const uint64_t calls = g_calls.fetch_add(1, std::memory_order_relaxed) + 1;
  if (!same) {
    const uint64_t n = g_mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 20)
      REXLOG_WARN("[native instance lod] MISMATCH #{} obj={:08X} ran={} r3 {}/{} flags {:08X}/{:08X} "
                  "counters {}/{} {}/{} {}/{} buckets_equal={} light_bytes_equal={}",
                  n, o, ran, original.r3, native.r3, original.flags, native.flags,
                  original.counters[0], native.counters[0], original.counters[1],
                  native.counters[1], original.counters[2], native.counters[2],
                  native.buckets == original.buckets,
                  native.light_bytes == original.light_bytes);
  }
  if ((calls & ((1u << 16) - 1)) == 0)
    REXLOG_INFO("[native instance lod] shadow calls={} mismatches={}", calls,
                g_mismatches.load(std::memory_order_relaxed));
}

void RunFrustumCull(PPCContext& ctx, uint8_t* base) {
  using namespace frustum_cull;
  const int32_t mode = FrustumCullMode();
  if (mode == 1) {
    ctx.r3.u64 = NativeCull(ctx, base);
    return;
  }
  if (mode != 2) {
    __imp__sub_82319108(ctx, base);
    return;
  }
  const uint32_t r3 = ctx.r3.u32, r4 = ctx.r4.u32, r5 = ctx.r5.u32;
  const uint32_t native = NativeCull(ctx, base);
  ctx.r3.u64 = r3;
  __imp__sub_82319108(ctx, base);
  const uint64_t calls = g_calls.fetch_add(1, std::memory_order_relaxed) + 1;
  if (ctx.r3.u32 != native) {
    const uint64_t n = g_mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 20)
      REXLOG_WARN("[native frustum cull] MISMATCH #{} planes={:08X} point={:08X} bias={:08X} "
                  "original={} native={}", n, r3, r4, r5, ctx.r3.u32, native);
  }
  if ((calls & ((1u << 20) - 1)) == 0)
    REXLOG_INFO("[native frustum cull] shadow calls={} mismatches={}", calls,
                g_mismatches.load(std::memory_order_relaxed));
}

void RunObjectLod(PPCContext& ctx, uint8_t* base) {
  switch (ObjectLodMode()) {
    case 1: object_lod::NativeObjectLod(ctx, base); return;
    case 2: object_lod::Shadow(ctx, base); return;
    default: __imp__sub_82305B88(ctx, base); return;
  }
}

}  // namespace superman_returns::native
