#include "index_endian.h"
#include "test_main.h"

using superman_returns::native::IndexByteXor;
using superman_returns::native::LoadIndex;

SR_TEST(index16_dword_swap_preserves_triangle_order) {
  const uint8_t bytes[] = {0, 2, 0, 1, 0, 4, 0, 3, 0xFF, 0xFF, 0, 5};
  for (uint32_t i = 0; i < 5; ++i)
    SR_CHECK_EQ(LoadIndex(bytes, i, false, 2), i + 1);
  SR_CHECK_EQ(LoadIndex(bytes, 5, false, 2), 0xFFFFu);
}

SR_TEST(index_endian_modes_match_byte_permutations) {
  const uint8_t decoded[] = {1, 2, 3, 4, 5, 6, 7, 8};
  for (uint32_t endian = 0; endian < 4; ++endian) {
    uint8_t encoded[8];
    for (uint32_t i = 0; i < 8; ++i)
      encoded[i ^ IndexByteXor(endian)] = decoded[i];
    SR_CHECK_EQ(LoadIndex(encoded, 0, true, endian), 0x04030201u);
    SR_CHECK_EQ(LoadIndex(encoded, 1, true, endian), 0x08070605u);
    SR_CHECK_EQ(LoadIndex(encoded, 1, false, endian), 0x0403u);
    SR_CHECK_EQ(LoadIndex(encoded, 2, false, endian), 0x0605u);
  }
}
