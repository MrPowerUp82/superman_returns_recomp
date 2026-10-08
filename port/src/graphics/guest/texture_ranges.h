#pragma once
#include "texture_layout.h"
#include <algorithm>

namespace superman_returns::graphics::guest {
// Input ranges have already been checked against the 512 MiB physical arena.
// Merge only overlaps/adjacency: never introduce bytes from an allocation gap.
inline void CoalesceTextureRanges(std::vector<TextureRange>& ranges) {
  std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) {
    return a.address < b.address;
  });
  size_t count = 0;
  for (const auto range : ranges) {
    if (!range.length) continue;
    if (count) {
      auto& previous = ranges[count - 1];
      const uint64_t previous_end = uint64_t(previous.address) + previous.length;
      if (range.address <= previous_end) {
        const uint64_t end = std::max(previous_end, uint64_t(range.address) + range.length);
        previous.length = uint32_t(end - previous.address);
        continue;
      }
    }
    ranges[count++] = range;
  }
  ranges.resize(count);
}
}  // namespace superman_returns::graphics::guest
