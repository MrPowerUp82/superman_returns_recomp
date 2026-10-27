// Source-identified render scheduler: 82177F90 -> 82166178 -> 827209D8.
// REA live evidence and limits: docs/xenia-canary-fps-unlock-2026-10-27.md.
#include <rex/ppc.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <windows.h>
#include <array>
#include <cstring>
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
