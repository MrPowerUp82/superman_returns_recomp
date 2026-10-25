#include "../../port/src/graphics/temporal_frame.h"
#include "test_main.h"
#include <array>
#include <limits>

using namespace superman_returns::graphics;

namespace {
constexpr std::array<float, 16> kIdentity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
}

SR_TEST(temporal_initial_frame_has_no_previous_camera_and_cannot_enable_dlss) {
  TemporalFrameState state;
  state.Begin(1, 1280, 720);
  SR_CHECK(state.CaptureCamera(kIdentity));
  state.ObserveScene(42, 43);
  state.ObserveDepthConvention(true);
  state.ObserveJitter(0.0f, 0.0f);
  state.ObserveHudBoundary();
  state.ObserveMotionVectors(true, true);
  SR_CHECK(!state.current().previous_camera_valid);
  SR_CHECK(state.DlssBlocker() == TemporalBlocker::kPreviousCamera);
  state.End();
  state.Begin(2, 1280, 720);
  SR_CHECK(state.CaptureCamera(kIdentity));
  state.ObserveScene(42, 43);
  state.ObserveDepthConvention(true);
  state.ObserveJitter(0.0f, 0.0f);
  state.ObserveHudBoundary();
  state.ObserveMotionVectors(true, true);
  SR_CHECK(state.current().previous_camera_valid);
  SR_CHECK(state.DlssBlocker() == TemporalBlocker::kNone);
}

SR_TEST(temporal_history_reset_on_gap_resize_or_camera_cut) {
  TemporalFrameState state;
  state.Begin(100, 1280, 720);
  SR_CHECK(state.CaptureCamera(kIdentity));
  state.End();
  state.Begin(102, 1280, 720);
  SR_CHECK(state.CaptureCamera(kIdentity));
  SR_CHECK(!state.current().previous_camera_valid);
  state.End();
  state.Begin(103, 640, 360);
  SR_CHECK(state.CaptureCamera(kIdentity));
  SR_CHECK(!state.current().previous_camera_valid);
  state.End();
  state.Begin(104, 640, 360);
  state.CutCamera();
  SR_CHECK(state.CaptureCamera(kIdentity));
  SR_CHECK(!state.current().previous_camera_valid);
}

SR_TEST(temporal_camera_rejects_nonfinite_and_does_not_poison_history) {
  TemporalFrameState state;
  auto bad = kIdentity;
  bad[6] = std::numeric_limits<float>::quiet_NaN();
  state.Begin(1, 1280, 720);
  SR_CHECK(!state.CaptureCamera(bad));
  SR_CHECK(state.DlssBlocker() == TemporalBlocker::kCamera);
  state.End();
  state.Begin(2, 1280, 720);
  SR_CHECK(state.CaptureCamera(kIdentity));
  SR_CHECK(!state.current().previous_camera_valid);
}

SR_TEST(temporal_dlss_gates_scene_depth_motion_and_hud_separately) {
  TemporalFrameState state;
  state.Begin(1, 1280, 720);
  state.CaptureCamera(kIdentity);
  state.End();
  state.Begin(2, 1280, 720);
  state.CaptureCamera(kIdentity);
  SR_CHECK(state.DlssBlocker() == TemporalBlocker::kSceneColor);
  state.ObserveScene(12, 0);
  SR_CHECK(state.DlssBlocker() == TemporalBlocker::kSceneDepth);
  state.ObserveScene(12, 13);
  SR_CHECK(state.DlssBlocker() == TemporalBlocker::kDepthConvention);
  state.ObserveDepthConvention(true);
  SR_CHECK(state.DlssBlocker() == TemporalBlocker::kMotionVectors);
  state.ObserveMotionVectors(true, false);
  SR_CHECK(state.DlssBlocker() == TemporalBlocker::kObjectMotion);
  state.ObserveMotionVectors(true, true);
  SR_CHECK(state.DlssBlocker() == TemporalBlocker::kJitter);
  state.ObserveJitter(0.25f, -0.25f);
  SR_CHECK(state.DlssBlocker() == TemporalBlocker::kHudBoundary);
  state.ObserveHudBoundary();
  SR_CHECK(state.DlssBlocker() == TemporalBlocker::kNone);
}
