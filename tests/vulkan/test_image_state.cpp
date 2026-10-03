#include "image_state.h"
#include "test_main.h"
#include <stdexcept>
using namespace superman_returns::graphics::vulkan;
namespace {
std::vector<VkImageMemoryBarrier> barriers;bool fail_record=false;
void VKAPI_CALL Record(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t count,const VkImageMemoryBarrier* b) {
  if(fail_record) throw std::runtime_error("Record failed");barriers.assign(b,b+count);
}
}
SR_TEST(image_transition_tracks_layout_only_after_recording_barrier) {
  Dispatch f;f.vkCmdPipelineBarrier=Record;ImageState state(f);Error e;
  auto image=reinterpret_cast<VkImage>(1);auto command=reinterpret_cast<VkCommandBuffer>(2);
  SR_CHECK(state.Register(image,2,2,VK_IMAGE_ASPECT_COLOR_BIT,e));
  VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,1,1,0,1};
  SR_CHECK(state.Transition(command,image,range,ImageUsage::ColorAttachment(),e));
  SR_CHECK_EQ(barriers.size(),1u);SR_CHECK(barriers[0].oldLayout==VK_IMAGE_LAYOUT_UNDEFINED);
  SR_CHECK(state.Transition(command,image,range,ImageUsage::Sampled(),e));
  SR_CHECK(barriers[0].srcAccessMask&VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  SR_CHECK(barriers[0].dstAccessMask&VK_ACCESS_SHADER_READ_BIT);
  fail_record=true;bool caught=false;
  try {state.Transition(command,image,range,ImageUsage::TransferSource(),e);}catch(const std::runtime_error&) {caught=true;}
  fail_record=false;SR_CHECK(caught);
  SR_CHECK(state.Usage(image,VK_IMAGE_ASPECT_COLOR_BIT,1,0).layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  SR_CHECK(state.Usage(image,VK_IMAGE_ASPECT_COLOR_BIT,0,0).layout==VK_IMAGE_LAYOUT_UNDEFINED);
}
SR_TEST(depth_stencil_transitions_keep_both_aspects_together_without_feature) {
  Dispatch f;f.vkCmdPipelineBarrier=Record;ImageState state(f);Error e;
  auto image=reinterpret_cast<VkImage>(3);auto command=reinterpret_cast<VkCommandBuffer>(2);
  constexpr auto aspects=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
  SR_CHECK(state.Register(image,1,1,aspects,e));
  SR_CHECK(!state.Transition(command,image,{VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1},ImageUsage::DepthAttachment(),e));
  SR_CHECK(state.Transition(command,image,{aspects,0,1,0,1},ImageUsage::DepthAttachment(),e));
  SR_CHECK(state.Transition(command,image,{aspects,0,1,0,1},ImageUsage::Sampled(true),e));
  SR_CHECK(barriers[0].srcAccessMask&VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
  SR_CHECK(barriers[0].newLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
}
