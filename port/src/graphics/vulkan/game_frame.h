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
  bool WaitSlot(size_t slot,Error&);
  Context& c_;std::mutex& queue_mutex_;ShaderLookup shaders_;GameRenderer renderer_;
  std::vector<guest::RenderPacket> packets_;
  // Two frames in flight: frame N records while the GPU still runs frame N-1.
  // Each slot owns its pool, command buffers and fence; the members below
  // alias the slot of the frame being recorded.
  static constexpr size_t kSlots=2;
  std::array<VkCommandPool,kSlots> pools_{};std::array<VkCommandBuffer,kSlots> commands_{},uploads_{};std::array<VkFence,kSlots> fences_{};
  std::array<uint64_t,kSlots> slot_serial_{};std::array<bool,kSlots> slot_submitted_{};
  VkCommandPool pool_=VK_NULL_HANDLE;VkCommandBuffer command_=VK_NULL_HANDLE,upload_=VK_NULL_HANDLE;
  // Presentation snapshots, reused once neither a mailbox nor a submission holds them.
  std::vector<std::shared_ptr<TextureResource>> snapshots_;
  VkFence fence_=VK_NULL_HANDLE;uint64_t serial_=0;bool failed_=false;
  std::atomic<bool> cancelled_{false};
  std::chrono::steady_clock::time_point cache_checkpoint_{};
  // SR_VULKAN_PROFILE=1: per-stage averages logged every 120 frames.
  bool profiling_=false;std::chrono::steady_clock::time_point last_swap_{};
  struct {uint64_t frames=0,packets=0,interval_us=0,shaders_us=0,fence_us=0,record_us=0,queue_us=0,tail_us=0;} timing_;
};
}
