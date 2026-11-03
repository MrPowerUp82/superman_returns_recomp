#include "../../port/src/coop_split_frame.h"
#include "test_main.h"
using namespace superman_returns::coop;
SR_TEST(coop_split_requires_two_ordered_views_from_the_same_frame_and_binding) {
  SplitFrame pair;
  SR_CHECK(!pair.Ready());
  SR_CHECK(pair.Marker(0,7,0xC0000001));
  SR_CHECK(pair.DeferSwap()); // The guest presents inside the effect draw.
  SR_CHECK(pair.Marker(1,7,0xC0000001));
  SR_CHECK(!pair.Ready());
  SR_CHECK(pair.Marker(2,7,0xC0000001));
  SR_CHECK(pair.DeferSwap());
  SR_CHECK(pair.Marker(3,7,0xC0000001));
  SR_CHECK(pair.Ready());
  SR_CHECK(!pair.DeferSwap());
  SR_CHECK(!pair.Marker(3,7,0xC0000001));
  SR_CHECK(!pair.Ready());
  SR_CHECK(pair.Marker(0,8,0xC0000002));
  SR_CHECK(!pair.Marker(1,7,0xC0000001));
  SR_CHECK(!pair.Ready());
  SR_CHECK(pair.Marker(0,9,0xC0000002));
  SR_CHECK(pair.Marker(1,9,0xC0000002));
  SR_CHECK(!pair.Marker(2,9,0xC0000001));
  SR_CHECK(!pair.Ready());
}
SR_TEST(coop_split_reset_and_invalid_starts_never_reuse_previous_images) {
  SplitFrame pair;
  SR_CHECK(!pair.Marker(0,0,0xC0000001));
  SR_CHECK(!pair.Marker(0,1,0));
  for(unsigned action=0;action<4;++action) SR_CHECK(pair.Marker(action,10,0xC0000001));
  SR_CHECK(pair.Ready());
  pair.Reset();
  SR_CHECK(!pair.Ready());
  SR_CHECK(!pair.Marker(1,10,0xC0000001));
  SR_CHECK(!pair.Ready());
}
SR_TEST(coop_second_draw_repeats_effect_phase_and_restores_first_draw_advancement) {
  ReplayPhase before;before.effects[0]={0x1000,10,3};before.frame_time=100;
  auto live=before;
  ++live.effects[0].clock;++live.effects[0].cadence;live.frame_time=200;
  const auto after_first=live;
  bool drew=false;
  SR_CHECK(ReplayAtOriginalPhase(before,[&]{return std::optional{live};},
      [&](const ReplayPhase& p){live=p;return true;},[&]{
        SR_CHECK(live==before);
        ++live.effects[0].clock;++live.effects[0].cadence;live.frame_time=0;drew=true;
      }));
  SR_CHECK(drew);SR_CHECK(live==after_first);
  drew=false;
  SR_CHECK(!ReplayAtOriginalPhase(before,[&]{return std::optional<ReplayPhase>{};},
      [&](const ReplayPhase&){return true;},[&]{drew=true;}));
  SR_CHECK(!drew);
}
SR_TEST(coop_capture_uses_resolved_image_instead_of_small_or_cleared_target) {
  SR_CHECK(ChooseSplitSource(true,false,true)==SplitSource::Bound);
  SR_CHECK(ChooseSplitSource(false,false,true)==SplitSource::Resolved);
  SR_CHECK(ChooseSplitSource(true,true,true)==SplitSource::Resolved);
  SR_CHECK(ChooseSplitSource(true,true,false)==SplitSource::None);
  SR_CHECK(ChooseSplitSource(false,false,false)==SplitSource::None);
}
