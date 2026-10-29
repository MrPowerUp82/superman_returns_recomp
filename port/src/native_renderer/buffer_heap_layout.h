#pragma once
#include <cstdint>
#include <limits>
#include <optional>

namespace superman_returns::native {
struct BufferHeapAllocation { uint64_t offset, end; };
inline std::optional<BufferHeapAllocation> PlanBufferHeapAllocation(
    uint64_t cursor, uint64_t capacity, uint64_t bytes, uint64_t alignment) {
  if (!bytes || !alignment || (alignment & (alignment - 1)) || cursor > capacity)
    return std::nullopt;
  const uint64_t mask = alignment - 1;
  if (cursor > std::numeric_limits<uint64_t>::max() - mask) return std::nullopt;
  const uint64_t offset = (cursor + mask) & ~mask;
  if (offset > capacity || bytes > capacity - offset) return std::nullopt;
  return BufferHeapAllocation{offset, offset + bytes};
}
}  // namespace superman_returns::native
