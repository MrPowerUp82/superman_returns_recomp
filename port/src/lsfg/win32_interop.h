// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <vulkan/vulkan.h>
#include "lsfg-vk-common/vulkan/vulkan.hpp"

namespace sr::lsfg {
void Check(HRESULT result, const char* operation);
void Check(VkResult result, const char* operation);

// The Vulkan device must outlive imported resources. Destruction requires all
// accesses on both queues to have finished. These objects do not own the queues.
class SharedImage {
 public:
  SharedImage(const vk::Vulkan& vulkan, ID3D12Device* device,
              uint32_t width, uint32_t height);
  ~SharedImage();
  SharedImage(const SharedImage&) = delete;
  SharedImage& operator=(const SharedImage&) = delete;
  ID3D12Resource* d3d() const { return resource_.Get(); }
  VkImage image() const { return image_; }
 private:
  void Reset();
  const vk::Vulkan& vk_;
  Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
  VkImage image_ = VK_NULL_HANDLE;
  VkDeviceMemory memory_ = VK_NULL_HANDLE;
};

class SharedFence {
 public:
  SharedFence(const vk::Vulkan& vulkan, ID3D12Device* device);
  ~SharedFence();
  SharedFence(const SharedFence&) = delete;
  SharedFence& operator=(const SharedFence&) = delete;
  ID3D12Fence* d3d() const { return fence_.Get(); }
  VkSemaphore semaphore() const { return semaphore_; }
 private:
  const vk::Vulkan& vk_;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  VkSemaphore semaphore_ = VK_NULL_HANDLE;
};
}
