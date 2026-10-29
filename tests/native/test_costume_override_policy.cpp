#include "test_main.h"
#include "../../port/src/costume_override_policy.h"

using namespace superman_returns::costume;

namespace {
// Frames until the first write after `film` changes, or -1 if none within `limit`.
int FramesUntilWrite(ReapplyState& state, uint32_t film, uint32_t worn, bool track_worn, int limit = 1000) {
  for (int frame = 1; frame <= limit; ++frame)
    if (StepReapply(state, film, worn, track_worn)) return frame;
  return -1;
}
}  // namespace

SR_TEST(costume_reapply_waits_for_the_film_suit) {
  ReapplyState state;
  SR_CHECK_EQ(FramesUntilWrite(state, 0, 0, true), -1);  // nothing decoded yet: never writes
  SR_CHECK_EQ(FramesUntilWrite(state, 1, 0, true), kReapplyDelayFrames);
}

SR_TEST(costume_reapply_stops_once_the_costume_shows_up) {
  ReapplyState state;
  SR_CHECK_EQ(FramesUntilWrite(state, 1, 0, true), kReapplyDelayFrames);
  SR_CHECK_EQ(FramesUntilWrite(state, 1, 1, true), -1);  // the wanted suit was decoded: no retry
  SR_CHECK_EQ(FramesUntilWrite(state, 2, 1, true), kReapplyDelayFrames);  // film suit back: again
}

SR_TEST(costume_reapply_retries_a_bounded_number_of_times) {
  ReapplyState state;
  int writes = 0;
  for (int frame = 0; frame < 10000; ++frame) writes += StepReapply(state, 1, 0, true);
  SR_CHECK_EQ(writes, kReapplyTries);
  ReapplyState untracked;  // no known texture for the wanted suit: one write per film-suit decode
  writes = 0;
  for (int frame = 0; frame < 10000; ++frame) writes += StepReapply(untracked, 1, 0, false);
  SR_CHECK_EQ(writes, 1);
}

SR_TEST(costume_parse_accepts_only_known_values) {
  SR_CHECK(ParseCostume("0") == 0);
  SR_CHECK(ParseCostume("1") == 1);
  SR_CHECK(ParseCostume("2") == 2);
  for (const char* bad : {"default", "", "3", "-1", "abc", "02", " 1"})
    SR_CHECK(!ParseCostume(bad).has_value());
}
