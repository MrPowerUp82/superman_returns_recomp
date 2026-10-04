#pragma once
#include "../../shaders/vulkan_shader_service.h"
#include <rex/ui/graphics_provider.h>
namespace superman_returns::graphics::vulkan {
struct NativeProviderConfig {
  shaders::VulkanShaderConfig shaders;
  std::filesystem::path driver_cache;
  std::string gpu_uuid;
  bool validation=false,vsync=true;
};
std::unique_ptr<rex::ui::GraphicsProvider> CreateNativeVulkanProvider(NativeProviderConfig);
}
