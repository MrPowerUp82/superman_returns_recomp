#pragma once
#include "swapchain.h"
#include <array>
namespace superman_returns::graphics::vulkan {
enum class FrameOutcome { kPresented, kSuspended, kRecreate, kFailed };
class FrameLoop {
public:
  FrameLoop() = default;
  ~FrameLoop();
  FrameLoop(const FrameLoop &) = delete;
  FrameLoop &operator=(const FrameLoop &) = delete;
  bool Initialize(Context &, Swapchain &, Error &);
  bool Retire(Context &, Error &);
  FrameOutcome Draw(Context &, Swapchain &,
                    const std::function<void(VkCommandBuffer, uint32_t)> &,
                    Error &);
  uint64_t presented = 0;

private:
  struct Slot {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore acquire = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
  };
  std::array<Slot, 2> slots_{};
  std::vector<VkSemaphore> finished_;
  std::vector<VkFence> images_in_flight_;
  uint32_t cursor_ = 0;
  bool failed_ = false;
  Context *context_ = nullptr;
  void Destroy();
};
} // namespace superman_returns::graphics::vulkan
