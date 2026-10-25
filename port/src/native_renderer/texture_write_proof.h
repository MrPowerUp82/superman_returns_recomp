#pragma once
#include "../../../tools/rexglue-write-tracking/include/rex_memory_tracking_abi.h"
#include <span>
#include <string_view>
#include <vector>
#include <algorithm>

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
inline bool TextureDescriptorMatches(std::span<const uint32_t,6> stored,std::span<const uint32_t,6> incoming) {
  return std::equal(stored.begin(),stored.end(),incoming.begin());
}
struct TextureUploadProof {
  RexTrackingWatch watch=0;
  std::vector<RexTrackingStamp> stamps;
  uint64_t resource_version=0;
};
inline bool CanReuseTexture(const TextureUploadProof& proof,std::span<const RexTrackingStamp> current,
                            uint64_t live_resource_version) {
  return proof.watch && proof.resource_version && proof.resource_version==live_resource_version &&
      SameCoveredContent(proof.stamps,current);
}
inline bool LegacyTextureValidationRequired(uint32_t base_size,bool watches,bool written,
                                            uint64_t frame,uint64_t hashed_frame) {
  return base_size<=(4096u<<10) || (watches?written:frame-hashed_frame>=30);
}
// Physical watches catch writes immediately. Small textures must also be
// sampled periodically: guest virtual aliases (e.g. video) can bypass watches.
// Unchanged samples back off from 1 to 8 frames. When an unobserved change is
// found, sample every frame for 120 frames to keep video/UI updates fluid.
struct TextureHashBackoff {
  uint32_t stable_checks=0;
  uint64_t next_check_frame=0;
  uint64_t dynamic_until_frame=0;
  bool ShouldValidate(uint32_t base_size,bool watches,bool written,
                      uint64_t frame,uint64_t hashed_frame,bool enabled) const {
    if(!LegacyTextureValidationRequired(base_size,watches,written,frame,hashed_frame)) return false;
    return !enabled || !watches || written || base_size>(4096u<<10) ||
        frame<=dynamic_until_frame || frame>=next_check_frame;
  }
  void OnHashed(uint64_t frame,bool unchanged,bool unobserved_change=false) {
    if(unobserved_change) dynamic_until_frame=frame+120;
    stable_checks=unchanged?std::min(stable_checks+1,3u):0;
    next_check_frame=frame+(uint64_t(1)<<stable_checks);
  }
};
}  // namespace superman_returns::native
