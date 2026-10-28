#pragma once
#include <algorithm>
#include <cstdint>

namespace superman_returns::native {
// When a hashed buffer may be watched by write tracking instead of hashed once
// per frame. A watched page faults on the next guest write, so buffers the game
// rewrites every frame must not be watched: a watch needs `threshold` unchanged
// hashes in a row, and every lost proof doubles it (up to 256).
struct BufferTrackingPolicy {
  uint16_t stable = 0, threshold = 4;
  bool ReadyToWatch() const { return stable >= threshold; }
  void OnUnchangedHash() { if (stable < UINT16_MAX) ++stable; }
  void OnProofLost() {
    stable = 0;
    threshold = uint16_t(std::min<uint32_t>(uint32_t(threshold) * 2, 256));
  }
};
}  // namespace superman_returns::native
