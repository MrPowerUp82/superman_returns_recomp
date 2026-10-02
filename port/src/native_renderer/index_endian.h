#pragma once

#include <cstdint>

namespace superman_returns::native {

// Xenos DMA modes: none, 8-in-16, 8-in-32, 16-in-32.
constexpr uint32_t IndexByteXor(uint32_t endian) {
  constexpr uint32_t masks[] = {0, 1, 3, 2};
  return masks[endian & 3];
}

inline uint32_t LoadIndex(const uint8_t* bytes, uint32_t index, bool index32,
                          uint32_t endian) {
  uint32_t width = index32 ? 4 : 2;
  uint32_t offset = index * width, value = 0;
  for (uint32_t b = 0; b < width; ++b)
    value |= uint32_t(bytes[(offset + b) ^ IndexByteXor(endian)]) << (8 * b);
  return value;
}

}  // namespace superman_returns::native
