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

SR_TEST(benchmark_clock_invalidates_when_a_video_starts) {
  // The attract video plays over the still-rendered scene: the scene filter
  // keeps being drawn, so the video start itself must end the run.
  Clock c; c.OnFrame(1, true); c.OnFrame(7, true);
  c.Interrupt();
  SR_CHECK(c.phase() == Phase::Done); SR_CHECK(!c.valid());
  SR_CHECK(c.reason() == "a cena 3D foi interrompida (vídeo de demonstração ou carregamento)");
  Clock waiting; waiting.Interrupt();  // before the scene (boot videos): ignored
  SR_CHECK(waiting.phase() == Phase::WaitingScene);
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

#include "../../port/src/title_benchmark_format.h"
#include <filesystem>
#include <fstream>
#include <sstream>

SR_TEST(benchmark_result_json_contains_metrics_and_reason) {
  Result r;
  r.phase = Phase::Done; r.valid = false;
  r.reason = "a cena 3D foi interrompida (vídeo de demonstração ou carregamento)";
  r.stats.avg_fps = 41.25; r.stats.frames = 2475;
  r.settings_text = "API d3d12 \"teste\" C:\\jogo";
  const auto json = FormatResultJson(r, "2026-10-28T12:00:00");
  SR_CHECK(json.find("\"valid\": false") != std::string::npos);
  SR_CHECK(json.find(r.reason) != std::string::npos);
  SR_CHECK(json.find("\"avg_fps\": 41.25") != std::string::npos);
  SR_CHECK(json.find("\"frames\": 2475") != std::string::npos);
  SR_CHECK(json.find("\"timestamp\": \"2026-10-28T12:00:00\"") != std::string::npos);
  SR_CHECK(json.find("API d3d12 \\\"teste\\\" C:\\\\jogo") != std::string::npos);  // escaped
}

SR_TEST(benchmark_result_saves_json_and_frame_csv) {
  const auto dir = std::filesystem::temp_directory_path() / "sr_benchmark_test_save";
  std::filesystem::remove_all(dir);
  Result r; r.phase = Phase::Done; r.valid = true;
  const std::vector<double> frames{16.5, 33.25};
  SaveResult(r, dir, "20261028-120000", frames);
  SR_CHECK(r.saved);
  SR_CHECK(r.json_path == (dir / "benchmark-20261028-120000.json").string());
  std::stringstream text;
  {
    std::ifstream csv(dir / "benchmark-20261028-120000.csv");
    text << csv.rdbuf();
  }  // Windows cannot delete the directory while the file is open.
  SR_CHECK(text.str() == "frame_ms\n16.5\n33.25\n");
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(benchmark_result_reports_unsaved_file) {
  // A regular file where the directory should be: creation fails.
  const auto blocker = std::filesystem::temp_directory_path() / "sr_benchmark_test_blocker";
  std::filesystem::remove_all(blocker);
  std::ofstream(blocker) << "x";
  Result r; r.phase = Phase::Done; r.valid = true;
  SaveResult(r, blocker / "benchmarks", "20261028-120000", {});
  SR_CHECK(!r.saved); SR_CHECK(r.json_path.empty());
  std::filesystem::remove(blocker);
}

SR_TEST(benchmark_result_saves_under_a_unicode_folder) {
  // A name outside the ANSI code page must not throw from path::string().
  const auto dir = std::filesystem::temp_directory_path() / std::filesystem::path(u8"sr_bench_Łukasz_日本") / "benchmarks";
  std::error_code ignored;
  std::filesystem::remove_all(dir.parent_path(), ignored);
  Result r; r.phase = Phase::Done; r.valid = true;
  const std::vector<double> frames{16.5};
  SaveResult(r, dir, "20261028-120000", frames);
  SR_CHECK(r.saved);
  SR_CHECK(r.json_path.find(reinterpret_cast<const char*>(u8"Łukasz")) != std::string::npos);  // UTF-8 for ImGui and the log
  SR_CHECK(std::filesystem::exists(dir / "benchmark-20261028-120000.csv"));
  std::filesystem::remove_all(dir.parent_path(), ignored);
}

SR_TEST(benchmark_scene_latch_keeps_a_lost_frame_until_read) {
  SceneLatch latch;
  SR_CHECK(!latch.Consume());          // nothing reported yet
  latch.Note(true); SR_CHECK(latch.Consume());
  latch.Note(false); latch.Note(true);  // a loss between two presents
  SR_CHECK(!latch.Consume());
  SR_CHECK(latch.Consume());            // cleared once read; latest frame had the scene
}
