#include "graphics_options_runtime.h"
namespace superman_returns::graphics {
namespace {
struct RuntimeState {
  std::shared_ptr<GraphicsOptions> options=std::make_shared<GraphicsOptions>();
  std::shared_ptr<GraphicsOptionsMenu> menu=std::make_shared<GraphicsOptionsMenu>(options);
  GraphicsGuestPauseTracker tracker;
  std::mutex mutex;
  GuestPauseContract pause;
  bool previous_y=false;
  bool y_armed=false;
};
RuntimeState& State() { static RuntimeState state; return state; }
}
std::shared_ptr<GraphicsOptions> SharedGraphicsOptions() { return State().options; }
std::shared_ptr<GraphicsOptionsMenu> SharedGraphicsOptionsMenu() { return State().menu; }
void ObserveGraphicsGuestPause(GraphicsGuestPauseSample sample) {
  auto& s=State();
  const auto pause=s.tracker.Observe(sample,false,true);
  {std::lock_guard lock(s.mutex);
    if(!pause.guest_paused || pause.session!=s.pause.session) {
      s.previous_y=false; s.y_armed=false;
    }
    s.pause=pause;
  }
  s.menu->ObservePause(pause);
}
GuestPauseContract CurrentGraphicsGuestPause() {
  auto& s=State(); std::lock_guard lock(s.mutex); return s.pause;
}
bool OpenGraphicsOptionsFromPause() {
  auto pause=CurrentGraphicsGuestPause();
  pause.graphics_entry_selected=true;
  pause.guest_input_captured=true;
  return State().menu->Open(pause);
}
void ObserveGraphicsGamepad(const GraphicsGamepadState& pad) {
  auto& s=State();
  s.menu->SetInput(MenuInputSource::kGamepad,SampleGraphicsGamepad(pad));
  const bool y=(pad.buttons&0x8000)!=0;
  bool pressed=false;
  {std::lock_guard lock(s.mutex);
    if(!y && s.pause.guest_paused) s.y_armed=true;
    pressed=s.pause.guest_paused && s.y_armed && y && !s.previous_y;
    s.previous_y=y;
  }
  if(pressed && !s.menu->Snapshot().open) OpenGraphicsOptionsFromPause();
}
void ClearGraphicsGuestPause() { ObserveGraphicsGuestPause({}); }
}
