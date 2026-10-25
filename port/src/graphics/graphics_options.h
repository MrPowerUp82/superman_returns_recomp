#pragma once

#include "backend_selection.h"
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace superman_returns::graphics {

enum class GraphicsOption : uint8_t {
  kRtShadows, kShadowSoftness, kCharacterShadows, kRtReflections, kDlss, kCount
};
constexpr size_t kGraphicsOptionCount = static_cast<size_t>(GraphicsOption::kCount);
// Boolean rows: 0 Off, 1 On. Softness preference: 1 Low, 2 High (0 is inactive backend only).
// DLSS: 0 Off, 1 Quality. These are requests, never proof of SDK/DXR execution.
using GraphicsValues = std::array<int, kGraphicsOptionCount>;
struct GraphicsFeatureSupport {
  bool implemented = false;
  bool capable = false;
  std::string reason = "Backend has not reported support";
};
struct GraphicsBackendReport {
  uint64_t request_revision = 0;
  uint64_t frame = 0;
  std::array<GraphicsFeatureSupport, kGraphicsOptionCount> features{};
  GraphicsValues applied{};
  std::string failure;
};
struct GraphicsOptionsSnapshot {
  NativeApi api = NativeApi::kD3D12;
  uint64_t backend_epoch = 0, request_revision = 0, completed_revision = 0;
  uint64_t applied_frame = 0;
  GraphicsValues requested{0,1,0,0,0}, applied{};
  std::array<GraphicsFeatureSupport, kGraphicsOptionCount> features{};
  std::string failure;
  bool pending() const { return request_revision != completed_revision; }
};

// Shared by UI, renderer and settings adapter. No guest memory, ReXGlue, GPU
// calls or cvars inside the lock. Snapshots own their strings and arrays.
// Integration contract:
//  * Backend/device creation: BeginBackend(api), keep the returned epoch.
//  * At a safe frame boundary: Snapshot(), apply its requested values, then
//    PublishBackend(epoch, report) with that snapshot's request_revision.
//    Report actual output, even when a newer UI request arrived meanwhile.
//    Frame numbers increase within an epoch. Set failure on failed application;
//    never report applied On from capability checks alone. Refresh on loss.
//  * UI/cvars/launcher: Request(one field). Persist Snapshot().requested only;
//    merging individual fields avoids overwriting other writers. Persistence
//    and GPU execution belong to external adapters, not this component.
class GraphicsOptions {
 public:
  GraphicsOptionsSnapshot Snapshot() const;
  bool Request(GraphicsOption option, int value);
  // Persistence/startup only: retain user intent across device changes without
  // claiming support or application. Interactive edits must use Request.
  bool RestorePreferences(const GraphicsValues& values);
  uint64_t BeginBackend(NativeApi api);
  bool PublishBackend(uint64_t epoch, const GraphicsBackendReport& report);
 private:
  mutable std::mutex mutex_;
  GraphicsOptionsSnapshot state_;
};

std::string_view GraphicsOptionLabel(GraphicsOption option);
std::string_view GraphicsValueLabel(GraphicsOption option, int value);

// This is an attestation by a VERIFIED guest adapter, not a pause detector.
// No adapter is installed here: do not populate these booleans from Start/Esc,
// an overlay's visibility, a guessed address, or a host thread suspension.
// session identifies the existing guest pause instance, not an image pointer.
struct GuestPauseContract {
  uint64_t session = 0;
  bool guest_paused = false;
  bool graphics_entry_selected = false;
  bool guest_input_captured = false;
};
// Verified by loaded image + registration at sub_822AD978:
// PauseGame=822A0F38, UnPauseGame=822A0F70, IsPaused=822A0FA8.
// The last calls 8214CEA0 (*context+72) and compares the result with 4.
// Read on the guest thread, in the same lifetime as the context. The reader
// must validate mappings and return a HOST-ENDIAN word, or nullopt on failure.
constexpr uint32_t kGraphicsGuestContextRoot = 0x82911460;
constexpr uint32_t kGraphicsGuestStateOffset = 72;
constexpr uint32_t kGraphicsGuestPausedState = 4;
struct GraphicsGuestPauseSample {
  bool valid=false, paused=false;
  uint32_t context=0, state=0;
};
using GraphicsGuestRead32 = std::function<std::optional<uint32_t>(uint32_t)>;
GraphicsGuestPauseSample ReadGraphicsGuestPause(const GraphicsGuestRead32& read);
// Supply Observe result to menu.ObservePause. Only set entry_selected from an
// actual menu activation; recognizing the paused state is not that activation.
class GraphicsGuestPauseTracker {
 public:
  GuestPauseContract Observe(GraphicsGuestPauseSample sample, bool entry_selected,
                             bool guest_input_captured);
 private:
  std::mutex mutex_;
  uint64_t session_=0;
  uint32_t context_=0;
  bool paused_=false;
};
enum class MenuInputSource : uint8_t { kKeyboard, kGamepad };
enum MenuButton : uint32_t {
  kMenuUp=1, kMenuDown=2, kMenuLeft=4, kMenuRight=8, kMenuBack=16
};
struct MenuInputSample {
  uint32_t down = 0;
  // Include ALL held guest controls (axes/triggers/non-navigation buttons).
  // A disconnected device must publish a neutral sample.
  bool any_guest_input_down = false;
};
// Matches X_INPUT_GAMEPAD/XInput fields without depending on the SDK.
struct GraphicsGamepadState {
  uint16_t buttons = 0;
  int16_t left_x = 0, left_y = 0, right_x = 0, right_y = 0;
  uint8_t left_trigger = 0, right_trigger = 0;
};
MenuInputSample SampleGraphicsGamepad(const GraphicsGamepadState& pad);
struct GraphicsMenuSnapshot {
  bool open = false, capture_guest_input = false;
  uint64_t pause_session = 0;
  size_t selected = 0;
};

// Thread-safe input/session component. The integrator must capture ALL input
// paths before Open and while CaptureGuestInput(), including GetState and
// GetKeystroke, keyboard, mouse and synthetic input. Feed physical input to
// SetInput BEFORE filtering the guest copy. ImGui WantCapture* alone cannot
// block polled gamepads. Call ObservePause on every guest pause transition.
// TickInput once per UI frame, including while closed until capture drains.
// Keep this object alive beyond its dialog. It never pauses/resumes the guest;
// closing restores control to the SAME pause after every control is released.
class GraphicsOptionsMenu {
 public:
  explicit GraphicsOptionsMenu(std::shared_ptr<GraphicsOptions> options);
  bool Open(const GuestPauseContract& pause);
  void ObservePause(const GuestPauseContract& pause);
  void SetInput(MenuInputSource source, MenuInputSample sample);
  void TickInput();
  void Close();
  void Select(size_t row);
  void Adjust(int direction);
  bool CaptureGuestInput() const;
  GraphicsMenuSnapshot Snapshot() const;
  std::shared_ptr<GraphicsOptions> options() const { return options_; }
 private:
  void AdjustLocked(int direction);
  mutable std::mutex mutex_;
  const std::shared_ptr<GraphicsOptions> options_;
  GraphicsMenuSnapshot state_;
  std::array<MenuInputSample, 2> inputs_{};
  uint32_t previous_ = 0;
  bool armed_ = false;
};

} // namespace superman_returns::graphics
