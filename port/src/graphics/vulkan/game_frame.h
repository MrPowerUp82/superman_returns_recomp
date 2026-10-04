#pragma once
#include "game_renderer.h"
#include <atomic>
#include <mutex>
#include <chrono>
namespace superman_returns::graphics::vulkan {
// Shares queue submission ordering with host presentation. Shader waits and
// CPU recording happen outside this mutex so the window can keep responding.
class GameFrame {
public:
  GameFrame(Context&,std::mutex&,ShaderLookup,TextureDecoder);
  ~GameFrame();
  bool Initialize(const std::filesystem::path&,Error&);
  bool Enqueue(guest::RenderPacket&&,std::shared_ptr<TextureResource>&,Error&);
  void Cancel() {cancelled_.store(true);}
  std::function<void()> compilation_progress;
  GameRenderer& Renderer() {return renderer_;}
private:
  bool WaitShaders(Error&);
  bool WaitFence(Error&);
  Context& c_;std::mutex& queue_mutex_;ShaderLookup shaders_;GameRenderer renderer_;
  std::vector<guest::RenderPacket> packets_;
  VkCommandPool pool_=VK_NULL_HANDLE;VkCommandBuffer command_=VK_NULL_HANDLE;
  VkFence fence_=VK_NULL_HANDLE;uint64_t serial_=0;bool submitted_=false,failed_=false;
  std::atomic<bool> cancelled_{false};
  std::chrono::steady_clock::time_point cache_checkpoint_{};
};
}
