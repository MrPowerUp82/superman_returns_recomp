#include "../graphics/gpu_pass_stats.h"
#include "test_main.h"
#include <string>
using namespace superman_returns::graphics;

namespace {
GpuPassKey Key(uint32_t w, uint32_t h, uint32_t colors, bool depth) { return {w, h, colors, depth}; }
}

SR_TEST(gpu_pass_stats_sums_segments_per_key_across_frames) {
  GpuPassStats stats;
  stats.AddFrame({{Key(1280, 720, 1, true), 4000000}, {Key(256, 144, 0, true), 1000000},
                  {Key(1280, 720, 1, true), 2000000}}, 500000);
  stats.AddFrame({{Key(1280, 720, 1, true), 6000000}}, 1500000);
  SR_CHECK_EQ(stats.frames(), 2u);
  const std::string text = stats.Format(8);
  // 12 ms over 2 frames = 6.00 ms per frame in 3/2 = 1.5 passes per frame
  SR_CHECK(text.find("1280x720 c1 d1: 6.00 ms") != std::string::npos);
  SR_CHECK(text.find("1.5 passes") != std::string::npos);
  SR_CHECK(text.find("256x144 c0 d1: 0.50 ms") != std::string::npos);
  SR_CHECK(text.find("other 1.00 ms") != std::string::npos);  // (0.5 + 1.5) ms over 2 frames
}

SR_TEST(gpu_pass_stats_orders_by_total_time_and_applies_the_limit) {
  GpuPassStats stats;
  stats.AddFrame({{Key(100, 100, 1, false), 1000000}, {Key(200, 200, 1, false), 3000000},
                  {Key(300, 300, 1, false), 2000000}}, 0);
  const std::string two = stats.Format(2);
  SR_CHECK(two.find("200x200") < two.find("300x300"));
  SR_CHECK(two.find("100x100") == std::string::npos);
  SR_CHECK(two.find("more") != std::string::npos);  // says one group was left out
}

SR_TEST(gpu_pass_stats_reset_and_empty_format) {
  GpuPassStats stats;
  SR_CHECK_EQ(stats.frames(), 0u);
  SR_CHECK(stats.Format(4).find("no frames") != std::string::npos);
  stats.AddFrame({{Key(1, 1, 1, false), 1000}}, 0);
  stats.Reset();
  SR_CHECK_EQ(stats.frames(), 0u);
  SR_CHECK(stats.Format(4).find("no frames") != std::string::npos);
}
