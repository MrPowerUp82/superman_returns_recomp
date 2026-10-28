#pragma once
#include "../../../tools/rexglue-write-tracking/include/rex_memory_tracking_abi.h"
#include <algorithm>
#include <atomic>
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
  // The worker starts the session; the front end only uses it once active.
  bool active() const { return session_ != 0; }
  // Lock-free proof checks (RexTrackingFastView); 0 = no fast view.
  uint64_t FastDomain() const {
    return fast_ ? __atomic_load_n(fast_->domain_epoch, __ATOMIC_ACQUIRE) : 0;
  }
  // True while a Complete, writer-free stamp of `range` (its epoch) still
  // holds in `domain`, without taking the runtime locks.
  bool FastStampHolds(RexTrackingRange range, uint64_t epoch, uint64_t domain) const {
    return FastStampHoldsIn(fast_, range, epoch, domain);
  }
  static bool FastStampHoldsIn(const RexTrackingFastView* view, RexTrackingRange range,
                               uint64_t epoch, uint64_t domain) {
    if (!view || !domain || !range.length) return false;
    const uint64_t last = (uint64_t(range.physical_address) + range.length - 1) / 4096;
    if (last >= view->page_count) return false;
    uint64_t newest = 0;
    for (uint64_t p = range.physical_address / 4096; p <= last; ++p)
      newest = std::max(newest, __atomic_load_n(&view->page_epochs[p], __ATOMIC_ACQUIRE));
    // Re-read the domain last: a session change between loads fails the check.
    return newest == epoch && __atomic_load_n(view->domain_epoch, __ATOMIC_ACQUIRE) == domain;
  }
 private:
  // Shared by the worker (textures) and the front end (buffers): api_ is
  // published before session_, and the runtime serializes the calls.
  const RexMemoryTrackingApiV1* api_ = nullptr;
  const RexTrackingFastView* fast_ = nullptr;
  std::atomic<RexTrackingSession> session_{0};
  std::atomic<RexTrackingStatus> last_status_{RexTrackingStatus::Unsupported};
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
// Shared by the renderer and real-memory audit regression. A missed write
// disqualifies the allocation generation, including other cache entries.
bool AuditAndInvalidateTextureProof(TextureTrackingEntry&, uint64_t current_hash);
}  // namespace superman_returns::native
