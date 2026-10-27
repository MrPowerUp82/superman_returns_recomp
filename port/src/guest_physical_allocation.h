#pragma once
#include <cstdint>

namespace rex::memory { class Memory; }

namespace superman_returns {
// Keep the boot arena and large streamed sections at the low end of physical
// memory. The stock runtime places small, long-lived allocations at the top.
bool UseBottomUpPhysicalAllocation(uint32_t size, uint32_t physical_address,
                                  uint32_t protection);
uint32_t AllocateLargeGuestPhysical(rex::memory::Memory& memory, uint32_t size,
                                    uint32_t alignment, uint32_t protection);
}  // namespace superman_returns
