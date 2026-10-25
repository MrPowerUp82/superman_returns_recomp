#include "test_main.h"
#include "../../port/src/graphics/graphics_options.h"
#include "../../port/src/graphics/graphics_options_runtime.h"
#include <map>
using namespace superman_returns::graphics;

SR_TEST(graphics_options_runtime_opens_only_from_actual_pause_and_returns_to_it) {
  ClearGraphicsGuestPause();
  auto menu=SharedGraphicsOptionsMenu();
  menu->SetInput(MenuInputSource::kGamepad,{});
  menu->SetInput(MenuInputSource::kKeyboard,{});
  menu->TickInput();
  SR_CHECK(!OpenGraphicsOptionsFromPause());
  ObserveGraphicsGuestPause({true,true,0x83003000,4});
  ObserveGraphicsGamepad({});
  const auto pause=CurrentGraphicsGuestPause();
  SR_CHECK(OpenGraphicsOptionsFromPause());
  menu->TickInput();
  menu->SetInput(MenuInputSource::kKeyboard,{kMenuBack,true});
  menu->TickInput();
  SR_CHECK(!menu->Snapshot().open);
  SR_CHECK(menu->CaptureGuestInput());
  SR_CHECK(CurrentGraphicsGuestPause().guest_paused);
  SR_CHECK_EQ(CurrentGraphicsGuestPause().session,pause.session);
  menu->SetInput(MenuInputSource::kKeyboard,{});
  menu->TickInput();
  SR_CHECK(!menu->CaptureGuestInput());
  ObserveGraphicsGamepad({0x8000,0,0,0,0,0,0});
  SR_CHECK(menu->Snapshot().open);
  ObserveGraphicsGamepad({}); menu->TickInput();
  ClearGraphicsGuestPause();
  SR_CHECK(!menu->Snapshot().open);
  menu->TickInput();
  SR_CHECK(!menu->CaptureGuestInput());
}

SR_TEST(graphics_options_held_y_cannot_open_a_new_pause_or_replacement_context) {
  ClearGraphicsGuestPause();
  auto menu=SharedGraphicsOptionsMenu();
  menu->SetInput(MenuInputSource::kKeyboard,{});
  ObserveGraphicsGuestPause({true,true,0x83004000,4});
  ObserveGraphicsGamepad({0x8000});
  SR_CHECK(!menu->Snapshot().open);
  ObserveGraphicsGamepad({}); menu->TickInput();
  ObserveGraphicsGamepad({0x8000});
  SR_CHECK(menu->Snapshot().open);
  // A context replacement is a new pause even if the old panel was open.
  ObserveGraphicsGuestPause({true,true,0x83005000,4});
  ObserveGraphicsGamepad({0x8000});
  SR_CHECK(!menu->Snapshot().open);
  ObserveGraphicsGamepad({}); menu->TickInput();
  ObserveGraphicsGamepad({0x8000});
  SR_CHECK(menu->Snapshot().open);
  ClearGraphicsGuestPause(); ObserveGraphicsGamepad({}); menu->TickInput();
}

SR_TEST(graphics_options_guest_pause_reads_verified_root_and_state_without_writing_guest) {
  std::map<uint32_t,uint32_t> memory{{0x82911460,0x83001000},{0x83001048,4}};
  auto read = [&](uint32_t address)->std::optional<uint32_t> {
    auto it=memory.find(address);
    return it==memory.end() ? std::nullopt : std::optional(it->second);
  };
  auto pause=ReadGraphicsGuestPause(read);
  SR_CHECK(pause.valid); SR_CHECK(pause.paused);
  SR_CHECK_EQ(pause.context,0x83001000u);
  memory[0x83001048]=3;
  pause=ReadGraphicsGuestPause(read);
  SR_CHECK(pause.valid); SR_CHECK(!pause.paused);
  memory[0x82911460]=0;
  SR_CHECK(!ReadGraphicsGuestPause(read).valid);
  memory[0x82911460]=0xfffffffcu;
  SR_CHECK(!ReadGraphicsGuestPause(read).valid);
  memory.clear();
  SR_CHECK(!ReadGraphicsGuestPause(read).valid);
}

SR_TEST(graphics_options_guest_pause_sessions_change_on_exit_and_context_replacement) {
  GraphicsGuestPauseTracker tracker;
  auto pause=tracker.Observe({true,true,0x83001000,4},true,true);
  SR_CHECK(pause.guest_paused);
  const auto first=pause.session;
  SR_CHECK(first!=0);
  SR_CHECK_EQ(tracker.Observe({true,true,0x83001000,4},true,true).session,first);
  SR_CHECK(!tracker.Observe({true,false,0x83001000,3},false,true).guest_paused);
  SR_CHECK(tracker.Observe({true,true,0x83001000,4},true,true).session!=first);
  auto replaced=tracker.Observe({true,true,0x83002000,4},true,true);
  SR_CHECK(replaced.session!=first);
  SR_CHECK(!tracker.Observe({},true,true).guest_paused);
}
