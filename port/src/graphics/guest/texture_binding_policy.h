#pragma once
#include "shader_capture.h"

namespace superman_returns::graphics::guest {
// Slots may be reversed by internal XDK passes. Only fully identified shader
// pairs can authorize omission; a genuinely absent PS is valid for depth draws.
inline uint32_t TextureMaskForShaders(const ShaderCapture* first,
                                      const ShaderCapture* second,
                                      bool all_bound_resolved,
                                      bool enabled = true) {
  if (!enabled || !all_bound_resolved) return UINT32_MAX;
  const ShaderCapture* vs = nullptr;
  const ShaderCapture* ps = nullptr;
  for (const auto* shader : {first, second}) {
    if (!shader) continue;
    auto*& stage = shader->vertex ? vs : ps;
    if (stage) return UINT32_MAX;
    stage = shader;
  }
  if (!vs || !vs->texture_usage.known || (ps && !ps->texture_usage.known))
    return UINT32_MAX;
  return vs->texture_usage.mask | (ps ? ps->texture_usage.mask : 0u);
}
}  // namespace superman_returns::graphics::guest
