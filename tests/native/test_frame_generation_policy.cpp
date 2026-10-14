#include "../../port/src/graphics/vulkan/frame_generation_policy.h"
#include "test_main.h"
using namespace superman_returns::graphics::vulkan;
SR_TEST(fg_acquire_timeout_does_not_consume_the_unpresented_source) {
  FrameGenerationPolicy p;p.OnSource(1,0);
  SR_CHECK(p.PreviewSource(2,66000000)==GenerationAction::kInterpolate);
  SR_CHECK(p.PreviewSource(2,70000000)==GenerationAction::kInterpolate);
  SR_CHECK_EQ(p.real(),0u);
  p.OnSource(2,70000000);p.OnPresented(FramePresentationKind::kGenerated);
  // The pending original remains real even if its first acquire suspends.
  SR_CHECK(p.PreviewSource(2,80000000)==GenerationAction::kRepeat);
  SR_CHECK_EQ(p.real(),0u);p.OnPresented(FramePresentationKind::kReal);
  SR_CHECK_EQ(p.real(),1u);SR_CHECK_EQ(p.generated(),1u);
}
SR_TEST(fg_only_advances_history_for_new_real_frames) {
  FrameGenerationPolicy p;
  SR_CHECK(p.OnSource(1,0)==GenerationAction::kWarmup);
  SR_CHECK(p.OnSource(2,66000000)==GenerationAction::kInterpolate);
  SR_CHECK_EQ(p.HalfIntervalNs(),33000000u);
  SR_CHECK(p.OnSource(2,70000000)==GenerationAction::kRepeat);
  SR_CHECK(p.OnSource(1,80000000)==GenerationAction::kStale);
  SR_CHECK(p.OnSource(3,132000000)==GenerationAction::kInterpolate);
  SR_CHECK_EQ(p.HalfIntervalNs(),33000000u);
}
SR_TEST(fg_resets_for_pause_clock_regression_and_reconnection) {
  FrameGenerationPolicy p;p.OnSource(1,100000000);
  SR_CHECK(p.OnSource(2,500000000)==GenerationAction::kWarmup);
  SR_CHECK(p.OnSource(3,1)==GenerationAction::kWarmup);
  p.Reset();SR_CHECK(p.OnSource(1,0)==GenerationAction::kWarmup);
  SR_CHECK(p.OnSource(2,1)==GenerationAction::kInterpolate);
  SR_CHECK_EQ(p.HalfIntervalNs(),1000000u);
  p.OnSource(3,200000001);SR_CHECK_EQ(p.HalfIntervalNs(),50000000u);
}
SR_TEST(fg_counts_only_successfully_presented_frames) {
  FrameGenerationPolicy p;p.OnSource(1,0);p.OnSource(2,66000000);
  SR_CHECK_EQ(p.presented(),0u);
  p.OnPresented(FramePresentationKind::kReal);
  p.OnPresented(FramePresentationKind::kGenerated);
  p.OnPresented(FramePresentationKind::kRepeat);
  SR_CHECK_EQ(p.real(),1u);SR_CHECK_EQ(p.generated(),1u);
  SR_CHECK_EQ(p.repeated(),1u);SR_CHECK_EQ(p.presented(),3u);
  p.Reset();SR_CHECK_EQ(p.presented(),3u);
}
