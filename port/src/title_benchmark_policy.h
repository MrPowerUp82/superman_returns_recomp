#pragma once
// Title-screen benchmark rules (docs/superpowers/specs/2026-10-28-title-benchmark-design.md).
// Pure: no SDK, so tests/native exercises them directly.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace superman_returns::benchmark {

// Final spatial filter of the 3D scene. Every draw after it in a title frame is
// UI: logo sprites, PRESS START font and the frame lines.
constexpr uint64_t kSceneFilterPs = 0xB397B05F4CE5D5BBull;

// Per renderer and frame: skips every draw after the scene filter.
class UiCut {
 public:
  void BeginFrame() { scene_drawn_ = false; }
  bool ShouldSkip(uint64_t ps_hash) {
    if (scene_drawn_) return true;
    if (ps_hash == kSceneFilterPs) scene_drawn_ = true;
    return false;
  }
  bool scene_drawn() const { return scene_drawn_; }

 private:
  bool scene_drawn_ = false;
};

enum class Phase { WaitingScene, Warmup, Measuring, Done };

// Fed once per guest present with seconds since boot and whether the renderer
// drew the 3D scene in its latest frame.
class Clock {
 public:
  explicit Clock(double warmup_s = 5, double measure_s = 60, double scene_timeout_s = 120)
      : warmup_s_(warmup_s), measure_s_(measure_s), scene_timeout_s_(scene_timeout_s) {}
  void OnFrame(double t_s, bool scene_drawn) {
    switch (phase_) {
      case Phase::WaitingScene:
        if (scene_drawn) { phase_ = Phase::Warmup; scene_s_ = t_s; }
        else if (t_s > scene_timeout_s_) Finish("a cena do título não apareceu");
        break;
      case Phase::Warmup:
        if (!scene_drawn) { Finish(kInterrupted); break; }
        if (t_s - scene_s_ >= warmup_s_) { phase_ = Phase::Measuring; measure_s0_ = last_s_ = t_s; }
        break;
      case Phase::Measuring:
        if (!scene_drawn) { Finish(kInterrupted); break; }
        frame_ms_.push_back((t_s - last_s_) * 1000.0);
        last_s_ = t_s;
        if (t_s - measure_s0_ >= measure_s_) { phase_ = Phase::Done; valid_ = true; }
        break;
      case Phase::Done:
        break;
    }
  }
  Phase phase() const { return phase_; }
  bool valid() const { return valid_; }
  const std::string& reason() const { return reason_; }
  std::span<const double> frame_ms() const { return frame_ms_; }

 private:
  static constexpr const char* kInterrupted =
      "a cena 3D foi interrompida (vídeo de demonstração ou carregamento)";
  void Finish(const char* reason) { phase_ = Phase::Done; valid_ = false; reason_ = reason; }
  double warmup_s_, measure_s_, scene_timeout_s_;
  double scene_s_ = 0, measure_s0_ = 0, last_s_ = 0;
  Phase phase_ = Phase::WaitingScene;
  bool valid_ = false;
  std::string reason_;
  std::vector<double> frame_ms_;
};

struct Stats {
  double avg_fps = 0, min_fps = 0, low1_fps = 0, low01_fps = 0;
  double p50_ms = 0, p95_ms = 0, p99_ms = 0, max_ms = 0, seconds = 0;
  uint32_t frames = 0;
};

// Lows: FPS of the mean of the slowest ceil(1%) / ceil(0.1%) frames.
// Percentiles: nearest rank.
inline Stats ComputeStats(std::span<const double> frame_ms) {
  Stats s;
  if (frame_ms.empty()) return s;
  std::vector<double> sorted(frame_ms.begin(), frame_ms.end());
  std::sort(sorted.begin(), sorted.end());
  const size_t n = sorted.size();
  double total_ms = 0;
  for (double ms : sorted) total_ms += ms;
  const auto slowest_fps = [&](double fraction) {
    const size_t count = std::max<size_t>(1, size_t(std::ceil(double(n) * fraction)));
    double sum = 0;
    for (size_t i = n - count; i < n; ++i) sum += sorted[i];
    return 1000.0 / (sum / double(count));
  };
  const auto percentile = [&](double p) {
    const size_t rank = std::max<size_t>(1, size_t(std::ceil(p * double(n))));
    return sorted[rank - 1];
  };
  s.frames = uint32_t(n);
  s.seconds = total_ms / 1000.0;
  s.avg_fps = s.seconds > 0 ? double(n) / s.seconds : 0;
  s.max_ms = sorted.back();
  s.min_fps = s.max_ms > 0 ? 1000.0 / s.max_ms : 0;
  s.low1_fps = slowest_fps(0.01);
  s.low01_fps = slowest_fps(0.001);
  s.p50_ms = percentile(0.50);
  s.p95_ms = percentile(0.95);
  s.p99_ms = percentile(0.99);
  return s;
}

}  // namespace superman_returns::benchmark
