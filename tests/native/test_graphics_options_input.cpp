#include "test_main.h"
#include "../../port/src/graphics/graphics_options.h"
#include <memory>
using namespace superman_returns::graphics;

SR_TEST(graphics_options_requires_verified_pause_entry_and_input_capture) {
  auto options = std::make_shared<GraphicsOptions>();
  GraphicsOptionsMenu menu(options);
  SR_CHECK(!menu.Open({}));
  SR_CHECK(!menu.Open({17,true,false,true}));
  SR_CHECK(!menu.Open({17,true,true,false}));
  SR_CHECK(!menu.Open({17,false,true,true}));
  SR_CHECK(menu.Open({17,true,true,true}));
  SR_CHECK(!menu.Open({17,true,true,true}));
  SR_CHECK(menu.CaptureGuestInput());
  SR_CHECK_EQ(menu.Snapshot().pause_session, 17u);
}

SR_TEST(graphics_options_held_opening_input_is_disarmed_and_back_is_drained) {
  GraphicsOptionsMenu menu(std::make_shared<GraphicsOptions>());
  menu.Open({42,true,true,true});
  menu.SetInput(MenuInputSource::kGamepad, {kMenuBack,true});
  menu.TickInput();
  SR_CHECK(menu.Snapshot().open);
  menu.SetInput(MenuInputSource::kGamepad, {});
  menu.TickInput();
  menu.SetInput(MenuInputSource::kGamepad, {kMenuDown,true});
  menu.TickInput(); menu.TickInput();
  SR_CHECK_EQ(menu.Snapshot().selected, 1u); // Holding must not skip rows.
  menu.SetInput(MenuInputSource::kGamepad, {kMenuBack,true});
  menu.TickInput();
  SR_CHECK(!menu.Snapshot().open);
  SR_CHECK_EQ(menu.Snapshot().pause_session, 42u); // Return to same pause, never resume.
  SR_CHECK(menu.CaptureGuestInput());
  menu.SetInput(MenuInputSource::kGamepad, {0,true}); // Trigger/axis still held.
  menu.TickInput();
  SR_CHECK(menu.CaptureGuestInput());
  menu.SetInput(MenuInputSource::kGamepad, {});
  menu.TickInput();
  SR_CHECK(!menu.CaptureGuestInput());
}

SR_TEST(graphics_options_keyboard_and_gamepad_share_navigation_and_requests) {
  auto options = std::make_shared<GraphicsOptions>();
  auto epoch = options->BeginBackend(NativeApi::kD3D12);
  GraphicsBackendReport report; report.frame=1;
  for (auto& feature : report.features) feature={true,true,""};
  options->PublishBackend(epoch,report);
  GraphicsOptionsMenu menu(options);
  menu.Open({9,true,true,true}); menu.TickInput();
  menu.SetInput(MenuInputSource::kKeyboard,{kMenuRight,true}); menu.TickInput();
  SR_CHECK_EQ(options->Snapshot().requested[0],1);
  SR_CHECK_EQ(options->Snapshot().applied[0],0);
  menu.SetInput(MenuInputSource::kKeyboard,{}); menu.TickInput();
  menu.SetInput(MenuInputSource::kGamepad,{kMenuLeft,true}); menu.TickInput();
  SR_CHECK_EQ(options->Snapshot().requested[0],0);
  menu.SetInput(MenuInputSource::kGamepad,{}); menu.TickInput();
  menu.SetInput(MenuInputSource::kKeyboard,{kMenuUp,true}); menu.TickInput();
  SR_CHECK_EQ(menu.Snapshot().selected,4u);
}

SR_TEST(graphics_options_revoked_pause_closes_and_retains_capture_until_neutral) {
  GraphicsOptionsMenu menu(std::make_shared<GraphicsOptions>());
  menu.Open({10,true,true,true}); menu.TickInput();
  menu.SetInput(MenuInputSource::kKeyboard,{kMenuBack,true});
  menu.ObservePause({11,true,true,true});
  SR_CHECK(!menu.Snapshot().open);
  SR_CHECK(menu.CaptureGuestInput());
  SR_CHECK(!menu.Open({11,true,true,true}));
  menu.SetInput(MenuInputSource::kKeyboard,{}); menu.TickInput();
  SR_CHECK(!menu.CaptureGuestInput());
  SR_CHECK(menu.Open({11,true,true,true}));
}

SR_TEST(graphics_options_gamepad_adapter_maps_b_dpad_stick_and_drains_other_controls) {
  auto sample = SampleGraphicsGamepad({0x2009, 0, 0, 0, 0, 0, 0});
  SR_CHECK_EQ(sample.down, kMenuBack|kMenuUp|kMenuRight);
  SR_CHECK(sample.any_guest_input_down);
  sample = SampleGraphicsGamepad({0, -16000, -16000, 0, 0, 0, 0});
  SR_CHECK_EQ(sample.down,kMenuLeft|kMenuDown);
  sample = SampleGraphicsGamepad({0x1000,0,0,12000,0,255,0});
  SR_CHECK_EQ(sample.down,0u);
  SR_CHECK(sample.any_guest_input_down);
  sample = SampleGraphicsGamepad({0,100,-100,100,-100,0,0});
  SR_CHECK(!sample.any_guest_input_down);
}

SR_TEST(graphics_options_softness_off_is_not_a_user_option_and_requires_active_rt) {
  auto options = std::make_shared<GraphicsOptions>();
  auto epoch = options->BeginBackend(NativeApi::kD3D12);
  GraphicsBackendReport report; report.frame=1;
  for (auto& feature:report.features) feature={true,true,""};
  options->PublishBackend(epoch,report);
  SR_CHECK_EQ(options->Snapshot().requested[1],1);
  SR_CHECK(!options->Request(GraphicsOption::kShadowSoftness,0));
  SR_CHECK(!options->Request(GraphicsOption::kShadowSoftness,2));
  options->Request(GraphicsOption::kRtShadows,1);
  report.frame=2;report.request_revision=1;report.applied[0]=1;report.applied[1]=1;
  options->PublishBackend(epoch,report);
  SR_CHECK(options->Request(GraphicsOption::kShadowSoftness,2));
  options->BeginBackend(NativeApi::kVulkan);
  GraphicsOptionsMenu menu(options);
  menu.Open({1,true,true,true}); menu.TickInput();
  menu.Select(1); menu.Adjust(-1);
  SR_CHECK_EQ(options->Snapshot().requested[1],2); // Preserve preference while inactive.
}
