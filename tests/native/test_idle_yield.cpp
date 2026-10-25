#include "../../port/src/native_renderer/idle_yield.h"
#include "test_main.h"
using superman_returns::native::IdleYieldBackoff;
using superman_returns::native::VblankPacer;
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

SR_TEST(vblank_deadline_keeps_phase_with_small_jitter) {
  VblankPacer pacer(1000, 100);
  SR_CHECK(pacer.Remaining(1000) == 100);
  SR_CHECK(pacer.Remaining(1099) == 1);
  SR_CHECK(pacer.Remaining(1103) == 0);
  pacer.Complete(1103);
  SR_CHECK(pacer.Remaining(1103) == 97);
  pacer.Complete(1205);
  SR_CHECK(pacer.Remaining(1205) == 95);
}
SR_TEST(vblank_missed_deadlines_do_not_trigger_catchup_burst) {
  VblankPacer pacer(1000, 100);
  pacer.Complete(1450);
  SR_CHECK(pacer.Remaining(1450) == 100);
  SR_CHECK(pacer.Remaining(1549) == 1);
  pacer.Complete(1553);
  SR_CHECK(pacer.Remaining(1553) == 97);
  VblankPacer zero_interval(10, 0);
  SR_CHECK(zero_interval.Remaining(10) == 1);
}
