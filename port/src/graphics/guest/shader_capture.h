#pragma once
#include <cstdint>
#include <vector>
#include "shader_texture_usage.h"
#include "shader_constant_usage.h"

namespace superman_returns::graphics::guest {
// Original container captured at creation, before XDK bind-time patching.
// Commands share immutable ownership; reused guest object addresses cannot
// change shaders that have already been enqueued.
struct ShaderCapture {
  uint64_t hash = 0;
  bool vertex = false;
  bool dynamic_vertex_fetch = false;
  ShaderTextureUsage texture_usage;
  ShaderConstantUsage constant_usage;
  std::vector<uint8_t> container;
};
} // namespace superman_returns::graphics::guest
