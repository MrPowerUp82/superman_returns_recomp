#pragma once

#include <array>
#include <cstdint>

namespace superman_returns::graphics {

// Xenos RG16 EDRAM stores signed fixed point in [-32, 32]. Both native
// backends use SNORM [-1, 1] for this target, so normalize BEFORE the RTV
// conversion clamps the shader output. The shadow caster exports 32 * Z;
// its resolve has exponent bias -5 and receivers compare normalized Z.
// RGBA16 uses float storage by default and must retain its current units.
constexpr float ColorOutputScale(uint32_t guest_format) {
  return guest_format == 4 ? 1.0f / 32.0f : 1.0f;
}

inline std::array<float, 4> ColorClearValue(uint32_t guest_format,
                                           const float* guest_color) {
  const float scale = ColorOutputScale(guest_format);
  return {guest_color[0] * scale, guest_color[1] * scale,
          guest_color[2] * scale, guest_color[3] * scale};
}

}  // namespace superman_returns::graphics
