#include "title_benchmark.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>

#if defined(SR_PGO_GENERATE)
extern "C" int __llvm_profile_write_file(void);
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(sr_benchmark, false, "Superman Returns",
                    "Title-screen benchmark: skip the intro, hide the title UI, measure and report")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(sr_benchmark_exit, false, "Superman Returns",
                    "Close after saving the benchmark result (exit code 0 valid, 1 invalid)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(sr_benchmark_headless, false, "Superman Returns",
                    "Benchmark without a visible window: the window is moved off-screen without "
                    "focus, the run closes after saving and prints its summary to stdout")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(sr_window_offscreen, false, "Superman Returns",
                    "Development: move the game window off-screen and give the focus back once frames "
                    "are presented (automated runs that should not disturb the desktop)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_DOUBLE(sr_benchmark_warmup_s, 5.0, "Superman Returns",
                      "Diagnostic: benchmark warm-up seconds after the title scene appears")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_DOUBLE(sr_benchmark_measure_s, 60.0, "Superman Returns",
                      "Diagnostic: benchmark measurement seconds")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(sr_benchmark_cache_route, "title", "Superman Returns",
    "Diagnostic cache evidence route token for the title benchmark")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(sr_native_cache_audit_commands, "", "Superman Returns",
    "Diagnostic: consume begin/end route tokens from this command file during gameplay");

namespace superman_returns::benchmark {
namespace {
using SteadyClock = std::chrono::steady_clock;
std::atomic<CacheAuditProvider> cache_audit_provider{nullptr};

// Independent of the title clock; the harness separately proves the gameplay route.
void PollObservedCacheRoute() {
  const auto path=REXCVAR_GET(sr_native_cache_audit_commands);if(path.empty())return;
  static auto next=SteadyClock::time_point{};
  const auto now=SteadyClock::now();if(now<next)return;next=now+std::chrono::milliseconds(100);
  static CacheAuditProvider provider=nullptr;static CacheAuditCounts before{};static std::string route;
  std::ifstream file(std::filesystem::u8path(path));if(!file)return;
  std::string command,name,extra;file>>command>>name>>extra;file.close();
  std::error_code error;std::filesystem::remove(std::filesystem::u8path(path),error);
  if(error || name.empty() || !extra.empty() || name.size()>96 ||
      name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)return;
  if(command=="begin") {
    if(provider) {REXLOG_WARN("native cache audit: route_invalid name={} reason=nested_begin",route);provider=nullptr;}
    auto current=cache_audit_provider.load(std::memory_order_acquire);
    if(!current) {REXLOG_WARN("native cache audit: route_invalid name={} reason=missing_provider",name);return;}
    route=name;provider=current;before=provider();
    REXLOG_INFO("native cache audit: route_begin name={}",route);
  } else if(command=="end" && provider && name==route) {
    const auto after=provider();
    const bool valid=cache_audit_provider.load(std::memory_order_acquire)==provider &&
        after.frames>before.frames && after.draws>before.draws &&
        after.pso_runtime_creates>=before.pso_runtime_creates &&
        after.shader_runtime_compiles>=before.shader_runtime_compiles && after.shader_misses>=before.shader_misses;
    if(valid) REXLOG_INFO("native cache audit: route_end name={} frames={} draws={} pso_runtime_creates={} shader_runtime_compiles={} shader_misses={} valid=1",
        route,after.frames-before.frames,after.draws-before.draws,after.pso_runtime_creates-before.pso_runtime_creates,
        after.shader_runtime_compiles-before.shader_runtime_compiles,after.shader_misses-before.shader_misses);
    else REXLOG_WARN("native cache audit: route_invalid name={} reason=provider_or_counter_failure",route);
    provider=nullptr;route.clear();
  }
}

struct State {
  std::mutex mutex;
  std::optional<Clock> clock;
  std::optional<SteadyClock::time_point> first_present;
  Result result;
  std::string gpu_name;
  bool finished = false;
  CacheAuditProvider audit_provider = nullptr;
  CacheAuditCounts audit_begin{};
  std::string audit_name;
};
State& GetState() {
  static State state;
  return state;
}
SceneLatch scene_latch;

std::string SettingsText(const std::string& gpu) {
  const auto flag = [](const char* name) { return rex::cvar::GetFlagByName(name); };
  return "API " + flag("sr_native_api") + " | GPU " + (gpu.empty() ? "desconhecida" : gpu) +
         " | janela " + flag("window_width") + "x" + flag("window_height") +
         " | escala " + flag("sr_native_render_scale") + " | sombra " + flag("sr_native_shadow_quality") +
         " | MSAA " + flag("sr_native_msaa_samples") + " | VSync " + flag("vsync") +
         " | limite FPS " + flag("sr_native_fps_limit") + " | build " __DATE__ " " __TIME__;
}

std::filesystem::path ResultDirectory() {
#if defined(_WIN32)
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length && length < MAX_PATH) return std::filesystem::path(path).parent_path() / "benchmarks";
#endif
  return std::filesystem::current_path() / "benchmarks";
}

std::string Stamp() {
  const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char text[32];
  std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &local);
  return text;
}

