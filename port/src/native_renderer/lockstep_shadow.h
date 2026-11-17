// Retail port of the demo decomp's port/demo_runtime/lockstep_shadow.h
// (supermanReturns-demo-decomp); unchanged except the generated include.
// Compiled only in the main target, which sees port/generated/default.
//
// Lockstep shadow validation for hand rewrites whose callees have side effects.
//
// A rewrite is written once as `template <class Env> void Walk(Env&, PPCContext&)`
// and does every guest memory access and call through Env:
//   R32/R8/RBytes, W8/W32/W64/WBytes, Call(ctx, target, lr, args) (indirect
//   lookup), CallDirect(ctx, fn, target, lr, args) and VCall (vtable slot).
// DirectEnv is the production environment. In shadow mode the original runs
// once as a traced copy (tools/analysis/trace_functions.py: T_BEFORE / T_AFTER around
// each call, T_STORE_* and T_SAVE_REGION log the old bytes of each store) and
// the rewrite runs on a fiber with ShadowEnv: its writes go to an overlay and
// its reads see overlay -> bytes the original wrote this segment (old value)
// -> guest memory, i.e. memory as it was right after the previous callee. At
// every call the original makes, the rewrite must request the same call
// (target, return address, argument registers in `args`) and every byte
// either side wrote since the previous call must hold the rewrite's value.
// The original's state after each callee is fed back to the rewrite, so the
// callees run exactly once. See docs/native-demo-ports-2026-11-16.md.
#pragma once

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "generated/default/superman_returns_init.h"

#include <rex/platform.h>
// The shadow runs the rewrite on a fiber: Win32 fibers on Windows. Elsewhere
// (no fiber implementation yet) Shadow() runs the original and the rewrite
// stays available in production mode; validation is a Windows-side step.
#if REX_PLATFORM_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define LOCKSTEP_FIBERS 1
#define LOCKSTEP_FIBER_API WINAPI
#else
#define LOCKSTEP_FIBERS 0
#define LOCKSTEP_FIBER_API
#endif

