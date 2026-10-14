// Opt-in timing of functions selected by the bounded CPU stack sample.
// Every call executes the original; no guest registers/memory are changed.
#include <rex/ppc.h>
#include <rex/logging.h>
#include "native_renderer.h"
#include "guest_cpu_profile.h"
#include <array>
#include <chrono>
#include <cstdlib>

namespace {
using Clock=std::chrono::steady_clock;
constexpr std::array<uint32_t,15> addresses{0x823055A8,0x82783948,0x82305B88,0x82316958,0x82319108,0x826092B8,0x821078C0,
  0x8230F8E0,0x827838D0,0x82300328,0x822E9E68,0x82307CF8,0x823103C8,0x822F5290,0}; // 0 = native capture
bool Enabled() {
  static const bool on=[] {auto* v=std::getenv("SR_NATIVE_GUEST_CPU_PROFILE");return v && *v && *v!='0';}();
  return on && superman_returns::native::Enabled();
}
struct Counters {uint64_t calls=0,inclusive_ns=0,self_ns=0;};
struct Scope;
struct ThreadProfile {
  std::array<Counters,addresses.size()> counters{};
  Scope* active=nullptr;
  uint32_t capture_depth=0;
  Clock::time_point epoch=Clock::now();
};
thread_local ThreadProfile guest_thread_profile;
struct Scope {
  size_t index;Clock::time_point begin;Scope* parent;uint64_t children_ns=0;
  explicit Scope(size_t i):index(i),begin(Clock::now()),parent(guest_thread_profile.active) {guest_thread_profile.active=this;}
  ~Scope() {
    auto end=Clock::now();auto ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count());
    auto& c=guest_thread_profile.counters[index];++c.calls;c.inclusive_ns+=ns;c.self_ns+=ns>=children_ns?ns-children_ns:0;
    guest_thread_profile.active=parent;
    if(parent) {parent->children_ns+=ns;return;}
    const double seconds=std::chrono::duration<double>(end-guest_thread_profile.epoch).count();
    if(seconds<2) return;
    for(size_t i=0;i<addresses.size();++i) if(auto& count=guest_thread_profile.counters[i];count.calls) {
      REXLOG_INFO("native guest CPU: address={:08X} interval_s={:.3f} calls={} inclusive_ms={:.3f} tracked_self_ms={:.3f}",
          addresses[i],seconds,count.calls,count.inclusive_ns/1e6,count.self_ns/1e6);
    }
    guest_thread_profile.counters={};guest_thread_profile.epoch=Clock::now();
  }
};
}
namespace superman_returns::native {
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
SR_PROFILE_GUEST(8230F8E0,7)
SR_PROFILE_GUEST(827838D0,8)
SR_PROFILE_GUEST(82300328,9)
SR_PROFILE_GUEST(822E9E68,10)
SR_PROFILE_GUEST(82307CF8,11)
SR_PROFILE_GUEST(823103C8,12)
SR_PROFILE_GUEST(822F5290,13)
#undef SR_PROFILE_GUEST