#if defined(_WIN32)
// Window in front when the game started (the terminal or app that launched it).
HWND foreground_at_start = nullptr;
#endif

// Headless: move this process's windows off-screen and hand the keyboard focus
// back to the window that had it at startup. The window stays shown (a hidden
// or minimized one would stop the presenter's UI paint the swap thread waits
// on), so rendering continues normally. A detached thread does the work: the
// focus change sends messages to the UI thread, which must never block the
// guest thread that called this.
void MoveWindowsOffscreen() {
#if defined(_WIN32)
  std::thread([previous = foreground_at_start] {
    EnumWindows([](HWND window, LPARAM) -> BOOL {
      DWORD process = 0;
      GetWindowThreadProcessId(window, &process);
      if (process == GetCurrentProcessId() && IsWindowVisible(window))
        SetWindowPos(window, nullptr, -32000, -32000, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
      return TRUE;
    }, 0);
    if (previous && IsWindow(previous)) SetForegroundWindow(previous);
  }).detach();
#endif
}

void Finish(State& s) {
  auto& r = s.result;
  r.phase = Phase::Done;
  r.valid = s.clock->valid();
  r.reason = s.clock->reason();
  r.stats = ComputeStats(s.clock->frame_ms());
  r.settings_text = SettingsText(s.gpu_name);
  if(s.audit_provider) {
    const auto after=s.audit_provider();
    const auto& before=s.audit_begin;
    const bool monotonic=after.frames>=before.frames && after.draws>=before.draws &&
        after.pso_runtime_creates>=before.pso_runtime_creates &&
        after.shader_runtime_compiles>=before.shader_runtime_compiles && after.shader_misses>=before.shader_misses;
    if(monotonic) {
      REXLOG_INFO("native cache audit: route_end name={} frames={} draws={} pso_runtime_creates={} "
                  "shader_runtime_compiles={} shader_misses={} valid={}",s.audit_name,
          after.frames-before.frames,after.draws-before.draws,
          after.pso_runtime_creates-before.pso_runtime_creates,
          after.shader_runtime_compiles-before.shader_runtime_compiles,
          after.shader_misses-before.shader_misses,r.valid?1:0);
    } else {
      REXLOG_WARN("native cache audit: route_invalid name={} reason=counter_reset",s.audit_name);
    }
  }
  SaveResult(r, ResultDirectory(), Stamp(), s.clock->frame_ms());
  REXLOG_INFO("benchmark: {} avg_fps={:.1f} min_fps={:.1f} low1_fps={:.1f} low01_fps={:.1f} frames={} seconds={:.1f}{}{} | {} | {}",
              r.valid ? "valid" : "invalid", r.stats.avg_fps, r.stats.min_fps, r.stats.low1_fps,
              r.stats.low01_fps, r.stats.frames, r.stats.seconds, r.reason.empty() ? "" : " reason=",
              r.reason, r.settings_text, r.saved ? r.json_path : std::string("not saved"));
  if (REXCVAR_GET(sr_benchmark_headless)) {
    // Scripts read the result without opening the JSON (stdout must be redirected:
    // the game has no console of its own).
    std::printf("benchmark %s avg_fps=%.1f min_fps=%.1f low1_fps=%.1f low01_fps=%.1f frames=%u seconds=%.1f json=%s%s%s\n",
                r.valid ? "valid" : "invalid", r.stats.avg_fps, r.stats.min_fps, r.stats.low1_fps, r.stats.low01_fps,
                r.stats.frames, r.stats.seconds, r.saved ? r.json_path.c_str() : "-", r.reason.empty() ? "" : " reason=",
                r.reason.c_str());
    std::fflush(stdout);
  }
}
}  // namespace

