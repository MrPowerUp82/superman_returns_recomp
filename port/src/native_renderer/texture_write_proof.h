#pragma once
#include "../../../tools/rexglue-write-tracking/include/rex_memory_tracking_abi.h"
#include <span>
#include <string_view>
#include <vector>

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
enum class TextureTrackingMode { Off, Compare, On };
inline TextureTrackingMode ParseTextureTrackingMode(std::string_view value) {
  if(value=="compare") return TextureTrackingMode::Compare;
  if(value=="on") return TextureTrackingMode::On;
  return TextureTrackingMode::Off;
}
struct TextureWriteProof {
  uint64_t version=0, hash=0;
  std::vector<RexTrackingStamp> baseline;
  void Clear() { version=hash=0; baseline.clear(); }
  bool Publish(uint64_t resource_version,uint64_t content_hash,
               std::span<const RexTrackingStamp> before,std::span<const RexTrackingStamp> after,
               std::span<const RexTrackingStamp> final) {
    Clear();
    if(!resource_version || !SameCoveredContent(before,after) || !SameCoveredContent(before,final)) return false;
    version=resource_version; hash=content_hash; baseline.assign(before.begin(),before.end()); return true;
  }
  bool CanReuse(TextureTrackingMode mode,uint64_t resource_version,
                std::span<const RexTrackingStamp> current) const {
    return mode==TextureTrackingMode::On && version && version==resource_version && SameCoveredContent(baseline,current);
  }
  bool AuditMismatch(uint64_t resource_version,std::span<const RexTrackingStamp> before,
                     uint64_t current_hash,std::span<const RexTrackingStamp> after) const {
    return version && version==resource_version && hash!=current_hash &&
        SameCoveredContent(baseline,before) && SameCoveredContent(before,after);
  }
};
}  // namespace superman_returns::native
