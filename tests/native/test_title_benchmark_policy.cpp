#include "test_main.h"
#include "../../port/src/title_benchmark_policy.h"
#include <cmath>
#include <vector>

using namespace superman_returns::benchmark;

SR_TEST(benchmark_ui_cut_skips_only_draws_after_the_scene_filter) {
  UiCut cut; cut.BeginFrame();
  SR_CHECK(!cut.ShouldSkip(0x8A6DD1D52752FAD6ull)); SR_CHECK(!cut.scene_drawn());
  SR_CHECK(!cut.ShouldSkip(kSceneFilterPs)); SR_CHECK(cut.scene_drawn());
  SR_CHECK(cut.ShouldSkip(0x40746066C58DA8D5ull)); SR_CHECK(cut.ShouldSkip(0));
  cut.BeginFrame(); SR_CHECK(!cut.scene_drawn()); SR_CHECK(!cut.ShouldSkip(0x40746066C58DA8D5ull));
}

SR_TEST(benchmark_clock_warms_up_then_measures_sixty_seconds) {
  Clock c; double t = 10;
  c.OnFrame(t, false); SR_CHECK(c.phase() == Phase::WaitingScene);
  c.OnFrame(t += 0.02, true); SR_CHECK(c.phase() == Phase::Warmup);
  while (t < 10.02 + 5) c.OnFrame(t += 0.02, true);
  SR_CHECK(c.phase() == Phase::Measuring);
  while (c.phase() == Phase::Measuring) c.OnFrame(t += 0.02, true);
  SR_CHECK(c.valid()); SR_CHECK(c.frame_ms().size() >= 2999 && c.frame_ms().size() <= 3001);
}

SR_TEST(benchmark_clock_invalidates_when_the_scene_stops) {
  Clock c; c.OnFrame(1, true); c.OnFrame(7, true); c.OnFrame(7.02, false);
  SR_CHECK(c.phase() == Phase::Done); SR_CHECK(!c.valid());
  SR_CHECK(c.reason() == "a cena 3D foi interrompida (vídeo de demonstração ou carregamento)");
}

SR_TEST(benchmark_clock_gives_up_when_the_title_never_appears) {
  Clock c; c.OnFrame(119, false); SR_CHECK(c.phase() == Phase::WaitingScene);
  c.OnFrame(121, false); SR_CHECK(c.phase() == Phase::Done); SR_CHECK(!c.valid());
  SR_CHECK(c.reason() == "a cena do título não apareceu");
}

SR_TEST(benchmark_stats_use_slowest_frames_for_lows) {
  std::vector<double> ms(1000, 10.0); ms[0] = 100.0; for (int i = 1; i < 10; ++i) ms[i] = 50.0;
  const auto s = ComputeStats(ms);
  SR_CHECK_EQ(s.frames, 1000u);
  SR_CHECK(std::abs(s.seconds - (990 * 0.010 + 9 * 0.050 + 0.100)) < 1e-9);
  SR_CHECK(std::abs(s.avg_fps - 1000 / s.seconds) < 1e-9);
  SR_CHECK(std::abs(s.min_fps - 10.0) < 1e-9);
  SR_CHECK(std::abs(s.low1_fps - 1000.0 / 55.0) < 1e-9);
  SR_CHECK(std::abs(s.low01_fps - 10.0) < 1e-9);
  SR_CHECK(s.p50_ms == 10.0 && s.max_ms == 100.0);
  SR_CHECK(ComputeStats({}).frames == 0);
}
