#include "test_main.h"
#include "../../port/src/graphics/motion_raster_coverage.h"
using namespace superman_returns::graphics;
SR_TEST(motion_raster_stencil_requires_actual_original_fragment_ownership) {
  SR_CHECK(CanReplayMotionRasterCoverage(1,true,true,true,false,0x010B0706,0,13));
  SR_CHECK(!CanReplayMotionRasterCoverage(1,true,true,true,false,1,0,0));
  SR_CHECK(!CanReplayMotionRasterCoverage(1,true,true,true,false,0x010B0706,0,0));
}
SR_TEST(motion_raster_owner_never_bypasses_depth_or_sample_requirements) {
  SR_CHECK(!CanReplayMotionRasterCoverage(4,true,true,true,false,1,0,13));
  SR_CHECK(!CanReplayMotionRasterCoverage(1,false,true,true,false,1,0,13));
  SR_CHECK(!CanReplayMotionRasterCoverage(1,true,false,true,false,1,0,13));
  SR_CHECK(!CanReplayMotionRasterCoverage(1,true,true,true,true,1,0,13));
}
SR_TEST(motion_raster_legacy_opaque_and_alpha_rules_remain_conservative) {
  SR_CHECK(CanReplayMotionRasterCoverage(1,true,true,false,false,1,0,0));
  SR_CHECK(!CanReplayMotionRasterCoverage(1,true,true,false,false,0x010B0706,0,0));
  SR_CHECK(!CanReplayMotionRasterCoverage(1,true,true,false,false,1,8,0));
  SR_CHECK(CanReplayMotionRasterCoverage(1,true,true,false,false,1,15,0));
  SR_CHECK(CanReplayMotionRasterCoverage(1,true,true,false,false,0x010B0706,8,0xFFFFFFFF));
}
