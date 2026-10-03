#pragma once
#include <cstdint>
#include <span>
#include <string>
namespace superman_returns::graphics::vulkan {
struct SmokeOptions {
  std::string uuid, log_file;
  uint32_t frames = 0;
  bool list = false, validation = false, self_test = false;
};
bool ParseOptions(std::span<const std::string>, SmokeOptions &,
                  std::string &error);
} // namespace superman_returns::graphics::vulkan
