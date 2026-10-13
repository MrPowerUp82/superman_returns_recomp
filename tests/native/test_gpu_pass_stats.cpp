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
  SR_CHECK(text.find("1280x720 c1 d1: 6.00 ms (1.5 passes)") != std::string::npos);
  SR_CHECK(text.find("256x144 c0 d1: 0.50 ms") != std::string::npos);
  SR_CHECK(text.find("other 1.00 ms") != std::string::npos);  // (0.5 + 1.5) ms over 2 frames
}

SR_TEST(gpu_pass_stats_orders_by_total_time_and_applies_the_limit) {
  GpuPassStats stats;
  stats.AddFrame({{Key(100, 100, 1, false), 1000000}, {Key(200, 200, 1, false), 3000000},
                  {Key(300, 300, 1, false), 2000000}}, 0);
  const std::string all = stats.Format(8);
  const size_t a200 = all.find("200x200");
  const size_t a300 = all.find("300x300");
  const size_t a100 = all.find("100x100");
  SR_CHECK(a200 != std::string::npos && a300 != std::string::npos && a100 != std::string::npos);
  SR_CHECK(a200 < a300 && a300 < a100);
  SR_CHECK(all.find("more groups") == std::string::npos);

  const std::string two = stats.Format(2);
  const size_t t200 = two.find("200x200");
  const size_t t300 = two.find("300x300");
  SR_CHECK(t200 != std::string::npos && t300 != std::string::npos);
  SR_CHECK(t200 < t300);
  SR_CHECK(two.find("100x100") == std::string::npos);
  SR_CHECK(two.find("1 more groups: 1.00 ms") != std::string::npos);  // the 1 ms group over 1 frame

  const std::string none = stats.Format(0);
  SR_CHECK(none.find("200x200") == std::string::npos);
  SR_CHECK(none.find("300x300") == std::string::npos);
  SR_CHECK(none.find("100x100") == std::string::npos);
  SR_CHECK(none.find("3 more groups: 6.00 ms") != std::string::npos);  // all of them: 1 + 3 + 2 ms
}

SR_TEST(gpu_pass_stats_breaks_equal_totals_by_key_so_the_output_is_deterministic) {
  const GpuPassKey a = Key(100, 200, 1, false), b = Key(100, 200, 1, true), c = Key(100, 300, 0, false),
                   d = Key(640, 100, 1, false);
  // Same totals, inserted in a scrambled order, in two instances: the text must be identical.
  GpuPassStats first, second;
  first.AddFrame({{d, 1000000}, {b, 1000000}, {c, 1000000}, {a, 1000000}}, 0);
  second.AddFrame({{a, 1000000}, {c, 1000000}, {d, 1000000}, {b, 1000000}}, 0);
  const std::string text = first.Format(8);
  SR_CHECK(text == second.Format(8));
  const size_t pa = text.find("100x200 c1 d0");
  const size_t pb = text.find("100x200 c1 d1");
  const size_t pc = text.find("100x300 c0 d0");
  const size_t pd = text.find("640x100 c1 d0");
  SR_CHECK(pa != std::string::npos && pb != std::string::npos && pc != std::string::npos &&
           pd != std::string::npos);
  SR_CHECK(pa < pb && pb < pc && pc < pd);  // width, height, colors, then depth ascending
  // The limit cuts the same tail, too.
  SR_CHECK(first.Format(2).find("100x300") == std::string::npos);
  SR_CHECK(first.Format(2).find("2 more groups: 2.00 ms") != std::string::npos);
}

SR_TEST(gpu_pass_stats_reset_and_empty_format) {
  GpuPassStats stats;
  SR_CHECK_EQ(stats.frames(), 0u);
  SR_CHECK(stats.Format(4).find("no frames") != std::string::npos);
  stats.AddFrame({{Key(1, 1, 1, false), 1000}}, 7000000);
  stats.Reset();
  SR_CHECK_EQ(stats.frames(), 0u);
  SR_CHECK(stats.Format(4).find("no frames") != std::string::npos);
  // A half-done Reset (stale groups or stale other time) would show up after the next frame.
  stats.AddFrame({{Key(2, 2, 2, true), 1000000}}, 0);
  SR_CHECK_EQ(stats.frames(), 1u);
  const std::string text = stats.Format(4);
  SR_CHECK(text.find("2x2 c2 d1: 1.00 ms") != std::string::npos);
  SR_CHECK(text.find("1x1 c1 d0") == std::string::npos);
  SR_CHECK(text.find("other 0.00 ms") != std::string::npos);
}
