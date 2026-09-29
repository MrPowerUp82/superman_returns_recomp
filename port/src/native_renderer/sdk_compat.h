// Stand-ins for APIs the kit's renderer takes from its ReXGlue fork
// (kit sdk/KIT_SDK_CHANGES.md) and that the stock v0.10.0 SDK lacks.
// Everything here is diagnostics only; none of it changes what is rendered.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <vector>

namespace superman_returns::native::compat {

namespace detail {
inline std::atomic<int64_t> g_first_swap_ns{0};
inline std::mutex g_exit_mutex;
inline std::vector<void (*)()> g_exit_callbacks;
inline int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
inline void RunExitCallbacks() {
  std::vector<void (*)()> callbacks;
  {
    std::lock_guard<std::mutex> lock(g_exit_mutex);
    callbacks.swap(g_exit_callbacks);
  }
  for (auto* callback : callbacks) callback();
}
}  // namespace detail

// Starts the benchmark clock at the first guest swap (fork: same time base
// as its perf CSV). Called from the swap hook.
inline void MarkGuestSwap() {
  int64_t expected = 0;
  detail::g_first_swap_ns.compare_exchange_strong(expected, detail::NowNs());
}

// Milliseconds since the first guest swap; 0 before it (fork: rex::perf::BenchElapsedMs).
inline double BenchElapsedMs() {
  const int64_t first = detail::g_first_swap_ns.load();
  return first ? double(detail::NowNs() - first) / 1e6 : 0.0;
}

// Fork: in-process sampling profiler thread registration. No-op here.
inline void RegisterSampledThread(int, const char*) {}

// Fork: callbacks run before bench_exit_after_s ends the process. Here they
// run at normal process exit (std::atexit), which covers closing the game.
inline void RegisterBenchExitCallback(void (*callback)()) {
  static const bool registered = [] {
    std::atexit(detail::RunExitCallbacks);
    return true;
  }();
  (void)registered;
  std::lock_guard<std::mutex> lock(detail::g_exit_mutex);
  detail::g_exit_callbacks.push_back(callback);
}

}  // namespace superman_returns::native::compat
