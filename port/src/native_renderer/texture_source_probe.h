#pragma once
#include <chrono>
#include <cstdint>

namespace superman_returns::native {
struct TextureSourceProbeResult {
  bool readable = false, stable = false;
  double guest_ns = 0, owned_ns = 0;
};
// Callbacks hash the same range partition/seed and return false on read failure.
// Compare with the production hash and bracket the pair with a final guest read.
// Matching hashes reject observed changes, but do not make live reads atomic.
template <typename Guest, typename Owned>
TextureSourceProbeResult ProbeTextureSource(uint64_t expected, bool guest_first,
                                            Guest&& guest, Owned&& owned) {
  TextureSourceProbeResult result;
  uint64_t guest_value = 0, owned_value = 0, final_value = 0;
  auto timed = [](auto&& hash, uint64_t& value, double& ns) {
    const auto start = std::chrono::steady_clock::now();
    const bool ok = hash(value);
    ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
    return ok;
  };
  if (guest_first) {
    if (!timed(guest, guest_value, result.guest_ns) ||
        !timed(owned, owned_value, result.owned_ns)) return result;
  } else {
    if (!timed(owned, owned_value, result.owned_ns) ||
        !timed(guest, guest_value, result.guest_ns)) return result;
  }
  if (!guest(final_value)) return result;
  result.readable = true;
  result.stable = guest_value == expected && owned_value == expected && final_value == expected;
  return result;
}
}  // namespace superman_returns::native
