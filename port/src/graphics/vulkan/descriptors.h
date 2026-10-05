#pragma once
#include "../guest/render_packet.h"
#include "loader.h"
#include <span>
namespace superman_returns::graphics::vulkan {
enum class TextureDimension:uint32_t {k2D,k3D,kCube};
constexpr guest::ResourceId DummyTexture(TextureDimension d) {return UINT64_MAX-uint32_t(d);}
inline constexpr guest::ResourceId DummyBuffer=UINT64_MAX-3;
inline constexpr guest::ResourceId InlineBufferBase=uint64_t(1)<<63;
inline constexpr guest::ResourceId ExpandedIndexBufferBase=(uint64_t(1)<<63)|(uint64_t(1)<<62);
inline constexpr guest::ResourceId DescriptorConstantBufferBase=(uint64_t(1)<<63)|(uint64_t(1)<<61);
struct TextureBindingRequest {
  uint32_t slot;
  guest::ResourceId resource;
  TextureDimension dimension;
  uint32_t flags;
};
struct DrawBindings {
  const guest::ConstantSnapshot* original_constants = nullptr;
  std::array<uint8_t, 4096> shared_constants{};
  std::array<std::array<guest::ResourceId,32>,3> textures{};
  std::array<guest::ResourceId,32> vertex_buffers{};
  std::array<uint32_t,32> texture_indices{},sampler_indices{};
};
guest::ResourceId TextureResourceId(std::span<const uint32_t,6>);
bool RemapTextureBindings(std::span<const TextureBindingRequest>,DrawBindings&,Error&);
DrawBindings BuildBindings(const guest::DrawPacket&,Error&);
bool PlanSampler(std::span<const uint32_t,6>,const VkPhysicalDeviceFeatures&,
                 const VkPhysicalDeviceLimits&,bool mirror_clamp,
                 VkSamplerCreateInfo&,Error&);
} // namespace superman_returns::graphics::vulkan
