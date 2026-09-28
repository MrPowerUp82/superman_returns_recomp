// Guest frame counter for the F3 debug overlay.
//
// sub_82112050 is the game's present routine (the only caller of VdSwap).
// Overriding its weak symbol lets us count presented frames and then run the
// original recompiled body.

#include "frame_stats.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>

#include <rex/hook.h>

#include "generated/default/superman_returns_init.h"
#include "renderdoc_capture.h"

namespace {

std::atomic<uint64_t> g_frames{0};

// SR_LOG_FPS=1 writes the frame rate to the log every 2 seconds.
void MaybeLogFps(uint64_t frames) {
  using clock = std::chrono::steady_clock;
  static const bool enabled = [] {
    const char* v = std::getenv("SR_LOG_FPS");
    return v && *v && *v != '0';
  }();
  if (!enabled) return;
  static clock::time_point last_time = clock::now();
  static uint64_t last_frames = 0;
  const auto now = clock::now();
  const double elapsed = std::chrono::duration<double>(now - last_time).count();
  if (elapsed < 2.0) return;
  REXLOG_INFO("guest fps: {:.1f}", (frames - last_frames) / elapsed);
  last_time = now;
  last_frames = frames;
}

}  // namespace

REX_HOOK_RAW(sub_82112050) {
  const uint64_t frames = g_frames.fetch_add(1, std::memory_order_relaxed) + 1;
  MaybeLogFps(frames);
  OnGuestFrameForCapture(frames);
  __imp__sub_82112050(ctx, base);
}

rex::ui::FrameStats SampleGuestFrameStats() {
  using clock = std::chrono::steady_clock;
  static std::mutex mutex;
  static clock::time_point last_time = clock::now();
  static uint64_t last_frames = 0;
  static rex::ui::FrameStats stats;

  std::lock_guard lock(mutex);
  const uint64_t frames = g_frames.load(std::memory_order_relaxed);
  const auto now = clock::now();
  const double elapsed = std::chrono::duration<double>(now - last_time).count();
  if (elapsed >= 0.5) {
    const uint64_t delta = frames - last_frames;
    stats.fps = delta / elapsed;
    stats.frame_time_ms = delta ? elapsed * 1000.0 / delta : 0.0;
    last_time = now;
    last_frames = frames;
  }
  stats.frame_count = frames;
  return stats;
}
