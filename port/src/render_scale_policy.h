#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace superman_returns::render_scale {

// The guest's display size, render pitches, and resolve descriptors must stay
// in the original coordinate system when the native renderer is active.
inline int32_t GuestPercent(int32_t percent, bool native_renderer) {
  return native_renderer ? 100 : std::clamp(percent, 25, 100);
}

// Native surfaces, resolves and the output texture share this host scale.
// The dedicated native setting takes precedence over the old guest setting.
// Legacy --sr_render_scale=75/50 is supported by the native D3D12 renderer.
inline float NativeScale(double native_scale, int32_t guest_percent) {
  if (native_scale == 1.0 && guest_percent < 100)
    return float(std::clamp(guest_percent, 50, 100)) / 100.0f;
  return float(std::clamp(native_scale, 0.5, 4.0));
}

// D3D12 texture extents must never round to zero (e.g. a 1x1 resolve).
// Pixel *coordinates* still use normal rounding, so zero remains zero.
inline uint32_t HostDimension(uint32_t pixels, float scale) {
  return std::max(1u, uint32_t(std::lround(double(pixels) * double(scale))));
}

}  // namespace superman_returns::render_scale
