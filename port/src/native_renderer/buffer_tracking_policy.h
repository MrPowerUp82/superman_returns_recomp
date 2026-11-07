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

inline bool ShouldSkipIntraBatchBufferCheck(
    bool force_every_use, bool already_dirty, bool content_valid,
    uint64_t last_checked_batch, uint64_t current_batch,
    uint32_t last_checked_write_seq, uint32_t current_write_seq,
    uint64_t last_checked_invalidation_stamp, uint64_t current_invalidation_stamp) {
  if (force_every_use || already_dirty || !content_valid) return false;
  return last_checked_batch == current_batch &&
         last_checked_write_seq == current_write_seq &&
         last_checked_invalidation_stamp == current_invalidation_stamp;
}
}  // namespace superman_returns::native
