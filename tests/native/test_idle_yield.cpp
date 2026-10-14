#include "../../port/src/native_renderer/idle_yield.h"
#include "test_main.h"
using superman_returns::native::IdleYieldBackoff;
SR_TEST(idle_yields_only_park_after_a_hot_burst) {
  IdleYieldBackoff backoff;
  for(uint64_t i=0;i<7;++i) SR_CHECK(!backoff.Observe(i*1000));
  SR_CHECK(backoff.Observe(7000));
  for(uint64_t i=8;i<1000;++i) SR_CHECK(backoff.Observe(i*1000));
}
SR_TEST(useful_work_and_clock_reset_restore_immediate_yields) {
  IdleYieldBackoff backoff;
  for(uint64_t i=0;i<8;++i) backoff.Observe(i*1000);
  SR_CHECK(!backoff.Observe(1007000));
  SR_CHECK(!backoff.Observe(1008000));
  SR_CHECK(!backoff.Observe(1));
  IdleYieldBackoff another_thread;
  SR_CHECK(!another_thread.Observe(1));
}
