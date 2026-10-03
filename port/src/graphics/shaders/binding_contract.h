#pragma once
#include <cstdint>
namespace superman_returns::graphics::shaders {
inline constexpr uint32_t BindingContractVersion = 1;
inline constexpr char BindingAbi[] = "sr-vulkan-buffers-v1";
inline constexpr uint32_t ConstantBytes = 4096, SharedBytes = 4096,
                          DescriptorArrayCount = 32;
} // namespace superman_returns::graphics::shaders
