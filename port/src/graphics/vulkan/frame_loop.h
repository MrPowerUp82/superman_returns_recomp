#pragma once
#include "swapchain.h"
#include <array>
#include <mutex>
namespace superman_returns::graphics::vulkan {
enum class FrameOutcome { kPresented, kSuspended, kRecreate, kFailed };
struct FrameWork {
  std::function<bool(VkCommandBuffer,uint64_t,Error&)> prepare;
  std::function<void(VkCommandBuffer,uint32_t)> paint;
  std::function<void(uint64_t)> retire;
  // Mutex que serializa a fila e os recursos compartilhados com a thread de gravação do jogo. DrawGame
  // só o segura em retire, prepare/paint, submit e present; nunca durante uma espera de fence ou de
  // imagem do swapchain (um vblank, com FIFO). Nulo = sem lock.
  std::mutex* queue_mutex = nullptr;
};
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
  FrameOutcome DrawGame(Context&,Swapchain&,const FrameWork&,Error&);
  uint64_t presented = 0;

private:
  struct Slot {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore acquire = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    uint64_t serial=0;
    std::function<void(uint64_t)> retire;
  };
  std::array<Slot, 2> slots_{};
  std::vector<VkSemaphore> finished_;
  std::vector<VkFence> images_in_flight_;
  uint32_t cursor_ = 0;
  bool failed_ = false;
  uint64_t next_serial_=0;
  Context *context_ = nullptr;
  void Destroy();
};
} // namespace superman_returns::graphics::vulkan
