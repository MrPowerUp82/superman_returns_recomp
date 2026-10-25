#include "graphics_options.h"
#include <stdexcept>

namespace superman_returns::graphics {
GraphicsGuestPauseSample ReadGraphicsGuestPause(const GraphicsGuestRead32& read) {
  if(!read) return {};
  const auto context=read(kGraphicsGuestContextRoot);
  if(!context || *context<0x10000 || (*context&3) ||
     *context>UINT32_MAX-kGraphicsGuestStateOffset-4) return {};
  const auto state=read(*context+kGraphicsGuestStateOffset);
  if(!state) return {};
  return {true,*state==kGraphicsGuestPausedState,*context,*state};
}
GuestPauseContract GraphicsGuestPauseTracker::Observe(GraphicsGuestPauseSample sample,
    bool entry_selected,bool input_captured) {
  std::lock_guard lock(mutex_);
  const bool paused=sample.valid && sample.paused && sample.context;
  if(paused && (!paused_ || sample.context!=context_)) {
    if(++session_==0) ++session_;
  }
  context_=sample.valid ? sample.context : 0;
  paused_=paused;
  return {session_,paused,entry_selected,input_captured};
}
MenuInputSample SampleGraphicsGamepad(const GraphicsGamepadState& pad) {
  constexpr int kDeadzone = 7849;
  constexpr int kTriggerThreshold = 30;
  auto displaced = [](int axis) { return axis > kDeadzone || axis < -kDeadzone; };
  MenuInputSample sample;
  if ((pad.buttons & 0x0001) || pad.left_y > kDeadzone) sample.down |= kMenuUp;
  if ((pad.buttons & 0x0002) || pad.left_y < -kDeadzone) sample.down |= kMenuDown;
  if ((pad.buttons & 0x0004) || pad.left_x < -kDeadzone) sample.down |= kMenuLeft;
  if ((pad.buttons & 0x0008) || pad.left_x > kDeadzone) sample.down |= kMenuRight;
  if (pad.buttons & 0x2000) sample.down |= kMenuBack;
  sample.any_guest_input_down = pad.buttons || displaced(pad.left_x) || displaced(pad.left_y) ||
      displaced(pad.right_x) || displaced(pad.right_y) ||
      pad.left_trigger > kTriggerThreshold || pad.right_trigger > kTriggerThreshold;
  return sample;
}
namespace {
bool ValidValue(size_t row, int value) {
  return row < kGraphicsOptionCount && value >= 0 &&
      value <= (row == static_cast<size_t>(GraphicsOption::kShadowSoftness) ? 2 : 1);
}
bool CanOpen(const GuestPauseContract& pause) {
  return pause.session && pause.guest_paused && pause.graphics_entry_selected &&
      pause.guest_input_captured;
}
}

GraphicsOptionsSnapshot GraphicsOptions::Snapshot() const {
  std::lock_guard lock(mutex_);
  return state_;
}

bool GraphicsOptions::RestorePreferences(const GraphicsValues& values) {
  for(size_t i=0;i<values.size();++i)
    if(!ValidValue(i,values[i]) || (i==1 && !values[i])) return false;
  std::lock_guard lock(mutex_);
  if(state_.requested!=values) {state_.requested=values; ++state_.request_revision;}
  return true;
}

bool GraphicsOptions::Request(GraphicsOption option, int value) {
  const auto row = static_cast<size_t>(option);
  if (!ValidValue(row, value)) return false;
  std::lock_guard lock(mutex_);
  const auto& support = state_.features[row];
  if(option==GraphicsOption::kShadowSoftness &&
     (!value || state_.applied[static_cast<size_t>(GraphicsOption::kRtShadows)]!=1)) return false;
  if (value && !(support.implemented && support.capable)) return false;
  if (state_.requested[row] == value &&
      (state_.pending() || state_.applied[row] == value)) return true;
  state_.requested[row] = value;
  ++state_.request_revision;
  return true;
}

uint64_t GraphicsOptions::BeginBackend(NativeApi api) {
  std::lock_guard lock(mutex_);
  state_.api = api;
  ++state_.backend_epoch;
  state_.completed_revision = 0;
  state_.applied_frame = 0;
  state_.features = {};
  state_.applied = {};
  state_.failure.clear();
  return state_.backend_epoch;
}

bool GraphicsOptions::PublishBackend(uint64_t epoch, const GraphicsBackendReport& report) {
  std::lock_guard lock(mutex_);
  if (!epoch || epoch != state_.backend_epoch || report.frame <= state_.applied_frame ||
      report.request_revision > state_.request_revision ||
      report.request_revision < state_.completed_revision) return false;
  for (size_t row=0; row<kGraphicsOptionCount; ++row) {
    if (!ValidValue(row, report.applied[row]) ||
        (report.applied[row] && !(report.features[row].implemented && report.features[row].capable)))
      return false;
  }
  state_.features = report.features;
  state_.applied = report.applied;
  state_.completed_revision = report.request_revision;
  state_.applied_frame = report.frame;
  state_.failure = report.failure;
  bool unapplied=false;
  for(size_t row=0;row<kGraphicsOptionCount;++row) {
    if(row==static_cast<size_t>(GraphicsOption::kShadowSoftness) && !state_.applied[0]) continue;
    unapplied|=state_.applied[row]!=state_.requested[row];
  }
  if (!state_.pending() && unapplied && state_.failure.empty())
    state_.failure = "Backend did not apply all requested options";
  return true;
}

std::string_view GraphicsOptionLabel(GraphicsOption option) {
  constexpr std::array<std::string_view, kGraphicsOptionCount> labels{
      "RT Shadows", "Shadow Softness", "Character Shadows", "RT Reflections", "DLSS Super Resolution"};
  const auto row = static_cast<size_t>(option);
  return row < labels.size() ? labels[row] : "Unknown";
}

std::string_view GraphicsValueLabel(GraphicsOption option, int value) {
  if (!ValidValue(static_cast<size_t>(option),value)) return "Invalid";
  if (!value) return "Off";
  if (option == GraphicsOption::kShadowSoftness) return value == 1 ? "Low" : "High";
  return option == GraphicsOption::kDlss ? "Quality" : "On";
}

GraphicsOptionsMenu::GraphicsOptionsMenu(std::shared_ptr<GraphicsOptions> options)
    : options_(std::move(options)) {
  if (!options_) throw std::invalid_argument("GraphicsOptionsMenu requires shared options");
}

bool GraphicsOptionsMenu::Open(const GuestPauseContract& pause) {
  std::lock_guard lock(mutex_);
  if (state_.capture_guest_input || !CanOpen(pause)) return false;
  state_ = {true, true, pause.session, 0};
  armed_ = false;
  previous_ = 0;
  return true;
}

void GraphicsOptionsMenu::ObservePause(const GuestPauseContract& pause) {
  std::lock_guard lock(mutex_);
  if (state_.open && (pause.session != state_.pause_session || !pause.guest_paused ||
                     !pause.guest_input_captured)) state_.open = false;
}

void GraphicsOptionsMenu::SetInput(MenuInputSource source, MenuInputSample sample) {
  const auto index = static_cast<size_t>(source);
  if (index >= inputs_.size()) return;
  std::lock_guard lock(mutex_);
  inputs_[index] = sample;
}

void GraphicsOptionsMenu::TickInput() {
  std::lock_guard lock(mutex_);
  uint32_t down = 0;
  bool held = false;
  for (const auto& sample : inputs_) {
    down |= sample.down;
    held |= sample.any_guest_input_down || sample.down != 0;
  }
  const auto pressed = down & ~previous_;
  previous_ = down;
  if (!state_.open) {
    if (!held) state_.capture_guest_input = false;
    return;
  }
  if (!armed_) {
    if (!held) armed_ = true;
    return;
  }
  if (pressed & kMenuBack) { state_.open = false; return; }
  if ((pressed & (kMenuUp|kMenuDown)) == kMenuUp)
    state_.selected = (state_.selected+kGraphicsOptionCount-1)%kGraphicsOptionCount;
  else if ((pressed & (kMenuUp|kMenuDown)) == kMenuDown)
    state_.selected = (state_.selected+1)%kGraphicsOptionCount;
  if ((pressed & (kMenuLeft|kMenuRight)) == kMenuLeft) AdjustLocked(-1);
  else if ((pressed & (kMenuLeft|kMenuRight)) == kMenuRight) AdjustLocked(1);
}

void GraphicsOptionsMenu::Close() {
  std::lock_guard lock(mutex_);
  state_.open = false;
}
void GraphicsOptionsMenu::Select(size_t row) {
  std::lock_guard lock(mutex_);
  if (state_.open && armed_ && row < kGraphicsOptionCount) state_.selected = row;
}
void GraphicsOptionsMenu::Adjust(int direction) {
  std::lock_guard lock(mutex_);
  AdjustLocked(direction);
}
void GraphicsOptionsMenu::AdjustLocked(int direction) {
  if (!state_.open || !armed_ || !direction) return;
  const auto snapshot = options_->Snapshot();
  const auto row = state_.selected;
  const auto option = static_cast<GraphicsOption>(row);
  if(option==GraphicsOption::kShadowSoftness && snapshot.applied[0]!=1) return;
  auto next = snapshot.requested[row] + (direction > 0 ? 1 : -1);
  if (direction < 0 && !(snapshot.features[row].implemented && snapshot.features[row].capable))
    next = 0;
  // Clamp at endpoints: Left never accidentally switches Off to On.
  if (ValidValue(row,next)) options_->Request(option,next);
}
bool GraphicsOptionsMenu::CaptureGuestInput() const {
  std::lock_guard lock(mutex_);
  return state_.capture_guest_input;
}
GraphicsMenuSnapshot GraphicsOptionsMenu::Snapshot() const {
  std::lock_guard lock(mutex_);
  return state_;
}
}
