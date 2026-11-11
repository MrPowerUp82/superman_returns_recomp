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

// The physical write watch reports writes to a page, not to an allocation. A
// small buffer that shares its page with bytes the game writes on every draw is
// armed, hashed and found unchanged at each use, and the armed page faults on the
// next neighbouring write: a page fault and a protection syscall per draw that
// never find a change. After `kFalseAlarms` notifications in a row that left the
// content unchanged, the buffer stops arming its pages and is hashed at every use
// instead: the same content check the watch round trip ended in, without the
// fault or the syscall. The watch is tried again after a number of frames that
// doubles each time it is suspended again (600 up to 9600).
struct BufferWatchChurnPolicy {
  static constexpr uint8_t kFalseAlarms = 8;
  static constexpr uint32_t kFirstRetryFrames = 600, kMaxRetryFrames = 9600;
  uint8_t false_alarms = 0;
  bool suspended = false;
  uint32_t retry_frames = kFirstRetryFrames;
  uint64_t resume_frame = 0;
  // A page-write notification was checked against the content hash. Returns
  // true when this notification suspends the watch.
  bool OnNotification(bool content_changed, uint64_t frame) {
    if (suspended) return false;
    if (content_changed) { false_alarms = 0; return false; }
    if (++false_alarms < kFalseAlarms) return false;
    suspended = true;
    resume_frame = frame + retry_frames;
    retry_frames = std::min(retry_frames * 2, kMaxRetryFrames);
    return true;
  }
  bool ShouldResume(uint64_t frame) const { return suspended && frame >= resume_frame; }
  void OnResumed() { suspended = false; false_alarms = 0; }
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
