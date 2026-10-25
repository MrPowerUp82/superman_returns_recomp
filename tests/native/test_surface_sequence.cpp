#include "../../port/src/graphics/surface_sequence.h"
#include "test_main.h"

using namespace superman_returns::graphics;

SR_TEST(surface_sequence_coalesces_draws_with_same_target) {
  SurfaceSequence<4> sequence;
  const SurfaceSignature scene{936, 1280, 720, 12, 0, 1280, 720};
  sequence.ObserveDraw(scene);
  sequence.ObserveDraw(scene);
  SR_CHECK_EQ(sequence.size(), 1);
  SR_CHECK_EQ(sequence.total_draws(), 2);
  SR_CHECK_EQ(sequence[0].draws, 2);
  SR_CHECK(sequence[0].surface == scene);
}

SR_TEST(surface_sequence_preserves_order_and_repeated_target) {
  SurfaceSequence<4> sequence;
  const SurfaceSignature scene{936, 1280, 720, 12, 0, 1280, 720};
  const SurfaceSignature hud{0, 1280, 720, 6, 0, 0, 0};
  sequence.ObserveDraw(scene);
  sequence.ObserveDraw(hud);
  sequence.ObserveDraw(scene);
  SR_CHECK_EQ(sequence.size(), 3);
  SR_CHECK_EQ(sequence[0].draws, 1);
  SR_CHECK_EQ(sequence[1].draws, 1);
  SR_CHECK(sequence[2].surface == scene);
}

SR_TEST(surface_sequence_caps_memory_without_losing_draw_count) {
  SurfaceSequence<2> sequence;
  sequence.ObserveDraw({1, 1280, 720, 1, 0, 0, 0});
  sequence.ObserveDraw({2, 1280, 720, 1, 0, 0, 0});
  sequence.ObserveDraw({3, 1280, 720, 1, 0, 0, 0});
  sequence.ObserveDraw({3, 1280, 720, 1, 0, 0, 0});
  SR_CHECK_EQ(sequence.size(), 2);
  SR_CHECK_EQ(sequence.total_draws(), 4);
  SR_CHECK_EQ(sequence.dropped_segments(), 1);
  sequence.Reset();
  SR_CHECK_EQ(sequence.size(), 0);
  SR_CHECK_EQ(sequence.total_draws(), 0);
  SR_CHECK_EQ(sequence.dropped_segments(), 0);
}

SR_TEST(surface_sequence_samples_shader_and_render_state_at_bounded_draw_offsets) {
  SurfaceSequence<2> sequence;
  const SurfaceSignature scene{936, 1280, 720, 12, 0, 1280, 720};
  const SurfaceSignature hud{0, 1280, 720, 6, 0, 0, 0};
  for (uint32_t i = 0; i < 20; ++i) {
    const SurfaceDrawMetadata draw{0x1000 + i, 0x2000 + i, 3, 4, 0x55 + i, 0xF};
    sequence.ObserveDraw(scene, draw);
  }
  SR_CHECK_EQ(sequence.size(), 1);
  SR_CHECK_EQ(sequence[0].draws, 20);
  SR_CHECK_EQ(sequence[0].sample_count, 6);
  const uint32_t offsets[] = {0, 1, 2, 4, 8, 16};
  for (uint32_t i = 0; i < 6; ++i) {
    const auto& sample = sequence[0].samples[i];
    SR_CHECK_EQ(sample.draw_offset, offsets[i]);
    SR_CHECK_EQ(sample.draw.vs_hash, 0x1000 + offsets[i]);
    SR_CHECK_EQ(sample.draw.ps_hash, 0x2000 + offsets[i]);
    SR_CHECK_EQ(sample.draw.guest_pass, 3);
    SR_CHECK_EQ(sample.draw.primitive, 4);
    SR_CHECK_EQ(sample.draw.depth_control, 0x55 + offsets[i]);
    SR_CHECK_EQ(sample.draw.color_mask, 0xF);
  }
  sequence.ObserveDraw(hud, SurfaceDrawMetadata{0xABC, 0xDEF, 7, 5, 0, 0xF});
  SR_CHECK_EQ(sequence.size(), 2);
  SR_CHECK_EQ(sequence[1].sample_count, 1);
  SR_CHECK_EQ(sequence[1].samples[0].draw_offset, 0);
  SR_CHECK_EQ(sequence[1].samples[0].draw.vs_hash, 0xABC);
  sequence.Reset();
  sequence.ObserveDraw(scene, SurfaceDrawMetadata{0x123, 0x456, 3, 4, 0, 0xF});
  SR_CHECK_EQ(sequence[0].sample_count, 1);
  SR_CHECK_EQ(sequence[0].samples[0].draw.vs_hash, 0x123);
}
