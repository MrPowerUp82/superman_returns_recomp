#include "../../port/src/graphics/guest/replay_options.h"
#include "test_main.h"
using namespace superman_returns::graphics::guest;
SR_TEST(replay_options_distinguish_direct_and_prepared_one) {
  SR_CHECK(!ParseReplayOptions("", "interleaved", true).prepared);
  auto one=ParseReplayOptions("1", "interleaved", true);
  SR_CHECK(one.prepared); SR_CHECK_EQ(one.shards,1u);
  SR_CHECK(one.scheduling==ReplayScheduling::kInterleaved);
  for(unsigned n=1;n<=8;++n) {
    auto value=std::to_string(n);
    auto p=ParseReplayOptions(value,"contiguous",true);
    SR_CHECK(p.prepared);SR_CHECK_EQ(p.shards,n);
    SR_CHECK(!p.invalid_threads);SR_CHECK(!p.invalid_scheduling);
  }
  auto disabled=ParseReplayOptions("8","interleaved",false);
  SR_CHECK(!disabled.prepared);SR_CHECK_EQ(disabled.shards,1u);
}
SR_TEST(replay_options_reject_ambiguous_inputs) {
  for(auto value:{"0","9","01","+2","-1"," 2","2 ","2x","999999999999999999"}) {
    auto p=ParseReplayOptions(value,"contiguous",true);
    SR_CHECK(!p.prepared);SR_CHECK(p.invalid_threads);SR_CHECK_EQ(p.shards,1u);
  }
  auto absent=ParseReplayOptions("","",true);
  SR_CHECK(!absent.invalid_threads);SR_CHECK(!absent.invalid_scheduling);
  auto bad=ParseReplayOptions("4","other",true);
  SR_CHECK(bad.prepared);SR_CHECK(bad.invalid_scheduling);
  SR_CHECK(bad.scheduling==ReplayScheduling::kContiguous);
}
