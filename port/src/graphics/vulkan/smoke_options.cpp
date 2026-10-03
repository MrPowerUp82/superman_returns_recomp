#include "smoke_options.h"
#include <algorithm>
#include <charconv>
namespace superman_returns::graphics::vulkan {
bool ParseOptions(std::span<const std::string> args, SmokeOptions &o,
                  std::string &error) {
  o = {};
  for (auto &a : args) {
    if (a == "--list-gpus")
      o.list = true;
    else if (a == "--validation")
      o.validation = true;
    else if (a == "--self-test")
      o.self_test = true;
    else if (a.starts_with("--gpu-uuid=")) {
      o.uuid = a.substr(11);
      if (o.uuid.size() != 32 ||
          !std::all_of(o.uuid.begin(), o.uuid.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                   (c >= 'A' && c <= 'F');
          })) {
        error = "GPU UUID must contain 32 hexadecimal digits";
        return false;
      }
      for (auto &c : o.uuid)
        if (c >= 'A' && c <= 'F')
          c += 32;
    } else if (a.starts_with("--frames=")) {
      auto text = a.substr(9);
      auto r =
          std::from_chars(text.data(), text.data() + text.size(), o.frames);
      if (r.ec != std::errc{} || r.ptr != text.data() + text.size() ||
          !o.frames || o.frames > 10000000) {
        error = "Frame limit must be 1..10000000";
        return false;
      }
    } else if (a.starts_with("--log-file=")) {
      o.log_file = a.substr(11);
      if (o.log_file.empty()) {
        error = "Log path cannot be empty";
        return false;
      }
    } else {
      error = "Unknown option: " + a;
      return false;
    }
  }
  if (o.self_test) {
    if (!o.frames)
      o.frames = 120;
    if (o.frames < 80) {
      error = "Self-test requires at least 80 frames";
      return false;
    }
  }
  return true;
}
} // namespace superman_returns::graphics::vulkan
