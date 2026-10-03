#pragma once
#include <cstdint>
#include <vector>
#include <string>
namespace superman_returns::graphics::shaders {
enum class ShaderStage { kVertex,kPixel };
enum class ShaderBackend { kDxil,kSpirv };
struct ShaderArtifact { ShaderStage stage; ShaderBackend backend; uint32_t abi_version; std::vector<uint8_t> binary; std::string requirements_json; };
}
