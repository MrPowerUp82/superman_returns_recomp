#include "test_main.h"
#include "../../port/src/graphics/guest/texture_binding_policy.h"
#include <memory>

using namespace superman_returns::graphics::guest;

SR_TEST(texture_mask_unions_shader_stages_and_distinguishes_empty_from_unknown) {
  ShaderCapture vs, ps;
  vs.vertex = true;
  vs.texture_usage = {1u << 2, true};
  ps.texture_usage = {1u << 31, true};
  SR_CHECK_EQ(TextureMaskForShaders(&vs, &ps, true), (1u << 2) | (1u << 31));
  SR_CHECK_EQ(TextureMaskForShaders(&ps, &vs, true), (1u << 2) | (1u << 31));
  vs.texture_usage = {0, true};
  ps.texture_usage = {0, true};
  SR_CHECK_EQ(TextureMaskForShaders(&vs, &ps, true), 0u);
  ps.texture_usage.known = false;
  SR_CHECK_EQ(TextureMaskForShaders(&vs, &ps, true), UINT32_MAX);
}

SR_TEST(texture_mask_preserves_unknown_bound_slots_and_depth_only_draws) {
  ShaderCapture vs, ps, another_vs;
  vs.vertex = another_vs.vertex = true;
  vs.texture_usage = {8, true};
  ps.texture_usage = {16, true};
  SR_CHECK_EQ(TextureMaskForShaders(&vs, nullptr, true), 8u); // absent PS
  SR_CHECK_EQ(TextureMaskForShaders(&vs, nullptr, false), UINT32_MAX); // unknown PS
  SR_CHECK_EQ(TextureMaskForShaders(nullptr, &ps, true), UINT32_MAX); // missing VS
  SR_CHECK_EQ(TextureMaskForShaders(&vs, &another_vs, true), UINT32_MAX);
  SR_CHECK_EQ(TextureMaskForShaders(&vs, &ps, true, false), UINT32_MAX);
}

SR_TEST(texture_mask_snapshot_retains_its_usage_after_shader_object_replacement) {
  auto original = std::make_shared<ShaderCapture>();
  original->vertex = true;
  original->texture_usage = {2, true};
  std::shared_ptr<const ShaderCapture> pending = original;
  original = std::make_shared<ShaderCapture>();
  original->vertex = true;
  original->texture_usage = {4, true};
  SR_CHECK_EQ(TextureMaskForShaders(pending.get(), nullptr, true), 2u);
  SR_CHECK_EQ(TextureMaskForShaders(original.get(), nullptr, true), 4u);
}
