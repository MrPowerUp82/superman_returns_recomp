#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
namespace superman_returns::graphics {
// Private per-draw b6/space4 for the D3D12 exact-resolution variant. Existing
// guest/shared constants and descriptor layouts retain their original ABI.
struct alignas(256) SceneResolutionShaderConstants {
  std::array<float,2> pixel_scale{1,1};
  std::array<uint32_t,2> reserved{};
  std::array<std::array<uint32_t,4>,16> texture_grids{};
};
static_assert(sizeof(SceneResolutionShaderConstants)==512);
inline uint32_t EncodeSceneTextureGrid(uint32_t descriptor,uint32_t slot) {
  if(slot>=16 || (descriptor&0x7fffu)<=2) return descriptor;
  // Bit30 selects an exact guest grid; bits16..19 select this draw's slot.
  // Preserve the real SRV, gamma(bit31), and magnification filter(bit28).
  return (descriptor&~0x0fff0000u) | 0x40000000u | (slot<<16);
}
bool SpecializeSceneResolutionShader(std::string_view source,std::string& output,std::string& error);
std::string_view SceneResolutionGridFunction();
std::string_view SceneResolutionShaderCacheIdentity();
}
