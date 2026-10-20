#pragma once
#include "write_tracking_state.h"
#include <rex/memory.h>
namespace rex::memory { class Memory; class BaseHeap; }

namespace sr::tracking {
// Calls from the patched SDK serialize using its global critical region.
void TrackingAllocation(rex::memory::Memory*, uint32_t address, uint32_t length);
void TrackingRelease(rex::memory::Memory*, uint32_t address, uint32_t length);
void TrackingReset(rex::memory::Memory*);
void TrackingStopMemory(rex::memory::Memory*);
void TrackingHostProtection(rex::memory::Memory*, void* address, size_t length);
bool TrackingFault(rex::memory::Memory*, void* address, bool write);
}  // namespace sr::tracking
