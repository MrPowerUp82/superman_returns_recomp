#include "../../port/src/graphics/camera_candidate_probe.h"
#include "test_main.h"

#include <array>
#include <limits>

using superman_returns::graphics::CameraCandidateProbe;

namespace {
constexpr std::array<float, 16> kCamera{
    1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 1};
}

SR_TEST(camera_candidate_ignores_other_shaders_and_rejects_unusable_matrices) {
  CameraCandidateProbe probe;
  probe.Begin(10);
  SR_CHECK(!probe.Observe(0x123, kCamera));
  SR_CHECK_EQ(probe.frame().draws, 0);
  auto nan = kCamera;
  nan[5] = std::numeric_limits<float>::quiet_NaN();
  SR_CHECK(!probe.Observe(CameraCandidateProbe::kViewProjectionShader, nan));
  auto singular = kCamera;
  singular[15] = 0;
  SR_CHECK(!probe.Observe(CameraCandidateProbe::kViewProjectionShader, singular));
  SR_CHECK_EQ(probe.frame().draws, 2);
  SR_CHECK_EQ(probe.frame().valid, 0);
  SR_CHECK_EQ(probe.frame().rejected, 2);
  SR_CHECK(probe.Observe(CameraCandidateProbe::kViewProjectionShader, kCamera));
  SR_CHECK_EQ(probe.frame().valid, 1);
  SR_CHECK(probe.frame().first == kCamera);
}

SR_TEST(camera_candidate_measures_cross_draw_changes_and_consecutive_frames) {
  CameraCandidateProbe probe;
  probe.Begin(20);
  SR_CHECK(probe.Observe(CameraCandidateProbe::kViewProjectionShader, kCamera));
  SR_CHECK(probe.Observe(CameraCandidateProbe::kViewProjectionShader, kCamera));
  SR_CHECK_EQ(probe.frame().varying_draws, 0);
  auto moved = kCamera;
  moved[12] += 0.25f;
  SR_CHECK(probe.Observe(CameraCandidateProbe::kViewProjectionShader, moved));
  SR_CHECK_EQ(probe.frame().varying_draws, 1);
  SR_CHECK(probe.frame().max_within_frame_delta >= 0.25f);
  probe.End();
  probe.Begin(21);
  SR_CHECK(probe.Observe(CameraCandidateProbe::kViewProjectionShader, moved));
  SR_CHECK(probe.frame().has_previous);
  SR_CHECK(probe.frame().max_previous_frame_delta >= 0.25f);
  probe.End();
  probe.Begin(23);
  SR_CHECK(probe.Observe(CameraCandidateProbe::kViewProjectionShader, moved));
  SR_CHECK(!probe.frame().has_previous);
}

SR_TEST(camera_candidate_rejects_tiny_or_nonfinite_determinants) {
  CameraCandidateProbe probe;
  probe.Begin(1);
  auto degenerate = kCamera;
  degenerate[10] = 1e-25f;
  SR_CHECK(!probe.Observe(CameraCandidateProbe::kViewProjectionShader, degenerate));
  SR_CHECK(probe.Observe(CameraCandidateProbe::kViewProjectionShader, kCamera));
  SR_CHECK_EQ(probe.frame().rejected, 1);
}
