#pragma once
#include "../../../tools/rexglue-write-tracking/include/rex_memory_tracking_abi.h"
#include <span>

namespace superman_returns::native {
inline bool SameCoveredContent(std::span<const RexTrackingStamp> before,
                               std::span<const RexTrackingStamp> after) {
  if (before.empty() || before.size() != after.size()) return false;
  for (size_t i = 0; i < before.size(); ++i) {
    const auto& a = before[i]; const auto& b = after[i];
    if (a.coverage != RexTrackingCoverage::Complete || b.coverage != RexTrackingCoverage::Complete ||
        !a.allocation_generation || !a.coverage_generation || a.active_writers || b.active_writers ||
        a.allocation_generation != b.allocation_generation || a.coverage_generation != b.coverage_generation ||
        a.invalidation_epoch != b.invalidation_epoch) return false;
  }
  return true;
}
}  // namespace superman_returns::native
