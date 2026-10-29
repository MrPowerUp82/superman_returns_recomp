#include "buffer_heap_layout.h"
#include "test_main.h"
#include <limits>
using namespace superman_returns::native;

SR_TEST(buffer_heap_layout_respects_placement_alignment_and_exact_capacity) {
  auto a = PlanBufferHeapAllocation(17, 3 * 65536, 65536, 65536);
  SR_CHECK(a.has_value());
  if (a) { SR_CHECK_EQ(a->offset, 65536u); SR_CHECK_EQ(a->end, 131072u); }
  auto b = PlanBufferHeapAllocation(131072, 3 * 65536, 65536, 65536);
  SR_CHECK(b.has_value());
  if (b) SR_CHECK_EQ(b->end, 3 * 65536u);
  SR_CHECK(!PlanBufferHeapAllocation(131073, 3 * 65536, 65536, 65536));
}

SR_TEST(buffer_heap_layout_never_overlaps_live_resources) {
  uint64_t cursor = 0;
  for (uint64_t i = 0; i < 256; ++i) {
    const auto a = PlanBufferHeapAllocation(cursor, 16u << 20, 65536, 65536);
    SR_CHECK(a.has_value());
    if (!a) break;
    SR_CHECK(a->offset >= cursor);
    SR_CHECK_EQ(a->offset, i * 65536);
    cursor = a->end;
  }
  SR_CHECK_EQ(cursor, 16u << 20);
  SR_CHECK(!PlanBufferHeapAllocation(cursor, 16u << 20, 65536, 65536));
}

SR_TEST(buffer_heap_layout_rejects_invalid_or_overflowing_driver_requests) {
  const uint64_t max = std::numeric_limits<uint64_t>::max();
  SR_CHECK(!PlanBufferHeapAllocation(0, 65536, 0, 65536));
  SR_CHECK(!PlanBufferHeapAllocation(0, 65536, 1, 0));
  SR_CHECK(!PlanBufferHeapAllocation(0, 65536, 1, 3));
  SR_CHECK(!PlanBufferHeapAllocation(65537, 65536, 1, 1));
  SR_CHECK(!PlanBufferHeapAllocation(max - 1, max, 1, 65536));
  SR_CHECK(!PlanBufferHeapAllocation(1, max, max, 1));
  SR_CHECK(!PlanBufferHeapAllocation(0, 16u << 20, 32u << 20, 65536));
}
