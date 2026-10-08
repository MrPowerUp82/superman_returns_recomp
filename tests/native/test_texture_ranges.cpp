#include "../../port/src/graphics/guest/texture_ranges.h"
#include "test_main.h"
#include <array>

using namespace superman_returns::graphics::guest;
SR_TEST(texture_ranges_merge_duplicates_nested_overlaps_and_adjacency_but_keep_gaps) {
  std::vector<TextureRange> ranges{{0x9000, 0x1000}, {0x1000, 0x2000},
    {0x2000, 0x2000}, {0x1000, 0x2000}, {0x4000, 0x1000}, {0x2800, 0x100}, {0x8000, 0}};
  CoalesceTextureRanges(ranges);
  SR_CHECK_EQ(ranges.size(), 2u);
  if (ranges.size() != 2) return;
  SR_CHECK_EQ(ranges[0].address, 0x1000u);
  SR_CHECK_EQ(ranges[0].length, 0x4000u);
  SR_CHECK_EQ(ranges[1].address, 0x9000u);
  SR_CHECK_EQ(ranges[1].length, 0x1000u);
  CoalesceTextureRanges(ranges);
  SR_CHECK_EQ(ranges.size(), 2u);
  SR_CHECK_EQ(ranges[0].length, 0x4000u);
}
SR_TEST(texture_ranges_handle_empty_and_physical_arena_end) {
  std::vector<TextureRange> ranges;
  CoalesceTextureRanges(ranges);
  SR_CHECK(ranges.empty());
  ranges = {{0x1ffff000, 0x1000}, {0x1fffe000, 0x1800}};
  CoalesceTextureRanges(ranges);
  SR_CHECK_EQ(ranges.size(), 1u);
  if (ranges.size() != 1) return;
  SR_CHECK_EQ(ranges[0].address, 0x1fffe000u);
  SR_CHECK_EQ(ranges[0].length, 0x2000u);
}
SR_TEST(texture_ranges_union_preserves_exact_byte_coverage_for_varied_inputs) {
  uint32_t state = 12345;
  auto next = [&] { state = state * 1664525u + 1013904223u; return state; };
  for (uint32_t trial = 0; trial < 256; ++trial) {
    std::array<bool, 256> expected{}, actual{};
    std::vector<TextureRange> ranges;
    for (uint32_t i = 0; i < 12; ++i) {
      uint32_t address = (next() >> 16) % 256;
      uint32_t length = std::min((next() >> 16) % 64, 256 - address);
      ranges.push_back({address, length});
      for (uint32_t j = address; j < address + length; ++j) expected[j] = true;
    }
    CoalesceTextureRanges(ranges);
    uint32_t previous_end = 0;
    bool first = true;
    for (const auto& range : ranges) {
      SR_CHECK(range.length > 0);
      SR_CHECK(first || range.address > previous_end);
      previous_end = range.address + range.length;
      first = false;
      for (uint32_t j = range.address; j < previous_end; ++j) actual[j] = true;
    }
    SR_CHECK(actual == expected);
  }
}
