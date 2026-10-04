#pragma once
#include "render_targets.h"
namespace superman_returns::graphics::vulkan {
enum class ResolveOperation {kCopy,kMsaaResolve,kDepthConversion,kReinterpretation};
struct ResolvePlan {
  ResolveOperation operation=ResolveOperation::kCopy;
  TargetId source=0;guest::ResourceId destination=0;
  VkFormat destination_format=VK_FORMAT_UNDEFINED;
  VkExtent3D destination_size{},extent{};
  VkOffset3D source_offset{},destination_offset{};
  uint32_t dimension=1,destination_mips=1,destination_layers=1,destination_mip=0,destination_layer=0;
  bool clear_color=false,clear_depth=false,empty=false;
  uint32_t physical_base=0;
};
ResolvePlan PlanResolveRegion(const guest::ResolvePacket&,const guest::SurfaceDesc&,VkFormat source_format,VkSampleCountFlagBits,Error&);
ResolvePlan PlanResolve(const guest::ResolvePacket&,const TargetStore&,Error&);
VkComponentMapping PlanResolvedSwizzle(std::span<const uint32_t,6>,VkFormat,bool swap_red_blue);
uint32_t ResolvedPhysicalBase(std::span<const uint32_t,6>);
bool RecordColorResolve(Context&,VkCommandBuffer,const ResolvePlan&,TargetStore&,ResourceStore&,ImageState&,Error&);
}
