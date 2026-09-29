// Backend selection, fallback to Xenos and capture-mode bookkeeping for the
// native renderer (see native_bridge.h and docs/native-port-plan.md).
//
// Not part of the kit: the kit's Conan port selected its backend with a
// native_renderer cvar and had no fallback or capture build. Everything the
// kit's capture (d3d_capture.cpp) needed that depends on Conan's pass table
// or on the kit's SDK fork is replaced by the simpler record here.
#include "native_bridge.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/interfaces/graphics.h>

#include "game_profile.h"
#include "hang_watchdog.h"
#include "native_graphics_system.h"
#include "native_renderer.h"
#include "sdk_compat.h"

REXCVAR_DECLARE(bool, sr_native_ab_mode);

REXCVAR_DEFINE_BOOL(sr_native_capture, false, "Superman Returns Native",
                    "Record what the confirmed D3D hooks see (calls, sample arguments, device "
                    "pointer globals) into sr_native_capture_out; works with any sr_renderer")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(sr_native_capture_out, "native_capture.json", "Superman Returns Native",
                      "JSON written at exit by sr_native_capture")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

#if SR_NATIVE_RENDERER_BUILD
static_assert(superman_returns::native::profile::kProfileConfirmed,
              "SR_NATIVE=RENDERER needs every hook address and the device layout in "
              "port/src/native_renderer/game_profile.h confirmed (docs/native-port-plan.md "
              "section 8). Build with SR_NATIVE=CAPTURE until then.");
#endif

namespace superman_returns::native {

namespace {

enum class Mode : int { kOff, kNative, kAb };
std::atomic<int> g_mode{int(Mode::kOff)};

void LogFallbackOnce(const char* reason) {
  static std::mutex mutex;
  static std::set<std::string> logged;
  std::lock_guard<std::mutex> lock(mutex);
  if (logged.insert(reason).second) {
    REXLOG_WARN("sr_renderer=native: {}; using the xenos backend", reason);
  }
}

// Owns the native graphics system and swaps in the Xenos backend when the
// native one cannot set up D3D12 presentation or the guest GPU. Nothing has
// reached the guest at that point, so the switch is transparent to the game.
class FallbackGraphicsSystem final : public rex::system::IGraphicsSystem {
 public:
  FallbackGraphicsSystem(std::unique_ptr<rex::system::IGraphicsSystem> native,
                         GraphicsSystemFactory create_xenos)
      : active_(std::move(native)), create_xenos_(create_xenos) {}

  rex::X_STATUS SetupPresentation(rex::ui::WindowedAppContext* app_context) override {
    app_context_ = app_context;
    rex::X_STATUS status = active_->SetupPresentation(app_context);
    if (XFAILED(status) && FallBack("D3D12 presentation of the native graphics system failed")) {
      status = active_->SetupPresentation(app_context);
    }
    return status;
  }

  rex::X_STATUS SetupGuestGpu(rex::runtime::FunctionDispatcher* function_dispatcher,
                              rex::system::KernelState* kernel_state) override {
    rex::X_STATUS status = active_->SetupGuestGpu(function_dispatcher, kernel_state);
    if (XFAILED(status) && FallBack("guest GPU setup of the native graphics system failed")) {
      if (app_context_) active_->SetupPresentation(app_context_);
      status = active_->SetupGuestGpu(function_dispatcher, kernel_state);
    }
    return status;
  }

  bool has_presentation() const override { return active_->has_presentation(); }
  rex::ui::GraphicsProvider* provider() const override { return active_->provider(); }
  rex::ui::Presenter* presenter() const override { return active_->presenter(); }
  void SetInterruptCallback(uint32_t callback, uint32_t user_data) override {
    active_->SetInterruptCallback(callback, user_data);
  }
  void InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) override {
    active_->InitializeRingBuffer(ptr, size_log2);
  }
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2) override {
    active_->EnableReadPointerWriteBack(ptr, block_size_log2);
  }
  void InitializeShaderStorage(const std::filesystem::path& cache_root, uint32_t title_id,
                               bool blocking) override {
    active_->InitializeShaderStorage(cache_root, title_id, blocking);
  }
  void Shutdown() override { active_->Shutdown(); }

