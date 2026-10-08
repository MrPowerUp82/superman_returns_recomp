#pragma once
#include "../graphics/guest/texture_layout.h"

namespace superman_returns::native {
// Memoize only a clean page scan. A completion counter must be published after
// all page notifications, and sampled before scanning: a writer finishing
// during the scan then forces another scan on the next use. Entry identifiers
// or counters incremented before publishing pages are not sufficient.
struct CleanTextureWatchScan {
  uint64_t frame=0, completed_writes=0;
  uint32_t baseline=0;
  bool valid=false;
  template<class Scan>
  bool Written(uint64_t current_frame, uint64_t completed, uint32_t watch_baseline,
               Scan&& scan, bool& reused) {
    reused=valid && frame==current_frame && completed_writes==completed && baseline==watch_baseline;
    if(reused) return false;
    const bool written=scan();
    valid=!written;
    if(valid) { frame=current_frame; completed_writes=completed; baseline=watch_baseline; }
    return written;
  }
};
template <typename Written>
bool TextureContentWritten(std::span<const graphics::guest::TextureRange> ranges,
                           uint32_t sequence, Written&& written) {
  for (const auto& range : ranges)
    if (written(range.address, range.length, sequence)) return true;
  return false;
}
}  // namespace superman_returns::native
