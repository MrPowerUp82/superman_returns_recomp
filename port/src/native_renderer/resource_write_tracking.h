#pragma once
#include "../../../tools/rexglue-write-tracking/include/rex_memory_tracking_abi.h"
#include <span>

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
}  // namespace superman_returns::native
