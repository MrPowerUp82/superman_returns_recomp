#include "../native/test_main.h"
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/system/xtypes.h>
#include <vector>
#include "../../port/src/guest_physical_allocation.h"

namespace {
constexpr uint32_t MiB = 1024 * 1024;
constexpr auto allocation = rex::memory::kMemoryAllocationReserve |
                            rex::memory::kMemoryAllocationCommit;
constexpr auto writable = rex::memory::kMemoryProtectRead |
                          rex::memory::kMemoryProtectWrite;

uint32_t Allocate(rex::memory::Memory& memory, uint32_t size, uint32_t page_size) {
  const uint32_t protection = rex::X_PAGE_READWRITE |
      (page_size == 65536 ? rex::X_MEM_LARGE_PAGES : 0);
  if (superman_returns::UseBottomUpPhysicalAllocation(size, UINT32_MAX, protection)) {
    return superman_returns::AllocateLargeGuestPhysical(memory, size, page_size, protection);
  }
  auto* heap = memory.LookupHeapByType(true, page_size);
  uint32_t address = 0;
  heap->Alloc(size, page_size, allocation, writable, true, &address);
  return address;
}

void Free(rex::memory::Memory& memory, uint32_t address) {
  if (address) SR_CHECK(memory.LookupHeap(address)->Release(address));
}
}  // namespace

SR_TEST(physical_level_buffer_can_reload_after_boot_arena_and_small_allocations) {
  rex::InitLogging();
  rex::memory::Memory memory;
  SR_CHECK(memory.Initialize());
  // Observed startup arena, followed by long-lived small allocations and the
  // Metropolis section being loaded, unloaded for Warworld, and loaded again.
  const uint32_t arena = Allocate(memory, 0x12B00000, 4096);
  const uint32_t pin_before = Allocate(memory, 2 * MiB, 4096);
  SR_CHECK(arena && pin_before);
  Free(memory, arena);
  const uint32_t level = Allocate(memory, 0x0DFF0000, 65536);
  SR_CHECK(level != 0);
  const uint32_t pin_during = Allocate(memory, 2 * MiB, 4096);
  SR_CHECK(pin_during != 0);
  Free(memory, level);
  const uint32_t pin_after = Allocate(memory, MiB, 4096);
  SR_CHECK(pin_after != 0);
  SR_CHECK(memory.GetPhysicalHeap()->GetUnreservedPageCount() * uint64_t{4096} >
           400 * MiB);
  const uint32_t reloaded = Allocate(memory, 0x0DFF0000, 65536);
  SR_CHECK(reloaded != 0);
  if (reloaded) {
    auto* heap = static_cast<rex::memory::PhysicalHeap*>(memory.LookupHeap(reloaded));
    SR_CHECK_EQ(heap->GetPhysicalAddress(reloaded) % 65536, 0u);
    auto* bytes = memory.TranslateVirtual<uint8_t*>(reloaded);
    bytes[0] = 0x37;
    bytes[0x0DFF0000 - 1] = 0x91;
    const auto* physical = memory.TranslatePhysical<const uint8_t*>(heap->GetPhysicalAddress(reloaded));
    SR_CHECK_EQ(physical[0], 0x37u);
    SR_CHECK_EQ(physical[0x0DFF0000 - 1], 0x91u);
  }
  Free(memory, reloaded);
  Free(memory, pin_after);
  Free(memory, pin_during);
  Free(memory, pin_before);
}

SR_TEST(physical_allocation_policy_preserves_small_fixed_and_unrecognized_requests) {
  using superman_returns::UseBottomUpPhysicalAllocation;
  SR_CHECK(!UseBottomUpPhysicalAllocation(16 * MiB - 1, UINT32_MAX, rex::X_PAGE_READWRITE));
  SR_CHECK(UseBottomUpPhysicalAllocation(16 * MiB, UINT32_MAX, rex::X_PAGE_READWRITE));
  SR_CHECK(UseBottomUpPhysicalAllocation(0x0DFF0000, UINT32_MAX, 0x20000404));
  SR_CHECK(!UseBottomUpPhysicalAllocation(32 * MiB, 0x18000000, rex::X_PAGE_READWRITE));
  SR_CHECK(!UseBottomUpPhysicalAllocation(UINT32_MAX, UINT32_MAX, rex::X_PAGE_READWRITE));
  SR_CHECK(!UseBottomUpPhysicalAllocation(32 * MiB, UINT32_MAX, rex::X_PAGE_NOACCESS));
  SR_CHECK(!UseBottomUpPhysicalAllocation(32 * MiB, UINT32_MAX,
      rex::X_PAGE_READWRITE | rex::X_PAGE_GUARD));
  SR_CHECK(!UseBottomUpPhysicalAllocation(32 * MiB, UINT32_MAX,
      rex::X_PAGE_READWRITE | rex::X_PAGE_READONLY));
}

