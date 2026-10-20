#include "../native/test_main.h"
#include "../../port/src/native_renderer/resource_write_tracking.h"
#include <rex/system/xmemory.h>
#include <rex/logging.h>

SR_TEST(tracking_client_with_stock_dll_falls_back_without_guard_installation) {
  rex::InitLogging();
  rex::memory::Memory memory;
  SR_CHECK(memory.Initialize());
  superman_returns::native::ResourceWriteTracker tracker;
  SR_CHECK(!tracker.Start(memory));
  SR_CHECK(tracker.last_status() == RexTrackingStatus::Unsupported);
  const RexTrackingRange range{0x10000, 1};
  SR_CHECK_EQ(tracker.Watch(std::span(&range, 1)), 0u);
  RexTrackingStamp stamp{1, 2, 3, 0, RexTrackingCoverage::Complete};
  SR_CHECK(!tracker.Sample(1, std::span(&stamp, 1)));
  SR_CHECK(stamp.coverage != RexTrackingCoverage::Complete);
  tracker.Unwatch(1); tracker.Invalidate(1); tracker.Stop();
  *memory.TranslatePhysical<volatile uint8_t*>(0x10000) = 37;
  SR_CHECK_EQ(*memory.TranslatePhysical<volatile uint8_t*>(0x10000), 37u);
}
