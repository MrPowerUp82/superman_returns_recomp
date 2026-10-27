// Observe level-loading failures and keep large physical allocations contiguous.
// Warworld -> Metropolis: both APIs fail a physical allocation, then issue a
// 0x0DFE54F2-byte read into guest address zero. The existing runtime log omits
// the allocation size, caller, free ranges and source file.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include <rex/hook.h>
#include <rex/system/xfile.h>
#include <rex/system/xmemory.h>

#include "generated/default/superman_returns_init.h"
#include "guest_physical_allocation.h"

namespace {
constexpr uint32_t kLargeRequest = 16 * 1024 * 1024;

bool ReadWord(const uint8_t* base, uint32_t address, uint32_t& word) {
  SIZE_T copied = 0;
  uint32_t value = 0;
  if (!ReadProcessMemory(GetCurrentProcess(), base + address, &value,
                         sizeof(value), &copied) || copied != sizeof(value)) {
    return false;
  }
  word = __builtin_bswap32(value);
  return true;
}

std::string GuestCallers(const PPCContext& ctx, const uint8_t* base) {
  std::string callers = fmt::format("{:08X}", ctx.lr);
  uint32_t stack = ctx.r1.u32;
  for (unsigned i = 0; i < 12; ++i) {
    uint32_t parent = 0, caller = 0;
    if (!ReadWord(base, stack, parent) || parent <= stack ||
        parent - stack > 1024 * 1024 ||
        !ReadWord(base, parent - 8, caller)) break;
    callers += fmt::format(" {:08X}", caller);
    stack = parent;
  }
  return callers;
}

void LogPhysicalSpace(uint32_t size, uint32_t alignment, uint32_t protect,
                      uint32_t caller, uint32_t result) {
  auto* heap = REX_KERNEL_MEMORY()->GetPhysicalHeap();
  uint64_t free_bytes = 0;
  uint32_t largest = 0;
  // QueryRegionInfo takes the heap lock for each region. These are diagnostic
  // observations, not a reservation or a guarantee against concurrent allocs.
  for (uint64_t address = heap->heap_base(), end = address + heap->heap_size();
       address < end;) {
    rex::memory::HeapAllocationInfo info{};
    if (!heap->QueryRegionInfo(uint32_t(address), &info) || !info.region_size) break;
    const uint64_t next = uint64_t(info.base_address) + info.region_size;
    if (next <= address) break;
    if (!info.state) {
      const auto bytes = uint32_t(std::min(next, end) - address);
      free_bytes += bytes;
      largest = std::max(largest, bytes);
    }
    address = next;
  }
  REXLOG_INFO("[guest-memory] physical size={:08X} alignment={:08X} "
              "protect={:08X} caller={:08X} result={:08X} "
              "free_bytes={} largest_free_bytes={}",
              size, alignment, protect, caller, result, free_bytes, largest);
}
}  // namespace

REX_HOOK_RAW(sub_82468008) {
  const uint32_t size = ctx.r3.u32;
  const uint32_t alignment = ctx.r5.u32;
  const uint32_t protect = ctx.r6.u32;
  const uint32_t caller = uint32_t(ctx.lr);
  const bool large = size >= kLargeRequest;
  const std::string callers = large ? GuestCallers(ctx, base) : std::string{};
  static const bool bottom_up_enabled = [] {
    const char* value = std::getenv("SR_LARGE_PHYSICAL_BOTTOM_UP");
    return !value || *value != '0';
  }();
  uint32_t address = 0, disable_large_pages = 0;
  // XPhysicalAllocEx removes LARGE_PAGES when this game/XDK mode is enabled.
  // Preserve that behavior before selecting a physical heap.
  const bool mode_read = bottom_up_enabled && large && ctx.r4.u32 == UINT32_MAX &&
      ReadWord(base, 0x82976A94, disable_large_pages);
  const uint32_t effective_protect = disable_large_pages ? protect & ~0x20000000u : protect;
  if (mode_read && superman_returns::UseBottomUpPhysicalAllocation(
          size, ctx.r4.u32, effective_protect)) {
    address = superman_returns::AllocateLargeGuestPhysical(
        *REX_KERNEL_MEMORY(), size, alignment, effective_protect);
  }
  if (address) ctx.r3.u64 = address;
  else __imp__sub_82468008(ctx, base);  // Keep normal failure/last-error handling.
  if (large || !ctx.r3.u32) {
    REXLOG_INFO("[guest-memory] allocation policy={}", address ? "bottom_up" : "original");
    LogPhysicalSpace(size, alignment, protect, caller, ctx.r3.u32);
    if (large) REXLOG_INFO("[guest-memory] allocation callers={}", callers);
  }
}

REX_HOOK_RAW(sub_824685C8) {
  // XDK ReadFile(handle, buffer, length, bytes_read, overlapped).
  if (ctx.r5.u32 >= kLargeRequest || (!ctx.r4.u32 && ctx.r5.u32)) {
    const auto file = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XFile>(ctx.r3.u32);
    REXLOG_INFO("[guest-memory] read file='{}' handle={:08X} buffer={:08X} "
                "length={:08X} position={} overlapped={:08X} callers={}",
                file ? file->path() : "<unknown>", ctx.r3.u32, ctx.r4.u32,
                ctx.r5.u32, file ? file->position() : 0, ctx.r7.u32,
                GuestCallers(ctx, base));
  }
  __imp__sub_824685C8(ctx, base);
}
