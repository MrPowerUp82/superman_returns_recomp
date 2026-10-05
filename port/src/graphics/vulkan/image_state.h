#pragma once
#include "loader.h"
#include <map>
#include <vector>
#include <mutex>
#include <functional>
namespace superman_returns::graphics::vulkan {
struct ImageUsage {
  VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
  VkPipelineStageFlags stages=VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkAccessFlags access=0;
  static ImageUsage ColorAttachment();
  static ImageUsage DepthAttachment();
  static ImageUsage Sampled(bool depth=false);
  static ImageUsage TransferSource();
  static ImageUsage TransferDestination();
};
class ImageState {
public:
  explicit ImageState(Dispatch& f):f_(f) {}
  bool Register(VkImage,uint32_t mips,uint32_t layers,VkImageAspectFlags,Error&);
  void Forget(VkImage image) {std::lock_guard lock(mutex_);images_.erase(image);}
  bool Transition(VkCommandBuffer,VkImage,VkImageSubresourceRange,ImageUsage,Error&);
  ImageUsage Usage(VkImage,VkImageAspectFlagBits,uint32_t mip,uint32_t layer) const;
  // Runs before a barrier is recorded, so a caller can end an open render pass.
  std::function<void(VkCommandBuffer)> before_barrier;
private:
  struct Image {uint32_t mips,layers;VkImageAspectFlags aspects;std::vector<ImageUsage> usage;};
  Dispatch& f_;
  mutable std::mutex mutex_;
  std::map<VkImage,Image> images_;
};
} // namespace superman_returns::graphics::vulkan
