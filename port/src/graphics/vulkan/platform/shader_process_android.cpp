#include "shader_process_android.h"

namespace superman_returns::graphics::vulkan {

shaders::ShaderProcess AndroidShaderProcess() {
  return [](std::span<const std::filesystem::path> args, std::chrono::milliseconds timeout, std::stop_token cancel, std::string& error) {
    error = "Android cannot compile shaders offline; use precompiled cache.";
    return false;
  };
}

} // namespace superman_returns::graphics::vulkan
