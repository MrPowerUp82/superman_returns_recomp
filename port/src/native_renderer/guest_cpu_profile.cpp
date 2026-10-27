// Opt-in timing of functions selected by the bounded CPU stack sample.
// Every call executes the original; no guest registers/memory are changed.
#include <rex/ppc.h>
#include <rex/logging.h>
#include <rex/chrono/clock.h>
#include "native_renderer.h"
#include "guest_cpu_profile.h"
#include "instance_probe.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include "checked_guest_memory.h"

namespace {
using Clock=std::chrono::steady_clock;
constexpr size_t original_begin=14,original_end=23;
constexpr std::array<uint32_t,24> addresses{0x823055A8,0x82783948,0x82305B88,0x82316958,0x82319108,0x826092B8,0x821078C0,
  0x8230F8E0,0x827838D0,0x82300328,0x822E9E68,0x82307CF8,0x823103C8,0x822F5290,
  0x820FBBF8,0x820FC000,0x820FBBB0,0x820FB6E8,0x820FBBA0,0x8210C5F8,0x8210D588,0x8210DA98,0x82101A58,
  0}; // 0 = native capture
bool Enabled() {
  static const bool on=[] {auto* v=std::getenv("SR_NATIVE_GUEST_CPU_PROFILE");return v && *v && *v!='0';}();
  return on && superman_returns::native::Enabled();
}
bool SleepProfileEnabled() {
  // Independent opt-in. Also enable SR_NATIVE_GUEST_CPU_PROFILE to emit
  // zero-call windows from render-thread profiler activity during gameplay.
  static const bool on=[] {auto* v=std::getenv("SR_NATIVE_GUEST_SLEEP_PROFILE");return v && *v && *v!='0';}();
  return on;
}
bool ClockProfileEnabled() {
  static const bool on=[] {auto* v=std::getenv("SR_NATIVE_GUEST_CLOCK_PROFILE");return v && *v && *v!='0';}();
  return on;
}
const char* PresentProbePath() {
  static const char* path=[] {
    const auto* value=std::getenv("SR_NATIVE_PRESENT_INTERVAL_PROBE_TRIGGER_PATH");
    return value && *value ? value : nullptr;
  }();
  return path;
}
struct PresentProbe {
  std::atomic<bool> triggered{false};
  std::atomic<uint32_t> pre_swap{0},fence{0},setter{0};
};
PresentProbe& PresentProbeState() {static PresentProbe state;return state;}
bool ClaimPresentProbe(std::atomic<uint32_t>& count,uint32_t limit,uint32_t& sample) {
  auto value=count.load(std::memory_order_relaxed);
  while(value<limit) {
    if(count.compare_exchange_weak(value,value+1,std::memory_order_relaxed)) {
      sample=value+1;return true;
    }
  }
  return false;
}
bool PresentProbeTriggered() {
  auto& state=PresentProbeState();
  if(state.triggered.load(std::memory_order_relaxed)) return true;
  std::error_code error;
  if(!std::filesystem::exists(PresentProbePath(),error) || error) return false;
  state.triggered.store(true,std::memory_order_relaxed);
  return true;
}
// Diagnostic reads may encounter a device during teardown. A failed/partial
// read is logged explicitly; never let observing it fault the guest thread.
bool PresentProbeRead(uint8_t* base,uint32_t device,uint32_t offset,uint32_t& value) {
  value=0;
  if(!base || !device || uint64_t(device)+offset+sizeof(value)>0x100000000ull) return false;
  SIZE_T copied=0;
  if(!ReadProcessMemory(GetCurrentProcess(),base+uint64_t(device)+offset,&value,sizeof(value),&copied)
      || copied!=sizeof(value)) {value=0;return false;}
  value=__builtin_bswap32(value);return true;
}
struct ClockProfile {
  struct Caller {uint32_t address=0,parent=0;uint64_t calls=0;};
  std::array<Caller,64> callers{};
  uint64_t epoch=0,calls=0,overflow=0;
  void Record(uint32_t caller,uint32_t parent,uint64_t tick) {
    if(!epoch || tick<epoch) epoch=tick;
    ++calls;
    bool recorded=false;
    for(auto& row:callers) if((row.address==caller && row.parent==parent) || !row.calls) {
      row.address=caller;row.parent=parent;++row.calls;recorded=true;break;
    }
    if(!recorded) ++overflow;
    // Reuse the clock value produced by the original guest function. No
    // extra host clock query, lock or allocation on a potentially hot poll.
    static const uint64_t frequency=rex::chrono::Clock::guest_tick_frequency();
    if(!frequency || (tick-epoch)/frequency<2) return;
    const double seconds=double(tick-epoch)/double(frequency);
    REXLOG_INFO("native guest clock: hook=824680D0 interval_s={:.3f} calls={} overflow={}",seconds,calls,overflow);
    for(const auto& row:callers) if(row.calls)
      REXLOG_INFO("native guest clock caller: return={:08X} parent={:08X} calls={} calls_per_s={:.1f}",row.address,row.parent,row.calls,row.calls/seconds);
    callers={};calls=overflow=0;epoch=tick;
  }
};
struct SleepProfile {
  std::mutex mutex;
  superman_returns::native::GuestSleepWindow window;
  Clock::time_point epoch=Clock::now();
};
SleepProfile& SleepState() {static SleepProfile state;return state;}
void ReportGuestSleeps(Clock::time_point now) {
  if(!SleepProfileEnabled()) return;
  auto& state=SleepState();
  superman_returns::native::GuestSleepWindow snapshot;
  double seconds;
  {
    std::lock_guard lock(state.mutex);
    seconds=std::chrono::duration<double>(now-state.epoch).count();
    if(seconds<2) return;
    snapshot=state.window;state.window={};state.epoch=now;
  }
  REXLOG_INFO("native guest sleep: hook=8246A898 interval_s={:.3f} calls={} elapsed_ms={:.3f} movie33_calls={} movie33_ms={:.3f} overflow_calls={} overflow_ms={:.3f}",
      seconds,snapshot.calls,snapshot.elapsed_ns/1e6,snapshot.callers[0].calls,
      snapshot.callers[0].elapsed_ns/1e6,snapshot.overflow_calls,snapshot.overflow_ns/1e6);
  for(const auto& row:snapshot.callers) if(row.calls)
    REXLOG_INFO("native guest sleep caller: return={:08X} requested_ms={} calls={} elapsed_ms={:.3f} max_ms={:.3f}",
        row.caller,row.requested_ms,row.calls,row.elapsed_ns/1e6,row.max_ns/1e6);
}
struct Counters {uint64_t calls=0,inclusive_ns=0,self_ns=0,capture_ns=0;};
struct Scope;
struct ThreadProfile {
  std::array<Counters,addresses.size()> counters{};
  Counters original_roots{};
  Scope* active=nullptr;
  uint32_t capture_depth=0;
  Clock::time_point epoch=Clock::now();
};
thread_local ThreadProfile guest_thread_profile;
struct Scope {
  size_t index;Clock::time_point begin;Scope* parent;uint64_t children_ns=0,capture_ns=0;
  bool original_root=false;
  explicit Scope(size_t i):index(i),begin(Clock::now()),parent(guest_thread_profile.active) {
    original_root=i>=original_begin && i<original_end;
    if(original_root) for(auto* ancestor=parent;ancestor;ancestor=ancestor->parent)
      if(ancestor->index>=original_begin && ancestor->index<original_end) {original_root=false;break;}
    guest_thread_profile.active=this;
  }
  ~Scope() {
    auto end=Clock::now();auto ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count());
    auto& c=guest_thread_profile.counters[index];++c.calls;c.inclusive_ns+=ns;c.self_ns+=ns>=children_ns?ns-children_ns:0;
    c.capture_ns+=capture_ns;
    if(original_root) {auto& roots=guest_thread_profile.original_roots;++roots.calls;roots.inclusive_ns+=ns;roots.capture_ns+=capture_ns;}
    guest_thread_profile.active=parent;
    if(parent) {parent->children_ns+=ns;return;}
    const double seconds=std::chrono::duration<double>(end-guest_thread_profile.epoch).count();
    if(seconds<2) return;
    ReportGuestSleeps(end);
    for(size_t i=0;i<addresses.size();++i) if(auto& count=guest_thread_profile.counters[i];count.calls) {
      REXLOG_INFO("native guest CPU: address={:08X} interval_s={:.3f} calls={} inclusive_ms={:.3f} tracked_self_ms={:.3f} nested_capture_ms={:.3f}",
          addresses[i],seconds,count.calls,count.inclusive_ns/1e6,count.self_ns/1e6,count.capture_ns/1e6);
    }
    const auto& roots=guest_thread_profile.original_roots;
    if(roots.calls) REXLOG_INFO("native guest graphics originals: interval_s={:.3f} calls={} wall_ms={:.3f} nested_capture_ms={:.3f} excluding_capture_ms={:.3f}",
        seconds,roots.calls,roots.inclusive_ns/1e6,roots.capture_ns/1e6,
        (roots.inclusive_ns>=roots.capture_ns?roots.inclusive_ns-roots.capture_ns:0)/1e6);
    guest_thread_profile.counters={};guest_thread_profile.original_roots={};guest_thread_profile.epoch=Clock::now();
  }
};
}
namespace superman_returns::native {
void ProfileGuestGraphicsOriginal(uint32_t address,PPCFunc* original,PPCContext& ctx,uint8_t* base) {
  if(!::Enabled()) {original(ctx,base);return;}
  const auto found=std::find(addresses.begin()+original_begin,addresses.begin()+original_end,address);
  if(found==addresses.begin()+original_end) {original(ctx,base);return;}
  Scope scope(size_t(found-addresses.begin()));original(ctx,base);
}
GuestNativeCaptureProfile::GuestNativeCaptureProfile():enabled_(::Enabled()) {
  if(!enabled_) return;
  outer_=guest_thread_profile.capture_depth++==0;
  if(outer_) begin_=Clock::now();
}
GuestNativeCaptureProfile::~GuestNativeCaptureProfile() {
  if(!enabled_) return;
  --guest_thread_profile.capture_depth;
  if(!outer_) return;
  auto ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin_).count());
  auto& c=guest_thread_profile.counters.back();++c.calls;c.inclusive_ns+=ns;c.self_ns+=ns;
  if(guest_thread_profile.active) guest_thread_profile.active->children_ns+=ns;
  for(auto* ancestor=guest_thread_profile.active;ancestor;ancestor=ancestor->parent) ancestor->capture_ns+=ns;
}
}
#define SR_PROFILE_GUEST(address,index) \
  REX_EXTERN(__imp__sub_##address); \
  extern "C" REX_FUNC(sub_##address) { \
    if(!Enabled()) {__imp__sub_##address(ctx,base);return;} \
    Scope scope(index);__imp__sub_##address(ctx,base); \
  }
SR_PROFILE_GUEST(823055A8,0)
SR_PROFILE_GUEST(82783948,1)
SR_PROFILE_GUEST(82305B88,2)
SR_PROFILE_GUEST(82316958,3)
SR_PROFILE_GUEST(82319108,4)
SR_PROFILE_GUEST(826092B8,5)
SR_PROFILE_GUEST(821078C0,6)
SR_PROFILE_GUEST(827838D0,8)
SR_PROFILE_GUEST(822E9E68,10)
SR_PROFILE_GUEST(82307CF8,11)
SR_PROFILE_GUEST(822F5290,13)
#undef SR_PROFILE_GUEST
// These two entry points also carry the opt-in lifetime association scope.
// Keep one strong wrapper per guest symbol, including when CPU timing is off.
#define SR_PROFILE_RENDER(address,index) \
  REX_EXTERN(__imp__sub_##address); \
  extern "C" REX_FUNC(sub_##address) { \
    if(!Enabled()) {superman_returns::native::InvokeInstanceProbeRender(0x##address,__imp__sub_##address,ctx,base);return;} \
    Scope scope(index);superman_returns::native::InvokeInstanceProbeRender(0x##address,__imp__sub_##address,ctx,base); \
  }
SR_PROFILE_RENDER(8230F8E0,7)
SR_PROFILE_RENDER(823103C8,12)
SR_PROFILE_RENDER(82300328,9)
#undef SR_PROFILE_RENDER

// The 19 statically identified Sleep/SleepEx call sites reach this routine.
// Capture inputs before the original overwrites volatile registers. Never
// restore the input context: the original's complete output must reach callers.
// Disabled path performs only the cached switch check and original call.
REX_EXTERN(__imp__sub_8246A898);
extern "C" REX_FUNC(sub_8246A898) {
  if(!SleepProfileEnabled()) {__imp__sub_8246A898(ctx,base);return;}
  const uint32_t caller=ctx.lr,requested_ms=ctx.r3.u32;
  const auto begin=Clock::now();
  __imp__sub_8246A898(ctx,base);
  const auto end=Clock::now();
  const auto ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count());
  auto& state=SleepState();
  {
    std::lock_guard lock(state.mutex);
    state.window.Record(caller,requested_ms,ns);
  }
  ReportGuestSleeps(end);
}

REX_EXTERN(__imp__sub_824680D0);
extern "C" REX_FUNC(sub_824680D0) {
  if(!ClockProfileEnabled()) {__imp__sub_824680D0(ctx,base);return;}
  const uint32_t caller=ctx.lr;
  __imp__sub_824680D0(ctx,base);
  // The generated original leaves its mftb value in r11 and writes the same
  // value to the caller's output pointer. Its entire output context survives.
  thread_local ClockProfile profile;
  uint32_t parent=0;
  if(caller==0x825667DC) {
    // The millisecond helper 825667C8 saves its incoming LR at SP+88 in
    // its 96-byte frame before calling this clock routine. Resolve that
    // verified frame only; do not attempt a heuristic guest stack walk.
    std::memcpy(&parent,base+ctx.r1.u32+88,sizeof(parent));
    parent=__builtin_bswap32(parent);
  }
  profile.Record(caller,parent,ctx.r11.u64);
}

// Independent diagnostic: the cached null-path check is the entire disabled
// overhead. Originals receive the untouched input context exactly once and
// their complete output context is returned (never restore input registers).
REX_EXTERN(__imp__sub_82111EC8);
extern "C" REX_FUNC(sub_82111EC8) {
  if(!PresentProbePath()) {__imp__sub_82111EC8(ctx,base);return;}
  uint32_t sample=0;
  if(!PresentProbeTriggered() || !ClaimPresentProbe(PresentProbeState().pre_swap,128,sample)) {
    __imp__sub_82111EC8(ctx,base);return;
  }
  const uint32_t caller=ctx.lr,device=ctx.r3.u32;
  uint32_t interval=0,gate=0,writeback=0,counter=0;
  uint32_t valid=PresentProbeRead(base,device,0x33A4,interval) ? 1u : 0u;
  if(PresentProbeRead(base,device,13456,gate)) valid|=2;
  if(PresentProbeRead(base,device,10772,writeback)) valid|=4;
  if(PresentProbeRead(base,device,15132,counter)) valid|=8;
  const auto begin=Clock::now();
  __imp__sub_82111EC8(ctx,base);
  const double elapsed_ms=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
  REXLOG_INFO("native present interval pre-swap: sample={} return={:08X} dev={:08X} interval={:08X} gate={:08X} writeback={:08X} counter={} valid_mask={:X} elapsed_ms={:.6f}",
      sample,caller,device,interval,gate,writeback,counter,valid,elapsed_ms);
}

REX_EXTERN(__imp__sub_820FD9A8);
extern "C" REX_FUNC(sub_820FD9A8) {
  if(!PresentProbePath()) {__imp__sub_820FD9A8(ctx,base);return;}
  // Generated Swap occupies [82112050,82112750); its final fence call
  // returns to 82112720. Exclude every resource wait outside this routine.
  const uint32_t caller=ctx.lr;
  uint32_t sample=0;
  if(caller<0x82112050 || caller>=0x82112750 || !PresentProbeTriggered()
      || !ClaimPresentProbe(PresentProbeState().fence,128,sample)) {
    __imp__sub_820FD9A8(ctx,base);return;
  }
  const uint32_t device=ctx.r3.u32,request=ctx.r4.u32,reason=ctx.r5.u32;
  const auto begin=Clock::now();
  __imp__sub_820FD9A8(ctx,base);
  const double elapsed_ms=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
  REXLOG_INFO("native present interval fence: sample={} return={:08X} dev={:08X} request={:08X} reason={} elapsed_ms={:.6f}",
      sample,caller,device,request,reason,elapsed_ms);
}

REX_EXTERN(__imp__sub_820F1D10);
extern "C" REX_FUNC(sub_820F1D10) {
  if(!PresentProbePath()) {__imp__sub_820F1D10(ctx,base);return;}
  uint32_t sample=0;
  // Capture the first eight writes even before the HUD trigger: otherwise
  // device initialization may be invisible in the gameplay-only window.
  if(!ClaimPresentProbe(PresentProbeState().setter,8,sample)) {
    __imp__sub_820F1D10(ctx,base);return;
  }
  const uint32_t caller=ctx.lr,device=ctx.r3.u32,interval=ctx.r4.u32;
  __imp__sub_820F1D10(ctx,base);
  REXLOG_INFO("native present interval setter: sample={} return={:08X} dev={:08X} interval={:08X}",
      sample,caller,device,interval);
}
