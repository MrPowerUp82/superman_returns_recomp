#pragma once
#include "context.h"
#include <filesystem>
namespace superman_returns::graphics::vulkan {
bool ValidSpirv(std::span<const uint32_t> words);
bool ReadSpirv(const std::filesystem::path &, std::vector<uint32_t> &, Error &);
class UploadBuffer {
public:
  UploadBuffer() = default;
  ~UploadBuffer();
  UploadBuffer(const UploadBuffer &) = delete;
  UploadBuffer &operator=(const UploadBuffer &) = delete;
  bool Initialize(Context &, std::span<const std::byte>, Error &);
  void Destroy();
  VkBuffer handle = VK_NULL_HANDLE;

private:
  Context *context_ = nullptr;
  VkDeviceMemory memory_ = VK_NULL_HANDLE;
};
class Triangle {
public:
  Triangle() = default;
  ~Triangle();
  Triangle(const Triangle &) = delete;
  Triangle &operator=(const Triangle &) = delete;
  bool Initialize(Context &, VkRenderPass, std::span<const uint32_t> vs,
                  std::span<const uint32_t> ps, Error &);
  void Record(VkCommandBuffer, VkExtent2D);
  void Destroy();

private:
  Context *context_ = nullptr;
  UploadBuffer vertices_;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
};
} // namespace superman_returns::graphics::vulkan
