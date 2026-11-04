#include "../../port/src/coop_idle_policy.h"
#include "test_main.h"
#include <limits>
using namespace superman_returns::coop;
SR_TEST(coop_idle_only_primary_owns_global_tracker_with_a_live_owned_binding) {
  IdleBinding binding{1,8780,0xC0000001};
  SR_CHECK(SelectIdleRoute(binding,1,0)==IdleRoute::Primary);
  SR_CHECK(SelectIdleRoute(binding,8780,1)==IdleRoute::Secondary);
  SR_CHECK(SelectIdleRoute(binding,8780,0)==IdleRoute::Native);
  SR_CHECK(SelectIdleRoute(binding,1,1)==IdleRoute::Native);
  SR_CHECK(SelectIdleRoute(binding,99,0)==IdleRoute::Native);
}
SR_TEST(coop_idle_invalid_or_cleared_binding_preserves_native_actor_updates) {
  for(auto binding:{IdleBinding{},IdleBinding{1,8780,0},IdleBinding{1,8780,0xC0000000},
                   IdleBinding{1,8780,0x80000001},IdleBinding{1,1,0xC0000001},
                   IdleBinding{0,8780,0xC0000001},IdleBinding{1,255,0xC0000001}}) {
    SR_CHECK(SelectIdleRoute(binding,1,0)==IdleRoute::Native);
    SR_CHECK(SelectIdleRoute(binding,8780,1)==IdleRoute::Native);
  }
}
SR_TEST(coop_idle_activity_on_either_port_prevents_shared_inactivity) {
  SR_CHECK(CombinedIdle(35,0.1)==0.1);
  SR_CHECK(CombinedIdle(0.1,35)==0.1);
  SR_CHECK(CombinedIdle(35,40)==35);
  SR_CHECK(CombinedIdle(0,0)==0);
}
SR_TEST(coop_idle_invalid_secondary_time_cannot_override_native_result) {
  for(double value:{-1.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
    SR_CHECK(CombinedIdle(35,value)==35);
  SR_CHECK(CombinedIdle(-1,2)==-1);
}
SR_TEST(coop_idle_float_timestamp_roundoff_counts_as_current_input) {
  SR_CHECK(CombinedIdle(67.7,-0.0000072758593887556344)==0);
  SR_CHECK(CombinedIdle(67.7,-0.001)==0);
  SR_CHECK(CombinedIdle(67.7,-0.0011)==67.7);
}
