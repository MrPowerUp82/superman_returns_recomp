#include "constant_snapshot.h"
namespace superman_returns::graphics::guest {
bool CaptureFloatConstants(std::span<const uint32_t> source,
                           bool guest_big_endian,
                           std::array<uint32_t, 1024> &out,
                           std::string &error) {
  error.clear();
  if (source.size() < out.size()) {
    error = "Constant bank is truncated";
    return false;
  }
  // Hoist the span metadata load so the compiler can vectorize this loop.
  // No restrict/alignment promise: the in-place path remains supported.
  const uint32_t* input = source.data();
  for (size_t i = 0; i < out.size(); ++i) {
    uint32_t v = input[i];
    if (guest_big_endian)
      v = (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24);
    out[i] = ((v & 0x7f800000) == 0x7f800000 && (v & 0x007fffff)) ? 0 : v;
  }
  return true;
}
} // namespace superman_returns::graphics::guest