namespace lockstep {

inline void FiberSwitch(void* fiber) {
#if LOCKSTEP_FIBERS
  SwitchToFiber(fiber);
#else
  (void)fiber;  // unreachable: no session is created without fibers
#endif
}

using GuestFn = void (*)(PPCContext& __restrict, uint8_t*);

// Argument registers a call site sets (compared by the shadow).
constexpr uint32_t kR3 = 1, kR4 = 2, kR5 = 4, kR6 = 8, kF1 = 16, kR7 = 32, kR8 = 64, kR9 = 128, kR10 = 256;

inline uint8_t* Host(uint8_t* base, uint32_t a) { return base + a + (a >= 0xE0000000u ? 0x1000u : 0u); }

struct DirectEnv {
  static constexpr bool kPrefetch = true;  // hints only; the shadow environment skips them
  uint8_t* base;
  void Prefetch(uint32_t a) const { __builtin_prefetch(Host(base, a)); }
  uint32_t R32(uint32_t a) const { uint32_t v; std::memcpy(&v, Host(base, a), 4); return __builtin_bswap32(v); }
  uint64_t R64(uint32_t a) const { uint64_t v; std::memcpy(&v, Host(base, a), 8); return __builtin_bswap64(v); }
  uint32_t R8(uint32_t a) const { return *Host(base, a); }
  void RBytes(uint32_t a, uint8_t* p, uint32_t n) const { std::memcpy(p, Host(base, a), n); }
  void W32(uint32_t a, uint32_t v) const { v = __builtin_bswap32(v); std::memcpy(Host(base, a), &v, 4); }
  void W64(uint32_t a, uint64_t v) const { v = __builtin_bswap64(v); std::memcpy(Host(base, a), &v, 8); }
  void W8(uint32_t a, uint32_t v) const { *Host(base, a) = uint8_t(v); }
  void WBytes(uint32_t a, const uint8_t* p, uint32_t n) const { std::memcpy(Host(base, a), p, n); }
  void Call(PPCContext& ctx, uint32_t target, uint32_t lr, uint32_t) const {
    uint8_t* base = this->base;
    ctx.lr = lr;
    REX_CALL_INDIRECT_FUNC(target);
  }
  void CallDirect(PPCContext& ctx, GuestFn fn, uint32_t, uint32_t lr, uint32_t) const {
    ctx.lr = lr;
    fn(ctx, base);
  }
};

template <class Env>
inline uint32_t VCall(Env& e, PPCContext& ctx, uint32_t object, uint32_t slot, uint32_t lr, uint32_t args) {
  const uint32_t target = e.R32(e.R32(object) + slot);
  e.Call(ctx, target, lr, args);
  return target;
}

// __savegprlr_N (rN at r1 - 8 * (33 - N), saved LR word at r1 - 8).
template <class Env>
void SaveGprs(Env& e, const PPCContext& ctx, uint32_t r1, int first) {
  const uint64_t regs[18] = {ctx.r14.u64, ctx.r15.u64, ctx.r16.u64, ctx.r17.u64, ctx.r18.u64, ctx.r19.u64,
                             ctx.r20.u64, ctx.r21.u64, ctx.r22.u64, ctx.r23.u64, ctx.r24.u64, ctx.r25.u64,
                             ctx.r26.u64, ctx.r27.u64, ctx.r28.u64, ctx.r29.u64, ctx.r30.u64, ctx.r31.u64};
  for (int n = first; n <= 31; ++n) e.W64(r1 - 8 * (33 - n), regs[n - 14]);
  e.W32(r1 - 8, uint32_t(ctx.lr));
}

// __restgprlr_N: r1 back, saved LR word reloaded into r12 and LR.
inline void Epilogue(PPCContext& ctx, uint32_t r1, uint64_t lr) {
  ctx.r1.u64 = r1;
  ctx.r12.u64 = uint32_t(lr);
  ctx.lr = uint32_t(lr);
}

struct ShadowEnv;
using ShadowWalk = void (*)(ShadowEnv&, PPCContext&);

struct CallRecord {
  uint32_t target = 0, lr = 0, args = 0;
  uint32_t r[8] = {};  // r3..r10 (low words)
  uint64_t f1 = 0;
};

struct Session {
  uint8_t* base = nullptr;
  void* main_fiber = nullptr;
  void* native_fiber = nullptr;
  ShadowWalk walk = nullptr;
  PPCContext entry{}, after{}, native_end{};
  std::unordered_map<uint32_t, uint8_t> orig_old, overlay;
  CallRecord expected;
  bool native_finished = false, aborted = false, failed = false;
  uint32_t segment = 0, fault_addr = 0;
  char reason[160] = {};
};

inline thread_local Session* t_session = nullptr;
inline thread_local int t_shadow_depth = 0;

inline CallRecord Record(const PPCContext& ctx, uint32_t target, uint32_t lr, uint32_t args) {
  CallRecord c;
  c.target = target;
  c.lr = lr;
  c.args = args;
  const uint32_t r[8] = {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32};
  std::memcpy(c.r, r, sizeof(r));
  c.f1 = ctx.f1.u64;
  return c;
}

struct ShadowEnv {
  static constexpr bool kPrefetch = false;
  Session* s;
  void Prefetch(uint32_t) const {}
  uint32_t Byte(uint32_t a) const {
    if (auto it = s->overlay.find(a); it != s->overlay.end()) return it->second;
    if (auto it = s->orig_old.find(a); it != s->orig_old.end()) return it->second;
    return *Host(s->base, a);
  }
  uint32_t R32(uint32_t a) const { return (Byte(a) << 24) | (Byte(a + 1) << 16) | (Byte(a + 2) << 8) | Byte(a + 3); }
  uint64_t R64(uint32_t a) const { return (uint64_t(R32(a)) << 32) | R32(a + 4); }
  uint32_t R8(uint32_t a) const { return Byte(a); }
  void RBytes(uint32_t a, uint8_t* p, uint32_t n) const { for (uint32_t k = 0; k < n; ++k) p[k] = uint8_t(Byte(a + k)); }
  void W8(uint32_t a, uint32_t v) const { s->overlay[a] = uint8_t(v); }
  void W32(uint32_t a, uint32_t v) const {
    if (a == s->fault_addr && v != 0) v += 4;  // negative control (see the callers' fault cvars)
    for (int k = 0; k < 4; ++k) W8(a + k, v >> (24 - 8 * k));
  }
  void W64(uint32_t a, uint64_t v) const { for (int k = 0; k < 8; ++k) W8(a + k, uint32_t(v >> (56 - 8 * k))); }
  void WBytes(uint32_t a, const uint8_t* p, uint32_t n) const { for (uint32_t k = 0; k < n; ++k) W8(a + k, p[k]); }
  void Call(PPCContext& ctx, uint32_t target, uint32_t lr, uint32_t args) const {
    if (s->aborted) return;  // a mismatch was reported: run to completion without calls
    s->expected = Record(ctx, target, lr, args);
    FiberSwitch(s->main_fiber);
    if (s->aborted) return;
    ctx = s->after;  // the original's state right after the callee
  }
  void CallDirect(PPCContext& ctx, GuestFn, uint32_t target, uint32_t lr, uint32_t args) const {
    Call(ctx, target, lr, args);
  }
};

inline void LOCKSTEP_FIBER_API NativeFiber(void* param) {
  Session& s = *static_cast<Session*>(param);
  for (;;) {
    PPCContext ctx = s.entry;
    ShadowEnv env{&s};
    s.walk(env, ctx);
    s.native_end = ctx;
    s.native_finished = true;
    FiberSwitch(s.main_fiber);
  }
}

inline void Fail(Session& s, const char* what, uint32_t a, uint32_t b) {
  if (s.failed) return;
  s.failed = true;
  s.aborted = true;
  std::snprintf(s.reason, sizeof(s.reason), "segment %u: %s (%08X / %08X)", s.segment, what, a, b);
}

// Every byte either side wrote since the last call must now hold the rewrite's value.
inline void CompareMemory(Session& s) {
  for (const auto& [a, old] : s.orig_old) {
    const auto it = s.overlay.find(a);
    const uint8_t want = it != s.overlay.end() ? it->second : old;
    if (*Host(s.base, a) != want) return Fail(s, "byte differs at", a, (uint32_t(*Host(s.base, a)) << 8) | want);
  }
  for (const auto& [a, v] : s.overlay)
    if (!s.orig_old.count(a) && *Host(s.base, a) != v) return Fail(s, "native-only write at", a, v);
}

inline void TraceLogOld(uint8_t* base, uint32_t a, uint32_t size) {
  Session* s = t_session;
  if (!s || s->failed) return;
  for (uint32_t k = 0; k < size; ++k) s->orig_old.try_emplace(a + k, *Host(base, a + k));
}

inline void TraceBefore(PPCContext& ctx, uint32_t target) {
  Session* s = t_session;
  if (!s || s->failed) return;
  ++s->segment;
  FiberSwitch(s->native_fiber);
  if (s->native_finished) return Fail(*s, "native returned before call to", target, uint32_t(ctx.lr));
  const CallRecord& x = s->expected;
  if (x.target != target || x.lr != uint32_t(ctx.lr)) return Fail(*s, "call target/lr", x.target, target);
  const CallRecord y = Record(ctx, target, uint32_t(ctx.lr), x.args);
  for (int k = 0; k < 8; ++k) {
    const uint32_t bit = k < 4 ? (1u << k) : (32u << (k - 4));
    if ((x.args & bit) && x.r[k] != y.r[k]) return Fail(*s, k < 4 ? "argument r3..r6 at lr" : "argument r7..r10 at lr", x.lr, uint32_t(k + 3));
  }
  if ((x.args & kF1) && x.f1 != y.f1) return Fail(*s, "f1 at lr", x.lr, uint32_t(x.f1 ^ y.f1));
  CompareMemory(*s);
  s->orig_old.clear();
  s->overlay.clear();
}

inline void TraceAfter(PPCContext& ctx) {
  Session* s = t_session;
  if (s && !s->failed) s->after = ctx;
}

struct Counters {
  const char* name;
  std::atomic<uint64_t> sessions{0}, calls{0}, mismatches{0};
};

inline Session& ThreadSession(uint8_t* base) {
  thread_local std::unique_ptr<Session> session;
  if (!session) {
    session = std::make_unique<Session>();
#if LOCKSTEP_FIBERS
    session->native_fiber = CreateFiberEx(1 << 20, 1 << 20, FIBER_FLAG_FLOAT_SWITCH, NativeFiber, session.get());
#endif
  }
  session->base = base;
  return *session;
}

// Runs `traced` (the original with trace hooks) and `walk` in lockstep. Nested
// sessions (a callee re-entering a shadowed routine) and a missing traced copy
// run `original` instead.
inline void Shadow(PPCContext& ctx, uint8_t* base, ShadowWalk walk, GuestFn traced, GuestFn original, Counters& c,
                   uint32_t fault_addr = 0,
                   bool (*same_result)(const PPCContext&, const PPCContext&) = nullptr) {
  if (t_shadow_depth > 0 || !traced) {
    original(ctx, base);
    return;
  }
  Session& s = ThreadSession(base);
  if (!s.native_fiber) {
    original(ctx, base);
    return;
  }
#if LOCKSTEP_FIBERS
  s.main_fiber = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
#endif
  s.walk = walk;
  s.entry = ctx;
  s.orig_old.clear();
  s.overlay.clear();
  s.native_finished = s.aborted = s.failed = false;
  s.segment = 0;
  s.fault_addr = fault_addr;
  ++t_shadow_depth;
  t_session = &s;
  traced(ctx, base);
  if (!s.failed) {
    ++s.segment;
    FiberSwitch(s.native_fiber);
    if (!s.native_finished) {
      Fail(s, "native made an extra call to", s.expected.target, s.expected.lr);
    } else {
      CompareMemory(s);
      if (!s.failed && (s.native_end.r1.u64 != ctx.r1.u64 || uint32_t(s.native_end.lr) != uint32_t(ctx.lr)))
        Fail(s, "r1/lr at return", s.native_end.r1.u32, ctx.r1.u32);
      if (!s.failed && same_result && !same_result(s.native_end, ctx))
        Fail(s, "registers at return", s.native_end.r3.u32, ctx.r3.u32);
    }
  }
  if (!s.native_finished) {  // let the native walk finish without calls
    s.aborted = true;
    FiberSwitch(s.native_fiber);
  }
  t_session = nullptr;
  --t_shadow_depth;
  const uint64_t n = c.sessions.fetch_add(1, std::memory_order_relaxed) + 1;
  c.calls.fetch_add(s.segment, std::memory_order_relaxed);
  if (s.failed) {
    const uint64_t k = c.mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (k <= 20) REXLOG_WARN("[native lockstep {}] MISMATCH #{} r3={:08X} {}", c.name, k, s.entry.r3.u32, s.reason);
  }
  if ((n & ((1u << 12) - 1)) == 0)
    REXLOG_INFO("[native lockstep {}] shadow sessions={} calls={} mismatches={}", c.name, n,
                c.calls.load(std::memory_order_relaxed), c.mismatches.load(std::memory_order_relaxed));
}

template <void (*Native)(DirectEnv&, PPCContext&)>
inline void RunNative(PPCContext& ctx, uint8_t* base) {
  DirectEnv env{base};
  Native(env, ctx);
}

inline bool Guarded(const PPCContext& ctx) {
  return ctx.r1.u32 < 0x400u || (ctx.r3.u32 >= 0x7F000000u && ctx.r3.u32 < 0x80000000u);
}

}  // namespace lockstep

// Hooks used by the traced copies (port/generated/traced/*.inc, git-ignored).
#define T_BEFORE(ctx, target) ::lockstep::TraceBefore(ctx, target)
#define T_AFTER(ctx) ::lockstep::TraceAfter(ctx)
#define T_SAVE_REGION(a, size) ::lockstep::TraceLogOld(base, (uint32_t)(a), size)
#define T_STORE_IMPL(bits, bytes, a, v)               \
  do {                                                \
    const uint32_t t_a_ = (uint32_t)(a);              \
    ::lockstep::TraceLogOld(base, t_a_, bytes);       \
    REX_STORE_U##bits(t_a_, v);                       \
  } while (0)
#define T_STORE_U8(a, v) T_STORE_IMPL(8, 1, a, v)
#define T_STORE_U16(a, v) T_STORE_IMPL(16, 2, a, v)
#define T_STORE_U32(a, v) T_STORE_IMPL(32, 4, a, v)
#define T_STORE_U64(a, v) T_STORE_IMPL(64, 8, a, v)
