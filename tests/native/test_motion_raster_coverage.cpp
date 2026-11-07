#include "test_main.h"
#include "../../port/src/graphics/motion_raster_coverage.h"
#include "../../port/src/graphics/ray_geometry_opacity.h"
using namespace superman_returns::graphics;
SR_TEST(ray_geometry_literal_alpha_body_requires_a_whole_draw_certificate) {
  RayGeometryOpacity draw{0xC687EB65D635C5D7ull,0x3EF6A4A3C740ADB1ull,
      0x010b0706,0,7,0,31,1.f,true,true,true};
  SR_CHECK(IsOpaqueRayGeometry(draw));
  draw.spec=1;SR_CHECK(IsOpaqueRayGeometry(draw));
  draw.spec=64;SR_CHECK(IsOpaqueRayGeometry(draw));
  draw.spec=65;SR_CHECK(IsOpaqueRayGeometry(draw));
  for(uint32_t spec:{2u,4u,8u,16u,32u,128u,0xffffffffu}) {
    draw.spec=spec;SR_CHECK(!IsOpaqueRayGeometry(draw));
  }
  draw.spec=0;
  draw.ps=0xE83A13DF3297C6CBull;SR_CHECK(!IsOpaqueRayGeometry(draw));
  draw.ps=0xE6C740EAC16B70D4ull;SR_CHECK(!IsOpaqueRayGeometry(draw));
  draw.ps=0x3EF6A4A3C740ADB1ull;
  draw.vs=0xC20921D0DBBD2F1Full;SR_CHECK(!IsOpaqueRayGeometry(draw));
  draw.vs=0xC687EB65D635C5D7ull;
  draw.original_shader=false;SR_CHECK(!IsOpaqueRayGeometry(draw));draw.original_shader=true;
  draw.coverage_qualified=false;SR_CHECK(!IsOpaqueRayGeometry(draw));draw.coverage_qualified=true;
  draw.depth_write=false;SR_CHECK(!IsOpaqueRayGeometry(draw));draw.depth_write=true;
  draw.owner=0;SR_CHECK(!IsOpaqueRayGeometry(draw));draw.owner=31;
  draw.owner=4097;SR_CHECK(!IsOpaqueRayGeometry(draw));draw.owner=31;
  draw.color_mask=3;SR_CHECK(!IsOpaqueRayGeometry(draw));draw.color_mask=7;
  draw.output_scale=1.f/32;SR_CHECK(!IsOpaqueRayGeometry(draw));draw.output_scale=1;
  draw.blend=0x101;SR_CHECK(!IsOpaqueRayGeometry(draw));
}
SR_TEST(ray_geometry_existing_opaque_rule_does_not_require_a_motion_owner) {
  RayGeometryOpacity draw{};draw.depth_write=true;draw.blend=1;
  SR_CHECK(IsOpaqueRayGeometry(draw));
  draw.color_control=8;SR_CHECK(!IsOpaqueRayGeometry(draw));
  draw.color_control=0;draw.depth_write=false;SR_CHECK(!IsOpaqueRayGeometry(draw));
}
SR_TEST(motion_alpha_over_requires_original_owner_and_complete_rgb_writes) {
  SR_CHECK(MotionBlendHasSingleOwner(0x010b0706,37,7));
  SR_CHECK(MotionBlendHasSingleOwner(0x010b0706,37,15));
  SR_CHECK(!MotionBlendHasSingleOwner(0x010b0706,0,7));
  SR_CHECK(!MotionBlendHasSingleOwner(0x010b0706,UINT32_MAX,7));
  for(uint32_t mask=0;mask<7;++mask)SR_CHECK(!MotionBlendHasSingleOwner(0x010b0706,37,mask));
  for(uint32_t blend:{0x101u,0x107u,0x726u,0x806u})SR_CHECK(!MotionBlendHasSingleOwner(blend,37,7));
  SR_CHECK(MotionBlendHasSingleOwner(1,0,7));
}
SR_TEST(motion_alpha_certificate_is_requested_only_for_the_exact_alpha_over_case) {
  SR_CHECK_EQ(MotionPixelOwnerConstant(37,0x010b0706,7),37u|kMotionOwnerRequireOpaqueAlpha);
  SR_CHECK_EQ(MotionPixelOwnerConstant(37,1,7),37u);
  SR_CHECK_EQ(MotionPixelOwnerConstant(37,0x010b0706,3),37u);
  SR_CHECK_EQ(MotionPixelOwnerConstant(37,0x101,7),37u);
}
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

SR_TEST(motion_invalidator_uses_effective_depth_before_write_mask) {
  for(uint32_t compare=0;compare<8;++compare) {
    SR_CHECK_EQ(MotionInvalidatorDepthComparison(false,true,false,compare),uint32_t(8));
    SR_CHECK_EQ(MotionInvalidatorDepthComparison(true,true,true,compare),uint32_t(8));
    SR_CHECK_EQ(MotionInvalidatorDepthComparison(true,false,true,compare),uint32_t(8));
    SR_CHECK_EQ(MotionInvalidatorDepthComparison(true,true,false,compare),uint32_t(3));
    SR_CHECK_EQ(MotionInvalidatorDepthComparison(true,false,false,compare),compare+1);
  }
}
