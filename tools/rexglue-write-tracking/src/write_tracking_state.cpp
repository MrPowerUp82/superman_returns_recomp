#include "write_tracking_state.h"
#include <algorithm>
#include <limits>

namespace sr::tracking {
WriteTrackingState::WriteTrackingState() : pages_(kPhysicalSize / kPageSize) {}

bool WriteTrackingState::Valid(RexTrackingRange range) {
  return range.length && uint64_t(range.physical_address) + range.length <= kPhysicalSize;
}

WriteTrackingState::Allocations::const_iterator WriteTrackingState::Find(RexTrackingRange range) const {
  if (!Valid(range)) return allocations_.end();
  auto it = allocations_.upper_bound(range.physical_address);
  if (it == allocations_.begin()) return allocations_.end();
  --it;
  return uint64_t(range.physical_address) + range.length <= uint64_t(it->first) + it->second.length
      ? it : allocations_.end();
}

void WriteTrackingState::LoseDomain() {
  exhausted_ = true;
  for (auto& [address, allocation] : allocations_) allocation.invalidated = true;
  PublishDomainLoss();
}

uint64_t WriteTrackingState::NextSequence() {
  if (exhausted_ || sequence_ == UINT64_MAX) { LoseDomain(); return 0; }
  return ++sequence_;
}

void WriteTrackingState::InvalidateLocked(RexTrackingRange range) {
  if (!Valid(range)) { LoseDomain(); return; }
  const auto epoch = NextSequence();
  const uint32_t first = range.physical_address / kPageSize;
  const uint32_t last = (range.physical_address + range.length - 1) / kPageSize;
  for (uint32_t p = first; p <= last; ++p) {
    pages_[p].epoch = epoch; pages_[p].covered = false;
    PublishPage(p, epoch);
  }
  const uint32_t start = first * kPageSize, end = (last + 1) * kPageSize;
  auto it = allocations_.lower_bound(start);
  if (it != allocations_.begin()) --it;
  for (; it != allocations_.end() && it->first < end; ++it) {
    if (uint64_t(it->first) + it->second.length > start) it->second.invalidated = true;
  }
}

uint64_t WriteTrackingState::Allocate(uint32_t address, uint32_t length) {
  std::lock_guard lock(mutex_);
  const RexTrackingRange range{address, length};
  if (!Valid(range) || exhausted_) return 0;
  if (generation_ == UINT64_MAX) { LoseDomain(); return 0; }
  auto next = allocations_.lower_bound(address);
  bool overlaps = next != allocations_.end() && next->first < uint64_t(address) + length;
  if (next != allocations_.begin()) {
    const auto prev = std::prev(next);
    overlaps |= uint64_t(prev->first) + prev->second.length > address;
  }
  InvalidateLocked(range);  // A new mapping also disturbs other allocations sharing its pages.
  if (overlaps || exhausted_) return 0;
  const auto generation = ++generation_;
  allocations_.emplace(address, Allocation{length, generation, NextSequence()});
  return exhausted_ ? 0 : generation;
}

void WriteTrackingState::Release(uint32_t address, uint32_t length) {
  std::lock_guard lock(mutex_);
  InvalidateLocked({address, length});
  auto it = allocations_.find(address);
  if (it != allocations_.end() && it->second.length == length) allocations_.erase(it);
  // Partial/mismatched release cannot certify a new identity for the residual.
}

void WriteTrackingState::Invalidate(uint32_t address, uint32_t length) {
  std::lock_guard lock(mutex_);
  InvalidateLocked({address, length});
}

void WriteTrackingState::BeginWrite(uint32_t address, uint32_t length) {
  std::lock_guard lock(mutex_);
  InvalidateLocked({address, length});
  if (!Valid({address, length})) return;
  for (uint32_t p = address / kPageSize; p <= (address + length - 1) / kPageSize; ++p) {
    if (pages_[p].writers == UINT32_MAX) LoseDomain();
    else ++pages_[p].writers;
  }
}

void WriteTrackingState::EndWrite(uint32_t address, uint32_t length) {
  std::lock_guard lock(mutex_);
  if (!Valid({address, length})) { LoseDomain(); return; }
  for (uint32_t p = address / kPageSize; p <= (address + length - 1) / kPageSize; ++p) {
    if (!pages_[p].writers) LoseDomain();
    else --pages_[p].writers;
  }
}

void WriteTrackingState::Reset() {
  std::lock_guard lock(mutex_);
  const auto epoch = NextSequence();
  for (uint32_t p = 0; p < pages_.size(); ++p) {
    pages_[p].covered = false; pages_[p].epoch = epoch;
    PublishPage(p, epoch);
  }
  PublishDomainLoss();
  // Do not discard active host producers or recycle generation counters.
  allocations_.clear();
}

bool WriteTrackingState::MarkCovered(RexTrackingRange range) {
  std::lock_guard lock(mutex_);
  const auto it = Find(range);
  if (exhausted_ || it == allocations_.end() || it->second.invalidated) return false;
  for (uint32_t p = range.physical_address / kPageSize;
       p <= (range.physical_address + range.length - 1) / kPageSize; ++p) {
    if (pages_[p].writers) return false;
  }
  for (uint32_t p = range.physical_address / kPageSize;
       p <= (range.physical_address + range.length - 1) / kPageSize; ++p) pages_[p].covered = true;
  return true;
}

RexTrackingStamp WriteTrackingState::Sample(RexTrackingRange range) const {
  std::lock_guard lock(mutex_);
  RexTrackingStamp stamp{};
  const auto it = Find(range);
  if (it == allocations_.end()) return stamp;
  stamp.allocation_generation = it->second.generation;
  stamp.coverage_generation = it->second.coverage;
  bool covered = true;
  uint64_t writers = 0;
  for (uint32_t p = range.physical_address / kPageSize;
       p <= (range.physical_address + range.length - 1) / kPageSize; ++p) {
    stamp.invalidation_epoch = std::max(stamp.invalidation_epoch, pages_[p].epoch);
    writers += pages_[p].writers;
    covered &= pages_[p].covered;
  }
  stamp.active_writers = uint32_t(std::min(writers, uint64_t(UINT32_MAX)));
  stamp.coverage = exhausted_ || it->second.invalidated ? RexTrackingCoverage::Invalidated
      : covered && !writers ? RexTrackingCoverage::Complete : RexTrackingCoverage::Unknown;
  return stamp;
}
}  // namespace sr::tracking
