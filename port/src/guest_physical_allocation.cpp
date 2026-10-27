#include "guest_physical_allocation.h"
#include <rex/system/xmemory.h>
#include <rex/system/xtypes.h>

namespace superman_returns {
using namespace rex;

bool UseBottomUpPhysicalAllocation(uint32_t size, uint32_t physical_address,
                                  uint32_t protection) {
  constexpr uint32_t supported = X_PAGE_READONLY | X_PAGE_READWRITE |
      X_PAGE_NOCACHE | X_PAGE_WRITECOMBINE | X_MEM_LARGE_PAGES | X_MEM_16MB_PAGES;
  const uint32_t access = protection & (X_PAGE_READONLY | X_PAGE_READWRITE);
  return size >= 16 * 1024 * 1024 && size <= 0x20000000 &&
      physical_address == UINT32_MAX && !(protection & ~supported) &&
      (access == X_PAGE_READONLY || access == X_PAGE_READWRITE);
}

uint32_t AllocateLargeGuestPhysical(rex::memory::Memory& memory, uint32_t size,
                                    uint32_t alignment, uint32_t protection) {
  if (!UseBottomUpPhysicalAllocation(size, UINT32_MAX, protection)) return 0;
  // Match MmAllocatePhysicalMemoryEx's page selection and protection mapping.
  const uint32_t page_size = protection & X_MEM_LARGE_PAGES ? 65536 :
      protection & X_MEM_16MB_PAGES ? 16 * 1024 * 1024 : 4096;
  auto* heap = static_cast<rex::memory::PhysicalHeap*>(
      memory.LookupHeapByType(true, page_size));
  uint32_t access = rex::memory::kMemoryProtectRead;
  if (protection & X_PAGE_READWRITE) access |= rex::memory::kMemoryProtectWrite;
  if (protection & X_PAGE_NOCACHE) access |= rex::memory::kMemoryProtectNoCache;
  if (protection & X_PAGE_WRITECOMBINE) access |= rex::memory::kMemoryProtectWriteCombine;
  // Keep physical address zero unavailable to these buffers: GPU descriptors
  // and game resource code can use it as a null address.
  const uint32_t physical_base = heap->GetPhysicalAddress(heap->heap_base());
  const uint32_t low = heap->heap_base() +
      (physical_base < 65536 ? 65536 - physical_base : 0);
  uint32_t address = 0;
  heap->AllocRange(low, heap->heap_base() + heap->heap_size() - 1, size, alignment,
      rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
      access, false, &address);
  return address;
}
}  // namespace superman_returns