bool Active() { return REXCVAR_GET(sr_benchmark); }
void SetCacheAuditProvider(CacheAuditProvider provider) {
  cache_audit_provider.store(provider,std::memory_order_release);
}

void NoteStartup() {
#if defined(_WIN32)
  if ((Active() && REXCVAR_GET(sr_benchmark_headless)) || REXCVAR_GET(sr_window_offscreen))
    foreground_at_start = GetForegroundWindow();
#endif
}

void NoteRendererFrame(bool scene_drawn) { scene_latch.Note(scene_drawn); }

void OnGuestPresent() {
  PollObservedCacheRoute();
  static std::atomic<bool> moved{false};
  if (REXCVAR_GET(sr_window_offscreen) && !moved.exchange(true)) MoveWindowsOffscreen();
  if (!Active()) return;
  auto& s = GetState();
  bool exit_now = false;
  bool valid = false;
  {
    std::lock_guard lock(s.mutex);
    if (s.finished) return;
    const auto now = SteadyClock::now();
    if (!s.first_present) {
      s.first_present = now;
      if (REXCVAR_GET(sr_benchmark_headless)) MoveWindowsOffscreen();
      s.clock.emplace(REXCVAR_GET(sr_benchmark_warmup_s), REXCVAR_GET(sr_benchmark_measure_s));
    }
    const double t = std::chrono::duration<double>(now - *s.first_present).count();
    const Phase before = s.clock->phase();
    s.clock->OnFrame(t, scene_latch.Consume());
    s.result.phase = s.clock->phase();
    if(s.clock->phase()==Phase::Measuring && before!=Phase::Measuring) {
      s.audit_provider=cache_audit_provider.load(std::memory_order_acquire);
      if(s.audit_provider) {
        s.audit_name=REXCVAR_GET(sr_benchmark_cache_route);
        if(s.audit_name.empty())s.audit_name="title";
        for(auto& c:s.audit_name)if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
            (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.'))c='_';
        s.audit_begin=s.audit_provider();
        REXLOG_INFO("native cache audit: route_begin name={}",s.audit_name);
      } else {
        REXLOG_WARN("native cache audit: missing_route_provider; zero misses cannot be certified");
      }
    }
    if (s.clock->phase() != before && s.clock->phase() != Phase::Done)
      REXLOG_INFO("benchmark: phase {} at {:.1f} s", int(s.clock->phase()), t);
    if (s.clock->phase() == Phase::Done) {
      s.finished = true;
      Finish(s);
      exit_now = ExitAfterSave(REXCVAR_GET(sr_benchmark_exit), REXCVAR_GET(sr_benchmark_headless));
      valid = s.result.valid;
    }
  }
  if (exit_now) ExitNow(valid ? 0 : 1);
}

void ExitNow(int code) {
#if defined(SR_PGO_GENERATE)
  // TerminateProcess/_Exit deliberately bypass atexit. Flush training counters
  // after results are finalized; profile-use and normal builds omit this call.
  const int profile_status=__llvm_profile_write_file();
  REXLOG_INFO("benchmark: LLVM PGO profile write status={}",profile_status);
#endif
  rex::FlushLogging();
#if defined(_WIN32)
  TerminateProcess(GetCurrentProcess(), UINT(code));
#endif
  std::_Exit(code);
}

void NoteVideoStarted() {
  if (!Active()) return;
  auto& s = GetState();
  bool exit_now = false, valid = false;
  {
    std::lock_guard lock(s.mutex);
    if (s.finished || !s.clock) return;
    s.clock->Interrupt();
    if (s.clock->phase() != Phase::Done) return;
    REXLOG_INFO("benchmark: a video started during the run");
    s.finished = true;
    Finish(s);
    exit_now = ExitAfterSave(REXCVAR_GET(sr_benchmark_exit), REXCVAR_GET(sr_benchmark_headless));
    valid = s.result.valid;
  }
  if (exit_now) ExitNow(valid ? 0 : 1);
}

void SetGpuName(std::string name) {
  auto& s = GetState();
  std::lock_guard lock(s.mutex);
  s.gpu_name = std::move(name);
}

Result Snapshot() {
  auto& s = GetState();
  std::lock_guard lock(s.mutex);
  return s.result;
}

}  // namespace superman_returns::benchmark
