#include "resolve.h"
#include <algorithm>
#include <bit>
namespace superman_returns::graphics::vulkan {
uint32_t ResolvedPhysicalBase(std::span<const uint32_t,6> fetch) {
  uint32_t address=fetch[1]&0xfffff000;return (address&0x1fffffff)+(address>=0xe0000000?0x1000:0);
}
VkComponentMapping PlanResolvedSwizzle(std::span<const uint32_t,6> fetch,VkFormat format,bool swap) {
  VkComponentSwizzle channels[]{VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ONE,VK_COMPONENT_SWIZZLE_ONE,VK_COMPONENT_SWIZZLE_ONE};
  VkComponentSwizzle result[4];uint32_t swizzle=(fetch[3]>>1)&0xfff;
  for(uint32_t i=0;i<4;++i) {uint32_t c=(swizzle>>(3*i))&7;if(c<4 && swap && (c==0 || c==2)) c^=2;if(c<4 && format==VK_FORMAT_R32_SFLOAT) c=0;result[i]=channels[c];}
  return {result[0],result[1],result[2],result[3]};
}
ResolvePlan PlanResolveRegion(const guest::ResolvePacket& packet,const guest::SurfaceDesc& source,VkFormat source_format,VkSampleCountFlagBits samples,Error& e) {
  ResolvePlan out;auto fail=[&](const char* reason) {e={"Guest resolve plan",VK_ERROR_INITIALIZATION_FAILED,reason};return ResolvePlan{};};
  auto& fetch=packet.destination_fetch;
  if(!source.id || !source.geometry.width || !source.geometry.height || source.geometry.width>8192 || source.geometry.height>8192 || source_format==VK_FORMAT_UNDEFINED) return fail("Invalid resolve source description");
  out.clear_color=packet.flags&0x100;out.clear_depth=packet.flags&0x200;
  if(!packet.destination) {out.empty=true;e={};return out;}
  if((fetch[0]&3)!=2) return fail("Destination is not a texture fetch");
  out.dimension=(fetch[5]>>9)&3;
  if(out.dimension!=1 && out.dimension!=3) return fail("Resolve destination dimension unsupported");
  out.destination=packet.destination;out.physical_base=ResolvedPhysicalBase(fetch);
  if(!out.physical_base) return fail("Resolve destination has no physical storage");
  out.destination_size={(fetch[2]&8191)+1,((fetch[2]>>13)&8191)+1,1};
  out.destination_layers=out.dimension==3?6:((fetch[1]&(1<<10))?((fetch[2]>>26)+1):1);
  if(out.dimension==3 && (fetch[2]>>26)!=5) return fail("Cube resolve must contain six faces");
  out.destination_mips=((fetch[4]>>6)&15)+1;
  if(out.destination_mips>std::bit_width(std::max(out.destination_size.width,out.destination_size.height)) || packet.level>=out.destination_mips || packet.slice>=out.destination_layers) return fail("Resolve mip/layer exceeds destination");
  out.destination_mip=packet.level;out.destination_layer=packet.slice;
  out.destination_format=source.depth?VK_FORMAT_R32_SFLOAT:source_format;
  out.operation=source.depth?ResolveOperation::kDepthConversion:samples==VK_SAMPLE_COUNT_1_BIT?ResolveOperation::kCopy:ResolveOperation::kMsaaResolve;
  uint32_t width=std::max(1u,out.destination_size.width>>packet.level),height=std::max(1u,out.destination_size.height>>packet.level);
  auto r=packet.has_source_rect?packet.source:guest::Rect{0,0,int32_t(std::min(width,source.geometry.width)),int32_t(std::min(height,source.geometry.height))};
  if(r.left<0 || r.top<0 || r.right<r.left || r.bottom<r.top) return fail("Invalid resolve rectangle");
  int32_t x=packet.has_destination_point?packet.destination_point[0]:r.left,y=packet.has_destination_point?packet.destination_point[1]:r.top;
  if(x<0 || y<0) return fail("Negative resolve destination point");
  if(packet.tiling_active && !x && !y && (r.left || r.top) && width>=source.geometry.width && height>=source.geometry.height) {x=r.left;y=r.top;}
  uint32_t x0=std::min(uint32_t(r.left),source.geometry.width),y0=std::min(uint32_t(r.top),source.geometry.height);
  uint32_t x1=std::min(uint32_t(r.right),source.geometry.width),y1=std::min(uint32_t(r.bottom),source.geometry.height);
  uint32_t dx=std::min(uint32_t(x),width),dy=std::min(uint32_t(y),height);
  out.source_offset={int32_t(x0),int32_t(y0),0};out.destination_offset={int32_t(dx),int32_t(dy),0};
  out.extent={std::min(x1-x0,width-dx),std::min(y1-y0,height-dy),1};out.empty=!out.extent.width || !out.extent.height;
  e={};return out;
}
ResolvePlan PlanResolve(const guest::ResolvePacket& packet,const TargetStore& targets,Error& e) {
  auto source=targets.Find(packet.source_surface);
  if(!source) {e={"Guest resolve source",VK_ERROR_INITIALIZATION_FAILED,"Captured source has no rendered target"};return {};}
  auto plan=PlanResolveRegion(packet,source->description,source->format,VK_SAMPLE_COUNT_1_BIT,e);plan.source=source->id;return plan;
}
bool RecordColorResolve(Context& c,VkCommandBuffer command,const ResolvePlan& plan,TargetStore& targets,ResourceStore& resources,ImageState& state,Error& e) {
  if(plan.empty) {e={};return true;}
  if(!command || (plan.operation!=ResolveOperation::kCopy && plan.operation!=ResolveOperation::kMsaaResolve)) {e={"Resolve execution",VK_ERROR_FORMAT_NOT_SUPPORTED,"Resolve requires a color copy or MSAA operation"};return false;}
  auto source=targets.Get(plan.source,e);if(!source) return false;
  if(source->aspects!=VK_IMAGE_ASPECT_COLOR_BIT || source->format!=plan.destination_format) {e={"Resolve execution",VK_ERROR_FORMAT_NOT_SUPPORTED,"Color resolve format differs from source"};return false;}
  auto texture=resources.ResolveTexture(plan.destination,plan.destination_format,plan.destination_size,plan.destination_mips,plan.destination_layers,plan.dimension==3,state,e);if(!texture) return false;
  if(!state.Transition(command,source->image,{source->aspects,0,1,0,1},ImageUsage::TransferSource(),e) || !state.Transition(command,texture->handle,{VK_IMAGE_ASPECT_COLOR_BIT,plan.destination_mip,1,plan.destination_layer,1},ImageUsage::TransferDestination(),e)) return false;
  VkImageCopy region{};region.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.srcOffset=plan.source_offset;region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,plan.destination_mip,plan.destination_layer,1};region.dstOffset=plan.destination_offset;region.extent=plan.extent;
  if(plan.operation==ResolveOperation::kMsaaResolve) {
    VkImageResolve resolve{region.srcSubresource,region.srcOffset,region.dstSubresource,region.dstOffset,region.extent};
    c.f.vkCmdResolveImage(command,source->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,texture->handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&resolve);
  } else c.f.vkCmdCopyImage(command,source->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,texture->handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
  return state.Transition(command,texture->handle,{VK_IMAGE_ASPECT_COLOR_BIT,plan.destination_mip,1,plan.destination_layer,1},ImageUsage::Sampled(),e);
}
}
