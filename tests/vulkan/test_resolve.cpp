#include "resolve.h"
#include "test_main.h"
using namespace superman_returns::graphics::vulkan;
namespace guest=superman_returns::graphics::guest;
SR_TEST(resolve_plan_keeps_partial_offset_mip_slice_and_clear) {
  guest::ResolvePacket p{};p.destination=9;p.destination_fetch[0]=2;p.destination_fetch[1]=0x1006;p.destination_fetch[2]=63|(63<<13)|(5u<<26);p.destination_fetch[4]=3<<6;p.destination_fetch[5]=3<<9;
  p.level=1;p.slice=4;p.source={4,6,20,22};p.has_source_rect=true;p.destination_point={8,10};p.has_destination_point=true;p.flags=0x100;
  guest::SurfaceDesc source{7,false,{64,64,0,0,0,1}};Error e;auto plan=PlanResolveRegion(p,source,VK_FORMAT_R8G8B8A8_UNORM,VK_SAMPLE_COUNT_1_BIT,e);
  SR_CHECK(e.message.empty());SR_CHECK_EQ(plan.destination_mip,1u);SR_CHECK_EQ(plan.destination_layer,4u);
  SR_CHECK_EQ(plan.extent.width,16u);SR_CHECK_EQ(plan.destination_offset.x,8);SR_CHECK(plan.clear_color);
  p.slice=6;PlanResolveRegion(p,source,VK_FORMAT_R8G8B8A8_UNORM,VK_SAMPLE_COUNT_1_BIT,e);SR_CHECK(!e.message.empty());
}
SR_TEST(resolve_depth_is_conversion_and_does_not_assume_d24_color_copy) {
  guest::ResolvePacket p{};p.destination=9;p.destination_fetch[0]=2;p.destination_fetch[1]=0x1017;p.destination_fetch[2]=15|(15<<13);p.destination_fetch[5]=1<<9;
  guest::SurfaceDesc source{7,true,{16,16,0,1,0,1}};Error e;auto plan=PlanResolveRegion(p,source,VK_FORMAT_D24_UNORM_S8_UINT,VK_SAMPLE_COUNT_1_BIT,e);
  SR_CHECK(e.message.empty());SR_CHECK(plan.operation==ResolveOperation::kDepthConversion);SR_CHECK_EQ(plan.destination_format,VK_FORMAT_R32_SFLOAT);
}
SR_TEST(resolve_view_swizzle_swaps_color_and_replicates_single_channel) {
  std::array<uint32_t,6> fetch{};fetch[3]=(0u|(1u<<3)|(2u<<6)|(3u<<9))<<1;
  auto color=PlanResolvedSwizzle(fetch,VK_FORMAT_R8G8B8A8_UNORM,true);
  SR_CHECK_EQ(color.r,VK_COMPONENT_SWIZZLE_B);SR_CHECK_EQ(color.b,VK_COMPONENT_SWIZZLE_R);
  auto depth=PlanResolvedSwizzle(fetch,VK_FORMAT_R32_SFLOAT,false);
  SR_CHECK_EQ(depth.r,VK_COMPONENT_SWIZZLE_R);SR_CHECK_EQ(depth.g,VK_COMPONENT_SWIZZLE_R);SR_CHECK_EQ(depth.a,VK_COMPONENT_SWIZZLE_R);
  fetch[3]=(4u|(5u<<3)|(6u<<6)|(7u<<9))<<1;
  auto forced=PlanResolvedSwizzle(fetch,VK_FORMAT_R32_SFLOAT,false);
  SR_CHECK_EQ(forced.r,VK_COMPONENT_SWIZZLE_ZERO);SR_CHECK_EQ(forced.g,VK_COMPONENT_SWIZZLE_ONE);SR_CHECK_EQ(forced.a,VK_COMPONENT_SWIZZLE_ONE);
}