 private:
  bool FallBack(const char* reason) {
    if (fell_back_ || !create_xenos_) return false;
    fell_back_ = true;
    LogFallbackOnce(reason);
    g_mode.store(int(Mode::kOff));
    active_->Shutdown();
    active_ = create_xenos_();
    return active_ != nullptr;
  }

  std::unique_ptr<rex::system::IGraphicsSystem> active_;
  GraphicsSystemFactory create_xenos_ = nullptr;
  rex::ui::WindowedAppContext* app_context_ = nullptr;
  bool fell_back_ = false;
};

// ---- Capture mode -----------------------------------------------------------

struct RoleRecord {
  uint64_t calls = 0;
  std::vector<std::array<uint32_t, 2>> samples;  // first distinct (r3, r4) pairs
};

std::mutex g_capture_mutex;
std::string g_capture_path;  // read at startup: cvars may be gone at exit
std::map<std::string, RoleRecord> g_roles;
std::map<std::string, std::set<uint32_t>> g_anomalies;
uint64_t g_swaps = 0;
std::set<uint32_t> g_devices;
std::vector<uint32_t> g_device_globals;
bool g_device_scan_done = false;

void WriteCapture() {
  std::lock_guard<std::mutex> lock(g_capture_mutex);
  std::FILE* f = std::fopen(g_capture_path.c_str(), "w");
  if (!f) return;
  std::fprintf(f, "{\n  \"swaps\": %llu,\n  \"profile_confirmed\": %s,\n  \"roles\": [\n",
               (unsigned long long)g_swaps, profile::kProfileConfirmed ? "true" : "false");
  bool first = true;
  for (const auto& hook : profile::kHookRoles) {
    auto it = g_roles.find(hook.role);
    const RoleRecord* r = it == g_roles.end() ? nullptr : &it->second;
    std::fprintf(f,
                 "%s    {\"role\": \"%s\", \"address\": \"%08X\", \"confirmed\": %s, "
                 "\"absent\": %s, \"calls\": %llu, \"calls_per_swap\": %.2f, \"samples\": [",
                 first ? "" : ",\n", hook.role, hook.address, hook.confirmed ? "true" : "false",
                 hook.absent ? "true" : "false", (unsigned long long)(r ? r->calls : 0),
                 r && g_swaps ? double(r->calls) / double(g_swaps) : 0.0);
    if (r) {
      for (size_t i = 0; i < r->samples.size(); ++i) {
        std::fprintf(f, "%s[\"%08X\", \"%08X\"]", i ? ", " : "", r->samples[i][0],
                     r->samples[i][1]);
      }
    }
    std::fprintf(f, "]}");
    first = false;
  }
  std::fprintf(f, "\n  ],\n  \"devices\": [");
  first = true;
  for (uint32_t d : g_devices) {
    std::fprintf(f, "%s\"%08X\"", first ? "" : ", ", d);
    first = false;
  }
  std::fprintf(f, "],\n  \"device_pointer_globals\": [");
  for (size_t i = 0; i < g_device_globals.size(); ++i) {
    std::fprintf(f, "%s\"%08X\"", i ? ", " : "", g_device_globals[i]);
  }
  std::fprintf(f, "],\n  \"anomalies\": [");
  first = true;
  for (const auto& [what, values] : g_anomalies) {
    std::fprintf(f, "%s\n    {\"what\": \"%s\", \"values\": [", first ? "" : ",", what.c_str());
    bool first_value = true;
    for (uint32_t v : values) {
      std::fprintf(f, "%s\"%08X\"", first_value ? "" : ", ", v);
      first_value = false;
    }
    std::fprintf(f, "]}");
    first = false;
  }
  std::fprintf(f, "\n  ]\n}\n");
  std::fclose(f);
}

// Image data words equal to the device pointer: candidates for the global
// Conan's renderer read (kDevicePtrAddr). Run once, on the swap thread.
void ScanDeviceGlobals(const uint8_t* base, uint32_t dev) {
  const uint32_t be = __builtin_bswap32(dev);
  for (uint32_t a = profile::kImageBase; a + 4 <= profile::kImageEnd && g_device_globals.size() < 32;
       a += 4) {
    uint32_t v;
    std::memcpy(&v, base + a, 4);
    if (v == be) g_device_globals.push_back(a);
  }
}

}  // namespace

