#pragma once
#include <cstddef>
#include <string_view>

namespace superman_returns::native {
// Change dispatch granularity only; byte-pressure and swap flushes stay intact.
constexpr size_t CommandBatchLimit(const char* override_value, bool packet_sink = false) {
  const std::string_view value = override_value ? override_value : "";
  if (value == "32") return 32;
  if (value == "128") return 128;
  if (value == "256") return 256;
  return packet_sink ? 32 : 128;
}
}
