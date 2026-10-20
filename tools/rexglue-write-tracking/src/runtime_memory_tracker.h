#pragma once
#include "write_tracking_state.h"
#include <rex/memory.h>
#include <array>
#include <memory>
namespace rex::memory { class Memory; class BaseHeap; }

namespace sr::tracking {
// Calls from the patched SDK serialize using its global critical region.
void TrackingAllocation(rex::memory::Memory*, uint32_t address, uint32_t length);
void TrackingRelease(rex::memory::Memory*, uint32_t address, uint32_t length);
void TrackingReset(rex::memory::Memory*);
void TrackingStopMemory(rex::memory::Memory*);
void TrackingHostProtection(rex::memory::Memory*, void* address, size_t length);
bool TrackingFault(rex::memory::Memory*, void* address, bool write);
// Synchronous host operations only: retain the scope until the kernel has
// stopped accessing its destination, including partial/error returns.
class HostWriteScope {
 public:
  HostWriteScope(rex::memory::Memory&, uint32_t physical_address, uint32_t length);
  HostWriteScope(rex::memory::Memory&, void* host_address, size_t length);
  ~HostWriteScope();
  HostWriteScope(const HostWriteScope&) = delete;
  HostWriteScope& operator=(const HostWriteScope&) = delete;
  void NotifyLegacyVirtualWrite();
 private:
  std::shared_ptr<void> lease_;
  std::array<RexTrackingRange, 5> ranges_{};
  uint32_t count_ = 0;
  bool registered_ = false;
  rex::memory::Memory* memory_ = nullptr;
  void* host_ = nullptr;
  size_t length_ = 0;
};
}  // namespace sr::tracking