bool RendererActive() {
  const int mode = g_mode.load(std::memory_order_relaxed);
  return mode == int(Mode::kNative) || mode == int(Mode::kAb);
}

bool CaptureActive() {
  static const bool capture = [] {
    if (!REXCVAR_GET(sr_native_capture)) return false;
    g_capture_path = REXCVAR_GET(sr_native_capture_out);
    compat::RegisterBenchExitCallback(WriteCapture);
    REXLOG_INFO("sr_native_capture: recording hook calls to '{}'", g_capture_path);
    return true;
  }();
  return capture;
}

void NoteHookCall(const char* role, uint32_t r3, uint32_t r4) {
  if (!CaptureActive()) return;
  std::lock_guard<std::mutex> lock(g_capture_mutex);
  RoleRecord& r = g_roles[role];
  ++r.calls;
  if (r.samples.size() < 8) {
    const std::array<uint32_t, 2> sample{r3, r4};
    if (std::find(r.samples.begin(), r.samples.end(), sample) == r.samples.end()) {
      r.samples.push_back(sample);
    }
  }
}

void LogCaptureAnomalyOnce(const char* what, uint32_t value) {
  {
    std::lock_guard<std::mutex> lock(g_capture_mutex);
    auto& values = g_anomalies[what];
    if (values.size() >= 16 || !values.insert(value).second) return;
  }
  REXLOG_WARN("native hooks: {} ({:08X}); check game_profile.h", what, value);
}

void OnFrameStatsSwap(uint8_t* base, uint32_t dev, uint32_t front_buffer) {
#if SR_HOOK_ENABLED(SWAP) && SR_HEX(SR_ADDR_SWAP) == SR_FRAME_STATS_SWAP_HOOK
  static uint64_t swap_number = 0;
  ++swap_number;
  compat::MarkGuestSwap();
  NoteGuestDevice(dev);
  NoteHookCall("Swap", dev, front_buffer);
  if (CaptureActive()) {
    std::lock_guard<std::mutex> lock(g_capture_mutex);
    ++g_swaps;
    if (dev) g_devices.insert(dev);
    // A few seconds in: the device and its globals exist by then.
    if (!g_device_scan_done && dev && g_swaps == 120) {
      g_device_scan_done = true;
      ScanDeviceGlobals(base, dev);
    }
  }
  if (RendererActive()) {
    HangWatchdogBeat();
    Renderer::Get().OnSwap(base, front_buffer, swap_number);
  }
#else
  (void)base;
  (void)dev;
  (void)front_buffer;
#endif
}

std::unique_ptr<rex::system::IGraphicsSystem> CreateNativeGraphicsSystem(
    GraphicsSystemFactory create_xenos, GraphicsSystemFactory create_xenos_ab) {
#if !SR_NATIVE_RENDERER_BUILD
  (void)create_xenos;
  (void)create_xenos_ab;
  LogFallbackOnce("this build only has the capture hooks (CMake SR_NATIVE=CAPTURE)");
  return nullptr;
#else
  if constexpr (!profile::kProfileConfirmed) {
    LogFallbackOnce("game_profile.h has unconfirmed addresses");
    return nullptr;
  }
  std::string where;
  if (!ShaderCorpusAvailable(&where)) {
    LogFallbackOnce(("no offline shader corpus (embedded pack or " + where +
                     "; tools/shaders, docs/native-port-plan.md section 6)")
                        .c_str());
    return nullptr;
  }
  if (REXCVAR_GET(sr_native_ab_mode)) {
    if (!create_xenos_ab) {
      LogFallbackOnce("sr_native_ab_mode needs the in-process Xenos backend");
      return nullptr;
    }
    REXLOG_INFO("sr_renderer=native: A/B mode, Xenos renders and presents, native renders "
                "offscreen ({})", where);
    g_mode.store(int(Mode::kAb));
    return create_xenos_ab();
  }
  REXLOG_INFO("sr_renderer=native: native graphics system, no Xenos emulation ({})", where);
  g_mode.store(int(Mode::kNative));
  return std::make_unique<FallbackGraphicsSystem>(std::make_unique<NativeGraphicsSystem>(),
                                                  create_xenos);
#endif
}

}  // namespace superman_returns::native
