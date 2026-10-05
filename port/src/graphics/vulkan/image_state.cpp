#include "image_state.h"
#include <stdexcept>
namespace superman_returns::graphics::vulkan {
ImageUsage ImageUsage::ColorAttachment() {return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};}
ImageUsage ImageUsage::DepthAttachment() {return {VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};}
ImageUsage ImageUsage::Sampled(bool depth) {return {depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT};}
ImageUsage ImageUsage::TransferSource() {return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT};}
ImageUsage ImageUsage::TransferDestination() {return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT};}
bool ImageState::Register(VkImage image,uint32_t mips,uint32_t layers,VkImageAspectFlags aspects,Error& e) {
  std::lock_guard lock(mutex_);
  if(!image || !mips || !layers || uint64_t(mips)*layers>65536 || !aspects ||
     (aspects&~(VK_IMAGE_ASPECT_COLOR_BIT|VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT)) || images_.contains(image)) {
    e={"Image state",VK_ERROR_INITIALIZATION_FAILED,"Invalid or duplicate tracked image"};return false;
  }
  images_.emplace(image,Image{mips,layers,aspects,std::vector<ImageUsage>(size_t(mips)*layers)});e={};return true;
}
ImageUsage ImageState::Usage(VkImage image,VkImageAspectFlagBits aspect,uint32_t mip,uint32_t layer) const {
  std::lock_guard lock(mutex_);
  auto found=images_.find(image);
  if(found==images_.end() || !(found->second.aspects&aspect) || mip>=found->second.mips || layer>=found->second.layers) throw std::out_of_range("Untracked image subresource");
  return found->second.usage[layer*found->second.mips+mip];
}
bool ImageState::Transition(VkCommandBuffer command,VkImage image,VkImageSubresourceRange range,ImageUsage next,Error& e) {
  std::lock_guard lock(mutex_);
  auto found=images_.find(image);
  if(!command || !f_.vkCmdPipelineBarrier || found==images_.end()) {e={"Image transition",VK_ERROR_INITIALIZATION_FAILED,"Missing recording command or tracked image"};return false;}
  auto& tracked=found->second;
  if(range.baseMipLevel>=tracked.mips || range.baseArrayLayer>=tracked.layers || range.aspectMask!=tracked.aspects || !next.stages) {
    e={"Image transition",VK_ERROR_INITIALIZATION_FAILED,"Invalid subresource or separate depth/stencil transition without feature"};return false;
  }
  if(range.levelCount==VK_REMAINING_MIP_LEVELS) range.levelCount=tracked.mips-range.baseMipLevel;
  if(range.layerCount==VK_REMAINING_ARRAY_LAYERS) range.layerCount=tracked.layers-range.baseArrayLayer;
  if(!range.levelCount || !range.layerCount || range.levelCount>tracked.mips-range.baseMipLevel || range.layerCount>tracked.layers-range.baseArrayLayer) {
    e={"Image transition",VK_ERROR_INITIALIZATION_FAILED,"Subresource range exceeds tracked image"};return false;
  }
  std::vector<VkImageMemoryBarrier> barriers;
  VkPipelineStageFlags sources=0;
  constexpr VkAccessFlags writes=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
  for(uint32_t layer=range.baseArrayLayer;layer<range.baseArrayLayer+range.layerCount;++layer)
    for(uint32_t mip=range.baseMipLevel;mip<range.baseMipLevel+range.levelCount;++mip) {
      const auto& old=tracked.usage[layer*tracked.mips+mip];
      if(old.layout==next.layout && old.stages==next.stages && old.access==next.access && !(old.access&writes)) continue;
      VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=old.access;b.dstAccessMask=next.access;
      b.oldLayout=old.layout;b.newLayout=next.layout;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
      b.image=image;b.subresourceRange={range.aspectMask,mip,1,layer,1};barriers.push_back(b);sources|=old.stages;
    }
  if(!barriers.empty() && before_barrier) before_barrier(command);
  if(!barriers.empty()) f_.vkCmdPipelineBarrier(command,sources,next.stages,0,0,nullptr,0,nullptr,uint32_t(barriers.size()),barriers.data());
  // vkCmd calls have no result code. State changes after recording, so a fake
  // dispatch exception before recording cannot advance it.
  for(uint32_t layer=range.baseArrayLayer;layer<range.baseArrayLayer+range.layerCount;++layer)
    for(uint32_t mip=range.baseMipLevel;mip<range.baseMipLevel+range.levelCount;++mip) tracked.usage[layer*tracked.mips+mip]=next;
  e={};return true;
}
} // namespace superman_returns::graphics::vulkan
