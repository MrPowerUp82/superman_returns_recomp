#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace superman_returns::graphics::shaders {
struct ShaderRequirements {
  uint32_t storage_buffers = 0, uniform_buffers = 0, sampled_images = 0,
           samplers = 0, descriptor_sets = 0, vertex_attributes = 0,
           vertex_output_components = 0, fragment_input_components = 0;
  bool sampled_image_dynamic_indexing = false,
       storage_buffer_dynamic_indexing = false, clip_distance = false,
       cull_distance = false;
};
using ShaderCapabilities = ShaderRequirements;
inline std::vector<std::string> CheckRequirements(const ShaderRequirements &r,
                                                  const ShaderCapabilities &c) {
  std::vector<std::string> errors;
#define SR_LIMIT(name)                                                         \
  if (r.name > c.name)                                                         \
    errors.push_back(#name " requires " + std::to_string(r.name) +             \
                     ", available " + std::to_string(c.name));
  SR_LIMIT(storage_buffers);
  SR_LIMIT(uniform_buffers);
  SR_LIMIT(sampled_images);
  SR_LIMIT(samplers);
  SR_LIMIT(descriptor_sets);
  SR_LIMIT(vertex_attributes);
  SR_LIMIT(vertex_output_components);
  SR_LIMIT(fragment_input_components);
#undef SR_LIMIT
#define SR_FEATURE(name)                                                       \
  if (r.name && !c.name)                                                       \
    errors.push_back(#name " feature unavailable");
  SR_FEATURE(sampled_image_dynamic_indexing);
  SR_FEATURE(storage_buffer_dynamic_indexing);
  SR_FEATURE(clip_distance);
  SR_FEATURE(cull_distance);
#undef SR_FEATURE
  return errors;
}
} // namespace superman_returns::graphics::shaders
