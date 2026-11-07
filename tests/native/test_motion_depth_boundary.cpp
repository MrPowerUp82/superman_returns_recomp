#include "test_main.h"
#include "../../port/src/graphics/motion_depth_boundary.h"
#include "../../port/src/graphics/object_motion_frame.h"
using namespace superman_returns::graphics;
SR_TEST(motion_cars_before_scene_camera_are_retained_but_not_published_early) {
  SR_CHECK(CanCaptureMotionSceneDepth(true,false,123,0));
  SR_CHECK(!CanSealMotionSceneDepth(true,false,123,0));
  SR_CHECK(CanCaptureMotionSceneDepth(true,true,123,123));
  SR_CHECK(CanSealMotionSceneDepth(true,true,123,123));
  SR_CHECK(!CanSealMotionSceneDepth(true,true,123,456));
  SR_CHECK(!CanCaptureMotionSceneDepth(true,true,123,456));
  SR_CHECK(!CanCaptureMotionSceneDepth(true,false,0,0));
  SR_CHECK(!CanSealMotionSceneDepth(true,false,0,0));
  SR_CHECK(!CanSealMotionSceneDepth(true,true,123,0));
  SR_CHECK(CanCaptureMotionSceneDepth(false,false,123,0));
  SR_CHECK(CanSealMotionSceneDepth(false,false,123,0));
}
SR_TEST(motion_provisional_draw_accounting_survives_later_camera_publication) {
  ObjectMotionFrame frame;frame.Begin(12,1280,720);
  // The same gate controls original color accounting and post-VS capture.
  if(CanCaptureMotionSceneDepth(true,false,123,0)) frame.ExpectDraw(7);
  if(CanCaptureMotionSceneDepth(true,true,123,123)) frame.ExpectDraw(8);
  frame.CaptureDraw(7,true,true);frame.CaptureDraw(8,true,true);
  SR_CHECK(CanSealMotionSceneDepth(true,true,123,123));
  SR_CHECK(frame.draws()==2 && frame.captured()==2 && frame.matched()==2);
  SR_CHECK(frame.Rejection()==ObjectMotionRejection::kNone);
  ObjectMotionFrame wrong;wrong.Begin(12,1280,720);wrong.ExpectDraw(7);
  if(!CanSealMotionSceneDepth(true,true,123,456)) wrong.Fail(ObjectMotionRejection::kCamera);
  wrong.CaptureDraw(7,true,true);
  SR_CHECK(wrong.Rejection()==ObjectMotionRejection::kCamera);
}
SR_TEST(motion_capture_pair_accepts_first_pair_and_the_same_retained_resources) {
  SR_CHECK(MatchesRetainedMotionCapturePair({}, {123,456}));
  SR_CHECK(MatchesRetainedMotionCapturePair({123,456}, {123,456}));
}
SR_TEST(motion_capture_pair_rejects_a_changed_or_partial_color_depth_epoch) {
  SR_CHECK(!MatchesRetainedMotionCapturePair({123,456},{123,789}));
  SR_CHECK(!MatchesRetainedMotionCapturePair({123,456},{789,456}));
  SR_CHECK(!MatchesRetainedMotionCapturePair({123,456},{123,0}));
  SR_CHECK(!MatchesRetainedMotionCapturePair({},{}));
  SR_CHECK(!MatchesRetainedMotionCapturePair({0,456},{123,456}));
}
SR_TEST(motion_depth_is_frozen_before_resolve_or_clear_destroys_scene_contents) {
  const MotionDepthBoundaryState pending{true,false,true,123};
  SR_CHECK(ShouldFreezeMotionDepthBeforeWrite(pending,123,true));
  // A partial depth clear destroys part of the snapshot too; freezing cannot
  // depend on OM targets remaining bound after the preceding color resolve.
  SR_CHECK(ShouldFreezeMotionDepthBeforeWrite(pending,123,true));
}
SR_TEST(motion_depth_boundary_ignores_other_resources_and_stencil_only_writes) {
  const MotionDepthBoundaryState pending{true,false,true,123};
  SR_CHECK(!ShouldFreezeMotionDepthBeforeWrite(pending,456,true));
  SR_CHECK(!ShouldFreezeMotionDepthBeforeWrite(pending,123,false));
  SR_CHECK(!ShouldFreezeMotionDepthBeforeWrite(pending,0,true));
}
SR_TEST(motion_depth_boundary_requires_pending_geometry_and_an_owned_resource) {
  SR_CHECK(!ShouldFreezeMotionDepthBeforeWrite({false,false,true,123},123,true));
  SR_CHECK(!ShouldFreezeMotionDepthBeforeWrite({true,true,true,123},123,true));
  SR_CHECK(!ShouldFreezeMotionDepthBeforeWrite({true,false,false,123},123,true));
  SR_CHECK(!ShouldFreezeMotionDepthBeforeWrite({true,false,true,0},123,true));
}