SR_TEST(physical_level_buffer_survives_repeated_streaming_with_resident_objectives) {
  rex::InitLogging();
  rex::memory::Memory memory;
  SR_CHECK(memory.Initialize());
  const uint32_t arena = Allocate(memory, 0x12B00000, 4096);
  const uint32_t pin = Allocate(memory, 2 * MiB, 4096);
  SR_CHECK(arena && pin);
  Free(memory, arena);
  const uint32_t objective = Allocate(memory, 0x02B30000, 65536);
  SR_CHECK(objective != 0);
  std::vector<uint32_t> small;
  for (unsigned i = 0; i < 20; ++i) {
    const uint32_t level = Allocate(memory, 0x0DFF0000, 65536);
    SR_CHECK(level != 0);
    if (!level) break;
    Free(memory, level);
    small.push_back(Allocate(memory, MiB, 4096));
    SR_CHECK(small.back() != 0);
  }
  SR_CHECK_EQ(small.size(), 20u);
  for (auto address : small) Free(memory, address);
  Free(memory, objective);
  Free(memory, pin);
}

SR_TEST(physical_allocations_preserve_page_rounding_alignment_protection_and_release) {
  rex::InitLogging();
  rex::memory::Memory memory;
  SR_CHECK(memory.Initialize());
  const uint32_t free_before = memory.GetPhysicalHeap()->GetUnreservedPageCount();
  for (const uint32_t page_flag : {0u, uint32_t(rex::X_MEM_LARGE_PAGES), uint32_t(rex::X_MEM_16MB_PAGES)}) {
    const uint32_t page_size = page_flag == rex::X_MEM_16MB_PAGES ? 16 * MiB :
        page_flag == rex::X_MEM_LARGE_PAGES ? 65536 : 4096;
    const uint32_t expected_base = page_flag == rex::X_MEM_16MB_PAGES ? 0xC0000000 :
        page_flag == rex::X_MEM_LARGE_PAGES ? 0xA0000000 : 0xE0000000;
    for (const uint32_t alignment : {0u, 32768u, 262144u}) {
      const uint32_t address = superman_returns::AllocateLargeGuestPhysical(memory,
          16 * MiB + 1, alignment, page_flag | rex::X_PAGE_READWRITE | rex::X_PAGE_WRITECOMBINE);
      SR_CHECK(address != 0);
      if (!address) continue;
      SR_CHECK_EQ(address & 0xE0000000, expected_base);
      auto* heap = static_cast<rex::memory::PhysicalHeap*>(memory.LookupHeap(address));
      const uint32_t physical = heap->GetPhysicalAddress(address);
      SR_CHECK(physical >= 65536);
      SR_CHECK_EQ(physical % page_size, 0u);
      if (alignment) SR_CHECK_EQ(physical % alignment, 0u);
      uint32_t size = 0, protect = 0;
      SR_CHECK(heap->QuerySize(address, &size));
      SR_CHECK_EQ(size, ((16 * MiB + page_size) / page_size) * page_size);
      SR_CHECK(heap->QueryProtect(address, &protect));
      SR_CHECK_EQ(protect, uint32_t(writable | rex::memory::kMemoryProtectWriteCombine));
      auto* bytes = memory.TranslateVirtual<uint8_t*>(address);
      bytes[0] = 0x57; bytes[size - 1] = 0xA9;
      const auto* alias = memory.TranslatePhysical<const uint8_t*>(physical);
      SR_CHECK_EQ(alias[0], 0x57u); SR_CHECK_EQ(alias[size - 1], 0xA9u);
      Free(memory, address);
      SR_CHECK_EQ(memory.GetPhysicalHeap()->GetUnreservedPageCount(), free_before);
    }
  }
  const uint32_t readonly = superman_returns::AllocateLargeGuestPhysical(memory,
      16 * MiB, 65536, rex::X_PAGE_READONLY | rex::X_PAGE_NOCACHE | rex::X_MEM_LARGE_PAGES);
  SR_CHECK(readonly != 0);
  if (readonly) {
    uint32_t protect = 0;
    SR_CHECK(memory.LookupHeap(readonly)->QueryProtect(readonly, &protect));
    SR_CHECK_EQ(protect, uint32_t(rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectNoCache));
    Free(memory, readonly);
  }
}

SR_TEST(physical_allocation_failure_leaves_the_heap_available_for_smaller_requests) {
  rex::InitLogging();
  rex::memory::Memory memory;
  SR_CHECK(memory.Initialize());
  const uint32_t free_before = memory.GetPhysicalHeap()->GetUnreservedPageCount();
  // The 512 MiB space includes reserved pages, so a single 512 MiB request
  // must fail without retaining a partial reservation.
  SR_CHECK_EQ(superman_returns::AllocateLargeGuestPhysical(memory, 0x20000000, 65536,
      uint32_t(rex::X_PAGE_READWRITE) | rex::X_MEM_LARGE_PAGES), 0u);
  SR_CHECK_EQ(memory.GetPhysicalHeap()->GetUnreservedPageCount(), free_before);
  const uint32_t address = Allocate(memory, 0x0DFF0000, 65536);
  SR_CHECK(address != 0);
  Free(memory, address);
  SR_CHECK_EQ(memory.GetPhysicalHeap()->GetUnreservedPageCount(), free_before);
}
