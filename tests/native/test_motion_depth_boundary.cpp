#include "test_main.h"
#include "../../port/src/graphics/motion_depth_boundary.h"
using namespace superman_returns::graphics;
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
