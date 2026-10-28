#include "resource_write_tracking.h"
#include <limits>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace superman_returns::native {
bool AuditAndInvalidateTextureProof(TextureTrackingEntry& entry, uint64_t current_hash) {
  if (!entry.owner || !entry.watch ||
      !entry.proof.AuditMismatch(entry.version, entry.before, current_hash, entry.current)) return false;
  entry.owner->Invalidate(entry.watch);
  entry.proof.Clear();
  entry.upload = {};
  return true;
}

bool ResourceWriteTracker::Start(rex::memory::Memory& memory) {
  if (session_) return true;
#if defined(_WIN32)
  // The linked runtime is already loaded. Never load a second DLL instance.
  auto getter = reinterpret_cast<RexGetMemoryTrackingApiFn>(
      GetProcAddress(GetModuleHandleW(L"rexruntime.dll"), "RexGetMemoryTrackingApi"));
  const auto* api = getter ? getter(1, sizeof(RexMemoryTrackingApiV1)) : nullptr;
  if (!IsTrackingApiUsable(api)) { last_status_ = RexTrackingStatus::Unsupported; return false; }
  RexTrackingSession session = 0;
  last_status_ = api->start(&memory, &session);
  if (last_status_ != RexTrackingStatus::Ok || !session) {
    if (session) api->stop(session);
    return false;
  }
  // Optional: an older runtime has no fast view and keeps the locked Sample.
  auto fast_getter = reinterpret_cast<RexGetMemoryTrackingFastViewFn>(
      GetProcAddress(GetModuleHandleW(L"rexruntime.dll"), "RexGetMemoryTrackingFastView"));
  const auto* fast = fast_getter ? fast_getter(1) : nullptr;
  fast_ = fast && fast->page_epochs && fast->domain_epoch && fast->page_count ? fast : nullptr;
  api_ = api; session_ = session;
  return true;
#else
  (void)memory;
  last_status_ = RexTrackingStatus::Unsupported;
  return false;
#endif
}

void ResourceWriteTracker::Stop() {
  if (api_ && session_) api_->stop(session_);
  session_ = 0; api_ = nullptr; fast_ = nullptr;
}

RexTrackingWatch ResourceWriteTracker::Watch(std::span<const RexTrackingRange> ranges) {
  if (!session_ || ranges.empty() || ranges.size() > UINT32_MAX) return 0;
  RexTrackingWatch watch = 0;
  const auto status = api_->watch(session_, ranges.data(), uint32_t(ranges.size()), &watch);
  last_status_ = status;
  return status == RexTrackingStatus::Ok ? watch : 0;
}

void ResourceWriteTracker::Unwatch(RexTrackingWatch watch) {
  if (session_ && watch) api_->unwatch(session_, watch);
}

bool ResourceWriteTracker::Sample(RexTrackingWatch watch, std::span<RexTrackingStamp> stamps) {
  if (stamps.size() > UINT32_MAX) return false;
  const auto failed = [&] {
    for (auto& stamp : stamps) stamp = {};
    return false;
  };
  if (!session_ || !watch || stamps.empty()) return failed();
  const auto status = api_->sample(session_, watch, stamps.data(), uint32_t(stamps.size()));
  last_status_ = status;
  if (status != RexTrackingStatus::Ok) return failed();
  return true;
}

void ResourceWriteTracker::Invalidate(RexTrackingWatch watch) {
  if (session_ && watch) api_->invalidate(session_, watch);
}

RexTrackingStats ResourceWriteTracker::Stats() const {
  RexTrackingStats stats{};
  if (session_) api_->stats(session_, &stats);
  return stats;
}
}  // namespace superman_returns::native
