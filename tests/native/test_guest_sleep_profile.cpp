#include "../../port/src/native_renderer/guest_sleep_profile.h"
#include "test_main.h"
using superman_returns::native::GuestSleepWindow;

SR_TEST(guest_sleep_profile_groups_by_caller_and_requested_delay) {
  GuestSleepWindow window;
  window.Record(0x824822C4,33,34000000);
  window.Record(0x824822C4,33,35000000);
  window.Record(0x824822C4,1,2000000);
  window.Record(0x82100004,33,33000000);
  SR_CHECK_EQ(window.calls,4);
  SR_CHECK_EQ(window.elapsed_ns,104000000);
  SR_CHECK_EQ(window.callers[0].calls,2);
  SR_CHECK_EQ(window.callers[0].elapsed_ns,69000000);
  SR_CHECK_EQ(window.callers[0].max_ns,35000000);
  SR_CHECK_EQ(window.callers[1].requested_ms,1);
  SR_CHECK_EQ(window.callers[1].calls,1);
  SR_CHECK_EQ(window.callers[2].caller,0x82100004);
}
SR_TEST(guest_sleep_profile_overflow_preserves_movie_caller_and_totals) {
  GuestSleepWindow window;
  for(uint32_t i=0;i<100;++i) window.Record(0x82100000+4*i,1,100);
  window.Record(0x824822C4,33,33000000);
  SR_CHECK_EQ(window.calls,101);
  SR_CHECK_EQ(window.elapsed_ns,33010000);
  SR_CHECK_EQ(window.overflow_calls,69);
  SR_CHECK_EQ(window.overflow_ns,6900);
  SR_CHECK_EQ(window.callers[0].calls,1);
}
SR_TEST(guest_sleep_profile_windows_reset_and_keep_infinite_request_distinct) {
  GuestSleepWindow window;
  window.Record(0x82100004,0xffffffff,500);
  SR_CHECK_EQ(window.callers[1].requested_ms,0xffffffff);
  window={};
  SR_CHECK_EQ(window.calls,0);
  SR_CHECK_EQ(window.overflow_calls,0);
  window.Record(0x824822C4,33,33000000);
  SR_CHECK_EQ(window.callers[0].caller,0x824822C4);
  SR_CHECK_EQ(window.callers[0].calls,1);
  SR_CHECK_EQ(window.callers[1].calls,0);
}
