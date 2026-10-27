#include "vblank_policy.h"
#include "test_main.h"
using namespace superman_returns::native;

SR_TEST(canary_unlimited_requires_no_vsync_and_has_no_240hz_fallback) {
  const auto unlocked=SelectNativeVblankCadence(0,false);
  SR_CHECK(unlocked.unlocked);
  SR_CHECK_EQ(unlocked.hz,1000u);
  const auto synced=SelectNativeVblankCadence(0,true);
  SR_CHECK(!synced.unlocked);
  SR_CHECK_EQ(synced.hz,60u);
  SR_CHECK_EQ(SelectNativeVblankCadence(0,true,240).hz,60u);
}
SR_TEST(native_fps_choices_keep_their_existing_two_vblank_units) {
  for(bool vsync:{false,true}) {
    for(int32_t fps:{30,60,120}) {
      const auto limited=SelectNativeVblankCadence(fps,vsync);
      SR_CHECK(!limited.unlocked);
      SR_CHECK_EQ(limited.hz,uint32_t(fps*2));
    }
  }
}
SR_TEST(vblank_cadence_bounds_protect_period_division_and_custom_overrides) {
  SR_CHECK_EQ(SelectNativeVblankCadence(0,false,240).hz,240u);
  SR_CHECK_EQ(SelectNativeVblankCadence(0,false,0).hz,1u);
  SR_CHECK_EQ(SelectNativeVblankCadence(0,false,100000).hz,2000u);
  SR_CHECK_EQ(SelectNativeVblankCadence(1,false).hz,20u);
  SR_CHECK_EQ(SelectNativeVblankCadence(100000,false).hz,2000u);
}
