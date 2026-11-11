// Source-identified render scheduler: 82177F90 -> 82166178 -> 827209D8.
// REA live evidence and limits: docs/xenia-canary-fps-unlock-2026-10-27.md.
#include <rex/ppc.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <windows.h>
#include <array>
#include <cstring>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <tuple>
#include "native_renderer.h"
#include "guest_render_scheduler_policy.h"

REXCVAR_DEFINE_BOOL(sr_native_guest_render_scheduler, true, "Superman Returns Native",
    "Apply the native FPS target to the confirmed variable-step guest render scheduler (experimental)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REX_EXTERN(__imp__sub_82166178);
extern "C" REX_FUNC(sub_82166178) {
  const uint32_t object=ctx.r3.u32;
  __imp__sub_82166178(ctx,base);
  if(!superman_returns::native::Enabled() || !REXCVAR_GET(sr_native_guest_render_scheduler) ||
      !base || !object || ctx.r3.u32!=object || uint64_t(object)+176>0x100000000ull) return;
  // This constructor has finished initialization, but its caller has not yet
  // published the object to the scheduler thread. No lock spans guest code.
  std::array<uint8_t,176> snapshot;
  SIZE_T copied=0;
  if(!ReadProcessMemory(GetCurrentProcess(),base+uint64_t(object),snapshot.data(),snapshot.size(),&copied) ||
      copied!=snapshot.size()) {
    REXLOG_WARN("native guest render scheduler: constructor read failed; original period preserved");return;
  }
  const int32_t fps=rex::cvar::Query<int32_t>("sr_native_fps_limit");
  if(!superman_returns::native::ApplyGuestRenderSchedulerPeriod(snapshot,fps)) return;
  // Write only the period. The game also uses it to seed startup timing and
  // subtract/clamp its remainder. Keep the normal elapsed-time delta update,
  // clock and catch-up ceiling; do not replace the game's timing calculation.
  std::memcpy(base+uint64_t(object)+72,snapshot.data()+72,8);
  uint64_t period=0;for(size_t i=72;i<80;++i) period=(period<<8)|snapshot[i];
  REXLOG_INFO("native guest render scheduler: object={:08X} mode=1 target={} original_period_ns=33333334 period_ns={} delta_source=game_elapsed_time",
      object,fps,period);
}

// Experimental: the fixed-step SIM scheduler (821780C0 constructs it inline,
// so the hook is its start, 82720590, before it seeds +104 from +72).
REXCVAR_DEFINE_BOOL(sr_native_guest_sim_scheduler, false, "Superman Returns Native",
    "Experimental: also run the fixed-step guest SIM scheduler at the native FPS target (31-240); "
    "changes the simulation timestep")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(sr_native_guest_scheduler_log, false, "Superman Returns Native",
    "Log the SIM and render schedulers' update count and accumulated game time against wall time every 10 s");

namespace {
std::atomic<uint32_t> g_sim_object{0}, g_render_object{0};
uint64_t Be64(const uint8_t* p) {uint64_t v=0;for(int i=0;i<8;++i) v=(v<<8)|p[i];return v;}
uint32_t Be32(const uint8_t* p) {return uint32_t(Be64(p)>>32);}
void LogSchedulers(uint8_t* base) {
  struct Last {uint64_t time=0;uint32_t updates=0;};
  Last sim, render;
  auto wall=std::chrono::steady_clock::now();
  for(;;) {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    const auto now=std::chrono::steady_clock::now();
    const double wall_s=std::chrono::duration<double>(now-wall).count();wall=now;
    for(auto [name,object,last]:{std::tuple{"sim",g_sim_object.load(),&sim},
        std::tuple{"render",g_render_object.load(),&render}}) {
      if(!object) continue;
      std::array<uint8_t,136> s;SIZE_T copied=0;
      if(!ReadProcessMemory(GetCurrentProcess(),base+uint64_t(object),s.data(),s.size(),&copied) ||
          copied!=s.size()) continue;
      const uint64_t time=Be64(s.data()+80),period=Be64(s.data()+72);
      const uint32_t updates=Be32(s.data()+128);
      if(last->time || last->updates) {
        REXLOG_INFO("guest scheduler {}: period_ns={} updates/s={:.1f} game_time/wall={:.4f}",
            name,period,(updates-last->updates)/wall_s,double(time-last->time)/1e9/wall_s);
      }
      last->time=time;last->updates=updates;
    }
  }
}
}

REX_EXTERN(__imp__sub_82720590);
extern "C" REX_FUNC(sub_82720590) {
  const uint32_t object=ctx.r3.u32;
  if(superman_returns::native::Enabled() && base && object && uint64_t(object)+176<=0x100000000ull) {
    std::array<uint8_t,176> snapshot;
    SIZE_T copied=0;
    if(ReadProcessMemory(GetCurrentProcess(),base+uint64_t(object),snapshot.data(),snapshot.size(),&copied) &&
        copied==snapshot.size()) {
      const uint32_t vtable=Be32(snapshot.data());
      if(vtable==0x82005B98) g_render_object=object;
      if(vtable==0x82005BC4) {
        g_sim_object=object;
        const int32_t fps=rex::cvar::Query<int32_t>("sr_native_fps_limit");
        if(REXCVAR_GET(sr_native_guest_sim_scheduler) &&
            superman_returns::native::ApplyGuestSimSchedulerPeriod(snapshot,fps)) {
          // Before the start seeds +104 from +72; the thread is not running yet.
          std::memcpy(base+uint64_t(object)+72,snapshot.data()+72,8);
          REXLOG_INFO("native guest sim scheduler (experimental): object={:08X} mode=0 target={} "
              "original_period_ns=33333334 period_ns={} timestep=period",object,fps,Be64(snapshot.data()+72));
        }
      }
      static std::once_flag log_once;
      if(REXCVAR_GET(sr_native_guest_scheduler_log))
        std::call_once(log_once,[base] {std::thread(LogSchedulers,base).detach();});
    }
  }
  __imp__sub_82720590(ctx,base);
}
