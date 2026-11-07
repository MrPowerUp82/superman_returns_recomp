#include "test_main.h"
#include "../../port/src/native_renderer/slot_srv_cache.h"

using superman_returns::native::SlotSrvCache;

SR_TEST(slot_srv_cache_retains_more_than_8_distinct_fetches_in_same_frame) {
  SlotSrvCache cache;
  const uint64_t frame = 42;

  for (uint32_t i = 0; i < 16; ++i) {
    uint32_t fetch[6] = {0x2, (i + 1) * 0x10000u, 0x01000100u, 0, 0, 0};
    SR_CHECK(cache.Find(frame, fetch) == nullptr);
    cache.Insert(frame, fetch, 100 + i, i + 1, false);
  }

  uint32_t hits = 0;
  for (uint32_t i = 0; i < 16; ++i) {
    uint32_t fetch[6] = {0x2, (i + 1) * 0x10000u, 0x01000100u, 0, 0, 0};
    if (auto* entry = cache.Find(frame, fetch)) {
      SR_CHECK_EQ(entry->srv, 100u + i);
      ++hits;
    }
  }
  SR_CHECK(hits >= 14u);
}

SR_TEST(slot_srv_cache_targeted_invalidation_only_drops_matching_base) {
  SlotSrvCache cache;
  const uint64_t frame = 10;
  uint32_t fetch_a[6] = {0x2, 0x01000000u, 0x01000100u, 0, 0, 0};
  uint32_t fetch_b[6] = {0x2, 0x02000000u, 0x01000100u, 0, 0, 0};
  cache.Insert(frame, fetch_a, 11, 1, false);
  cache.Insert(frame, fetch_b, 22, 2, true);

  cache.Invalidate(0x01000000u, [](uint32_t a) { return a & 0x1FFFFFFFu; });
  SR_CHECK(cache.Find(frame, fetch_a) == nullptr);
  auto* b = cache.Find(frame, fetch_b);
  SR_CHECK(b != nullptr);
  SR_CHECK_EQ(b->srv, 22u);
  SR_CHECK(b->smooth);
}
