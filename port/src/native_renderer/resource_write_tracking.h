#pragma once
#include "../../../tools/rexglue-write-tracking/include/rex_memory_tracking_abi.h"
#include <span>
#include "texture_write_proof.h"

namespace rex::memory { class Memory; }
namespace superman_returns::native {
inline bool IsTrackingApiUsable(const RexMemoryTrackingApiV1* api) {
  return api && api->version == 1 && api->size >= sizeof(RexMemoryTrackingApiV1) &&
      (api->capabilities & kRexTrackingRequiredCapabilities) == kRexTrackingRequiredCapabilities &&
      api->start && api->stop && api->watch && api->unwatch && api->sample && api->invalidate && api->stats;
}

class ResourceWriteTracker {
 public:
  ~ResourceWriteTracker() { Stop(); }
  ResourceWriteTracker() = default;
  ResourceWriteTracker(const ResourceWriteTracker&) = delete;
  ResourceWriteTracker& operator=(const ResourceWriteTracker&) = delete;
  bool Start(rex::memory::Memory&);
  void Stop();
  RexTrackingWatch Watch(std::span<const RexTrackingRange> ranges);
  void Unwatch(RexTrackingWatch);
  bool Sample(RexTrackingWatch, std::span<RexTrackingStamp> stamps);
  void Invalidate(RexTrackingWatch);
  RexTrackingStats Stats() const;
  RexTrackingStatus last_status() const { return last_status_; }
 private:
  const RexMemoryTrackingApiV1* api_ = nullptr;
  RexTrackingSession session_ = 0;
  RexTrackingStatus last_status_ = RexTrackingStatus::Unsupported;
};
// Cache-owned lease; destroyed before its tracker. No renderer access in callbacks.
struct TextureTrackingEntry {
  ResourceWriteTracker* owner=nullptr;
  RexTrackingWatch watch=0;
  uint64_t version=0;
  TextureWriteProof proof;
  TextureUploadProof upload;
  std::vector<RexTrackingStamp> before, current;
  ~TextureTrackingEntry() { if(owner && watch) owner->Unwatch(watch); }
};
}  // namespace superman_returns::native
