#include "test_main.h"
#include "resource_write_tracking.h"

using namespace superman_returns::native;
namespace {
RexMemoryTrackingApiV1 UsableApi() {
  return {1, sizeof(RexMemoryTrackingApiV1), 15,
      [](void*, RexTrackingSession* out) { *out = 1; return RexTrackingStatus::Ok; },
      [](RexTrackingSession) {},
      [](RexTrackingSession, const RexTrackingRange*, uint32_t, RexTrackingWatch* out) { *out = 2; return RexTrackingStatus::Ok; },
      [](RexTrackingSession, RexTrackingWatch) {},
      [](RexTrackingSession, RexTrackingWatch, RexTrackingStamp*, uint32_t) { return RexTrackingStatus::Ok; },
      [](RexTrackingSession, RexTrackingWatch) {},
      [](RexTrackingSession, RexTrackingStats*) {}};
}
}
SR_TEST(tracking_api_requires_complete_versioned_contract) {
  auto api = UsableApi();
  SR_CHECK(IsTrackingApiUsable(&api));
  SR_CHECK(!IsTrackingApiUsable(nullptr));
  api.version = 2; SR_CHECK(!IsTrackingApiUsable(&api));
  api = UsableApi(); api.size -= 1; SR_CHECK(!IsTrackingApiUsable(&api));
  for (uint64_t bit : {1ull, 2ull, 4ull, 8ull}) {
    api = UsableApi(); api.capabilities &= ~bit; SR_CHECK(!IsTrackingApiUsable(&api));
  }
  api = UsableApi(); api.start = nullptr; SR_CHECK(!IsTrackingApiUsable(&api));
  api = UsableApi(); api.stop = nullptr; SR_CHECK(!IsTrackingApiUsable(&api));
  api = UsableApi(); api.watch = nullptr; SR_CHECK(!IsTrackingApiUsable(&api));
  api = UsableApi(); api.unwatch = nullptr; SR_CHECK(!IsTrackingApiUsable(&api));
  api = UsableApi(); api.sample = nullptr; SR_CHECK(!IsTrackingApiUsable(&api));
  api = UsableApi(); api.invalidate = nullptr; SR_CHECK(!IsTrackingApiUsable(&api));
  api = UsableApi(); api.stats = nullptr; SR_CHECK(!IsTrackingApiUsable(&api));
}
