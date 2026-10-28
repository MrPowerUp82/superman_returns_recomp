#pragma once

#include "../include/rex_memory_tracking_abi.h"
#include <map>
#include <mutex>
#include <vector>

namespace sr::tracking {
// Independent of guest data and GPU ownership. All methods serialize through
// mutex_: a completed Sample is the observation point. Fault-path operations
// (Invalidate/BeginWrite/EndWrite) only mutate preallocated pages/live records.
class WriteTrackingState {
 public:
  static constexpr uint32_t kPhysicalSize = 0x20000000, kPageSize = 4096;
  WriteTrackingState();
  uint64_t Allocate(uint32_t address, uint32_t length);
  void Release(uint32_t address, uint32_t length);
  void Invalidate(uint32_t address, uint32_t length);
  void BeginWrite(uint32_t address, uint32_t length);
  void EndWrite(uint32_t address, uint32_t length);
  void Reset();
  bool MarkCovered(RexTrackingRange range);
  RexTrackingStamp Sample(RexTrackingRange range) const;
  // Lock-free copies of page epochs and domain losses (RexTrackingFastView).
  // Set before any other call; the arrays must outlive this state.
  void SetEpochMirror(uint64_t* pages, uint64_t* domain) { mirror_pages_ = pages; mirror_domain_ = domain; }

 private:
  friend struct WriteTrackingTestAccess;
  struct Page { uint64_t epoch = 0; uint32_t writers = 0; bool covered = false; };
  struct Allocation { uint32_t length; uint64_t generation, coverage; bool invalidated = false; };
  using Allocations = std::map<uint32_t, Allocation>;
  static bool Valid(RexTrackingRange range);
  Allocations::const_iterator Find(RexTrackingRange range) const;
  void InvalidateLocked(RexTrackingRange range);
  void LoseDomain();
  uint64_t NextSequence();
  mutable std::mutex mutex_;
  std::vector<Page> pages_;
  Allocations allocations_;
  uint64_t generation_ = 0, sequence_ = 0;
  bool exhausted_ = false;
  uint64_t* mirror_pages_ = nullptr;
  uint64_t* mirror_domain_ = nullptr;
  void PublishPage(uint32_t page, uint64_t epoch) {
    if (mirror_pages_) __atomic_store_n(&mirror_pages_[page], epoch, __ATOMIC_RELEASE);
  }
  void PublishDomainLoss() {
    if (mirror_domain_) __atomic_add_fetch(mirror_domain_, 1, __ATOMIC_ACQ_REL);
  }
};
}  // namespace sr::tracking
