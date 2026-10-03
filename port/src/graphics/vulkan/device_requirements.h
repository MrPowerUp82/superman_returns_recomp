#pragma once
#include "../shaders/shader_requirements.h"
#include <span>
#include <vulkan/vulkan.h>
namespace superman_returns::graphics::vulkan {
struct LayoutBinding {
  uint32_t set, binding;
  VkDescriptorType type;
  uint32_t count;
  VkShaderStageFlags stages;
};
struct DeviceCaps {
  shaders::ShaderCapabilities stage{}, layout{};
  uint32_t stage_resources=UINT32_MAX;
};
DeviceCaps GetDeviceCaps(const VkPhysicalDeviceLimits&, const VkPhysicalDeviceFeatures&);
std::vector<std::string> CheckDeviceRequirements(
    const shaders::ShaderRequirements& vs, const shaders::ShaderRequirements& ps,
    std::span<const LayoutBinding>, const DeviceCaps&);
std::vector<LayoutBinding> GameBindingLayout();
} // namespace superman_returns::graphics::vulkan
