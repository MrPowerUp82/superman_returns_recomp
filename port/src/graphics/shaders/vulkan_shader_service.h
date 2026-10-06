#pragma once
#include "shader_artifact.h"
#include "shader_requirements.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <chrono>
#include <stop_token>
namespace superman_returns::graphics::shaders {
using ShaderKey=uint64_t;
enum class ShaderPoll {pending,ready,failed};
struct ShaderLocation {uint32_t location;std::string type;};
struct CompiledShader {
  ShaderStage stage=ShaderStage::kVertex;
  ShaderRequirements requirements;
  std::vector<ShaderLocation> inputs,outputs;
  std::vector<uint32_t> words;
};
bool DecodeShaderResult(std::span<const uint8_t>,ShaderStage,CompiledShader&,std::string&);
std::vector<std::string> ValidateShaderPair(const CompiledShader&,const CompiledShader&);
struct ShaderResult {
  ShaderPoll status=ShaderPoll::pending;
  std::shared_ptr<const CompiledShader> artifact;
  std::string diagnostic;
};
struct VulkanShaderConfig {
  std::filesystem::path python,script,cache,emitter,common,dxc;
  std::chrono::milliseconds timeout{210000};
  uint32_t compiler_workers=1;
  // Local, game-derived SPIR-V library; misses still use the runtime compiler.
  std::filesystem::path library;
};
// Platform runner receives individual arguments, never a shell command.
using ShaderProcess=std::function<bool(std::span<const std::filesystem::path>,std::chrono::milliseconds,std::stop_token,std::string&)>;
class VulkanShaderService {
public:
  VulkanShaderService(VulkanShaderConfig,ShaderProcess);
  ~VulkanShaderService();
  ShaderKey Request(std::span<const uint8_t>,ShaderStage);
  ShaderResult Poll(ShaderKey) const;
  size_t PrecompiledCount() const;
  const std::string& LibraryDiagnostic() const;
  VulkanShaderService(const VulkanShaderService&)=delete;
  VulkanShaderService& operator=(const VulkanShaderService&)=delete;
private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace superman_returns::graphics::shaders
