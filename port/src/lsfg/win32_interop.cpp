// SPDX-License-Identifier: GPL-3.0-or-later
#include "win32_interop.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

namespace sr::lsfg {
void Check(HRESULT result, const char* operation) {
  if (FAILED(result)) throw std::runtime_error(std::string(operation) +
      " failed (HRESULT " + std::to_string(static_cast<uint32_t>(result)) + ")");
}
void Check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) +
      " failed (VkResult " + std::to_string(result) + ")");
}
namespace {
struct WinHandle {
  HANDLE value = nullptr;
  ~WinHandle() { if (value) CloseHandle(value); }
};
PFN_vkGetInstanceProcAddr Loader() {
  static HMODULE module = LoadLibraryExW(L"vulkan-1.dll", nullptr,
      LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!module) throw std::runtime_error("No Windows Vulkan runtime");
  return reinterpret_cast<PFN_vkGetInstanceProcAddr>(
      GetProcAddress(module, "vkGetInstanceProcAddr"));
}
template<class T> T InstanceFunction(const vk::Vulkan& vk, const char* name) {
  auto loader = Loader();
  auto function = loader ? reinterpret_cast<T>(loader(vk.inst(), name)) : nullptr;
  if (!function) throw std::runtime_error(std::string("Missing Vulkan function: ") + name);
  return function;
}
template<class T> T DeviceFunction(const vk::Vulkan& vk, const char* name) {
  auto function = reinterpret_cast<T>(vk.fi().GetDeviceProcAddr(vk.dev(), name));
  if (!function) throw std::runtime_error(std::string("Missing Vulkan function: ") + name);
  return function;
}
void CheckLuid(const vk::Vulkan& vk, ID3D12Device* device) {
  VkPhysicalDeviceIDProperties ids{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
  VkPhysicalDeviceProperties2 props{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                   .pNext = &ids};
  vk.fi().GetPhysicalDeviceProperties2(vk.physdev(), &props);
  auto luid = device->GetAdapterLuid();
  if (!ids.deviceLUIDValid || std::memcmp(ids.deviceLUID, &luid, sizeof(luid)))
    throw std::runtime_error("D3D12/Vulkan adapter LUID mismatch");
}
}

SharedImage::SharedImage(const vk::Vulkan& vulkan, ID3D12Device* device,
                         uint32_t width, uint32_t height) : vk_(vulkan) {
  CheckLuid(vk_, device);
  if (!width || !height) throw std::runtime_error("Empty shared image");
  // Check the exact handle type, format, usage and tiling before allocation.
  const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  VkPhysicalDeviceExternalImageFormatInfo external{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT};
  VkPhysicalDeviceImageFormatInfo2 query{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, .pNext = &external,
      .format = VK_FORMAT_R8G8B8A8_UNORM, .type = VK_IMAGE_TYPE_2D,
      .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage};
  VkExternalImageFormatProperties supported{
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  VkImageFormatProperties2 properties{
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, .pNext = &supported};
  auto queryFormat = InstanceFunction<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
      vk_, "vkGetPhysicalDeviceImageFormatProperties2");
  Check(queryFormat(vk_.physdev(), &query, &properties), "external image support");
  if (!(supported.externalMemoryProperties.externalMemoryFeatures &
        VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
    throw std::runtime_error("D3D12 resource import is unsupported");
  try {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height;
    desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    // Vulkan/D3D12 interop requires simultaneous-access textures on Windows.
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource_)), "shared D3D12 texture");
    WinHandle shared;
    Check(device->CreateSharedHandle(resource_.Get(), nullptr, GENERIC_ALL,
        nullptr, &shared.value), "texture shared handle");
    VkExternalMemoryImageCreateInfo imageExternal{
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT};
    VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext = &imageExternal,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {width, height, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    Check(vk_.df().CreateImage(vk_.dev(), &imageInfo, nullptr, &image_), "import image");
    VkMemoryRequirements requirements{};
    vk_.df().GetImageMemoryRequirements(vk_.dev(), image_, &requirements);
    auto memoryType = vk_.findMemoryTypeIndex(requirements.memoryTypeBits, false);
    if (!memoryType) throw std::runtime_error("No compatible import memory type");
    VkMemoryDedicatedAllocateInfo dedicated{
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .image = image_};
    VkImportMemoryWin32HandleInfoKHR import{
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        .pNext = &dedicated,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
        .handle = shared.value};
    VkMemoryAllocateInfo allocation{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &import,
        .allocationSize = requirements.size, .memoryTypeIndex = *memoryType};
    Check(vk_.df().AllocateMemory(vk_.dev(), &allocation, nullptr, &memory_), "import memory");
    Check(vk_.df().BindImageMemory(vk_.dev(), image_, memory_, 0), "bind imported image");
  } catch (...) { Reset(); throw; }
}
void SharedImage::Reset() {
  if (image_) vk_.df().DestroyImage(vk_.dev(), image_, nullptr);
  if (memory_) vk_.df().FreeMemory(vk_.dev(), memory_, nullptr);
  image_ = VK_NULL_HANDLE; memory_ = VK_NULL_HANDLE;
  resource_.Reset();
}
SharedImage::~SharedImage() { Reset(); }

SharedFence::SharedFence(const vk::Vulkan& vulkan, ID3D12Device* device) : vk_(vulkan) {
  CheckLuid(vk_, device);
  VkSemaphoreTypeCreateInfo queryType{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE};
  VkPhysicalDeviceExternalSemaphoreInfo query{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
      .pNext = &queryType,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT};
  VkExternalSemaphoreProperties supported{
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
  InstanceFunction<PFN_vkGetPhysicalDeviceExternalSemaphoreProperties>(vk_,
      "vkGetPhysicalDeviceExternalSemaphoreProperties")(vk_.physdev(), &query, &supported);
  if (!(supported.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT))
    throw std::runtime_error("D3D12 fence import is unsupported");
  Check(device->CreateFence(0, D3D12_FENCE_FLAG_SHARED,
      IID_PPV_ARGS(&fence_)), "shared D3D12 fence");
  WinHandle shared;
  Check(device->CreateSharedHandle(fence_.Get(), nullptr, GENERIC_ALL,
      nullptr, &shared.value), "fence shared handle");
  VkSemaphoreTypeCreateInfo type{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE};
  VkSemaphoreCreateInfo info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &type};
  Check(vk_.df().CreateSemaphore(vk_.dev(), &info, nullptr, &semaphore_), "timeline semaphore");
  try {
    VkImportSemaphoreWin32HandleInfoKHR import{
        .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR,
        .semaphore = semaphore_, .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT,
        .handle = shared.value};
    Check(DeviceFunction<PFN_vkImportSemaphoreWin32HandleKHR>(vk_,
        "vkImportSemaphoreWin32HandleKHR")(vk_.dev(), &import), "import D3D12 fence");
  } catch (...) {
    vk_.df().DestroySemaphore(vk_.dev(), semaphore_, nullptr);
    semaphore_ = VK_NULL_HANDLE;
    throw;
  }
}
SharedFence::~SharedFence() {
  if (semaphore_) vk_.df().DestroySemaphore(vk_.dev(), semaphore_, nullptr);
}
}
