#include "test_main.h"
#include "../../port/src/graphics/scene_resolution_mapping.h"

#include <cmath>
#include <limits>

using namespace superman_returns::graphics;

SR_TEST(valid_scene_surface_mapping_bounds) {
  // Valid bounds are 1..16384 per axis for both guest and host.
  SceneSurfaceMapping valid{
      .guest = {.width = 1280, .height = 720},
      .host = {.width = 853, .height = 480},
  };
  SR_CHECK(ValidSceneSurfaceMapping(valid));

  // Extreme valid bounds (1 and 16384)
  SceneSurfaceMapping min_valid{
      .guest = {.width = 1, .height = 1},
      .host = {.width = 1, .height = 1},
  };
  SR_CHECK(ValidSceneSurfaceMapping(min_valid));

  SceneSurfaceMapping max_valid{
      .guest = {.width = 16384, .height = 16384},
      .host = {.width = 16384, .height = 16384},
  };
  SR_CHECK(ValidSceneSurfaceMapping(max_valid));

  // Zero is invalid on any axis
  SceneSurfaceMapping zero_guest_w = valid;
  zero_guest_w.guest.width = 0;
  SR_CHECK(!ValidSceneSurfaceMapping(zero_guest_w));

  SceneSurfaceMapping zero_guest_h = valid;
  zero_guest_h.guest.height = 0;
  SR_CHECK(!ValidSceneSurfaceMapping(zero_guest_h));

  SceneSurfaceMapping zero_host_w = valid;
  zero_host_w.host.width = 0;
  SR_CHECK(!ValidSceneSurfaceMapping(zero_host_w));

  SceneSurfaceMapping zero_host_h = valid;
  zero_host_h.host.height = 0;
  SR_CHECK(!ValidSceneSurfaceMapping(zero_host_h));

  // Greater than 16384 is invalid on any axis
  SceneSurfaceMapping overflow_guest_w = valid;
  overflow_guest_w.guest.width = 16385;
  SR_CHECK(!ValidSceneSurfaceMapping(overflow_guest_w));

  SceneSurfaceMapping overflow_host_h = valid;
  overflow_host_h.host.height = 20000;
  SR_CHECK(!ValidSceneSurfaceMapping(overflow_host_h));
}

SR_TEST(map_scene_pixel_edge_nearest_floor_ceil) {
  // 1280 -> 853 mapping tests
  // 0 -> 0
  auto edge0_nearest = MapScenePixelEdge(0, 1280, 853, SceneEdgeRounding::Nearest);
  SR_CHECK(edge0_nearest.has_value());
  SR_CHECK_EQ(*edge0_nearest, 0);

  auto edge0_floor = MapScenePixelEdge(0, 1280, 853, SceneEdgeRounding::Floor);
  SR_CHECK(edge0_floor.has_value());
  SR_CHECK_EQ(*edge0_floor, 0);

  auto edge0_ceil = MapScenePixelEdge(0, 1280, 853, SceneEdgeRounding::Ceil);
  SR_CHECK(edge0_ceil.has_value());
  SR_CHECK_EQ(*edge0_ceil, 0);

  // 1280 -> 853
  auto edge_full_nearest = MapScenePixelEdge(1280, 1280, 853, SceneEdgeRounding::Nearest);
  SR_CHECK(edge_full_nearest.has_value());
  SR_CHECK_EQ(*edge_full_nearest, 853);

  // Midpoint 640 -> 640 * 853 / 1280 = 426.5
  // Nearest rounds half away from zero -> 427
  auto mid_nearest = MapScenePixelEdge(640, 1280, 853, SceneEdgeRounding::Nearest);
  SR_CHECK(mid_nearest.has_value());
  SR_CHECK_EQ(*mid_nearest, 427);

  // Floor of 426.5 -> 426
  auto mid_floor = MapScenePixelEdge(640, 1280, 853, SceneEdgeRounding::Floor);
  SR_CHECK(mid_floor.has_value());
  SR_CHECK_EQ(*mid_floor, 426);

  // Ceil of 426.5 -> 427
  auto mid_ceil = MapScenePixelEdge(640, 1280, 853, SceneEdgeRounding::Ceil);
  SR_CHECK(mid_ceil.has_value());
  SR_CHECK_EQ(*mid_ceil, 427);

  // Invalid extents must return nullopt
  SR_CHECK(!MapScenePixelEdge(100, 0, 853).has_value());
  SR_CHECK(!MapScenePixelEdge(100, 1280, 0).has_value());
  SR_CHECK(!MapScenePixelEdge(100, 16385, 853).has_value());
  SR_CHECK(!MapScenePixelEdge(100, 1280, 16385).has_value());
}

SR_TEST(map_scene_pixel_edge_signed_tie_breaking) {
  // Test scale = 2.5 (guest = 2, host = 5)
  // Halfway cases: edge = 1 -> 2.5; edge = -1 -> -2.5
  // Nearest with tie breaking away from zero:
  // +2.5 -> +3
  // -2.5 -> -3 (matching std::lround(-2.5) == -3)
  auto pos_half = MapScenePixelEdge(1, 2, 5, SceneEdgeRounding::Nearest);
  SR_CHECK(pos_half.has_value());
  SR_CHECK_EQ(*pos_half, 3);

  auto neg_half = MapScenePixelEdge(-1, 2, 5, SceneEdgeRounding::Nearest);
  SR_CHECK(neg_half.has_value());
  SR_CHECK_EQ(*neg_half, -3);

  // Floor of -2.5 is -3; Floor of +2.5 is +2
  auto pos_floor = MapScenePixelEdge(1, 2, 5, SceneEdgeRounding::Floor);
  SR_CHECK(pos_floor.has_value());
  SR_CHECK_EQ(*pos_floor, 2);

  auto neg_floor = MapScenePixelEdge(-1, 2, 5, SceneEdgeRounding::Floor);
  SR_CHECK(neg_floor.has_value());
  SR_CHECK_EQ(*neg_floor, -3);

  // Ceil of -2.5 is -2; Ceil of +2.5 is +3
  auto pos_ceil = MapScenePixelEdge(1, 2, 5, SceneEdgeRounding::Ceil);
  SR_CHECK(pos_ceil.has_value());
  SR_CHECK_EQ(*pos_ceil, 3);

  auto neg_ceil = MapScenePixelEdge(-1, 2, 5, SceneEdgeRounding::Ceil);
  SR_CHECK(neg_ceil.has_value());
  SR_CHECK_EQ(*neg_ceil, -2);

  // Integer multiples (no remainder)
  // +2 * 5 / 2 = 5; -2 * 5 / 2 = -5
  SR_CHECK_EQ(*MapScenePixelEdge(2, 2, 5, SceneEdgeRounding::Nearest), 5);
  SR_CHECK_EQ(*MapScenePixelEdge(2, 2, 5, SceneEdgeRounding::Floor), 5);
  SR_CHECK_EQ(*MapScenePixelEdge(2, 2, 5, SceneEdgeRounding::Ceil), 5);

  SR_CHECK_EQ(*MapScenePixelEdge(-2, 2, 5, SceneEdgeRounding::Nearest), -5);
  SR_CHECK_EQ(*MapScenePixelEdge(-2, 2, 5, SceneEdgeRounding::Floor), -5);
  SR_CHECK_EQ(*MapScenePixelEdge(-2, 2, 5, SceneEdgeRounding::Ceil), -5);

  // Fractional < 0.5 (e.g. 1/4 = 0.25; -1/4 = -0.25)
  SR_CHECK_EQ(*MapScenePixelEdge(1, 4, 1, SceneEdgeRounding::Nearest), 0);
  SR_CHECK_EQ(*MapScenePixelEdge(-1, 4, 1, SceneEdgeRounding::Nearest), 0);
  SR_CHECK_EQ(*MapScenePixelEdge(1, 4, 1, SceneEdgeRounding::Floor), 0);
  SR_CHECK_EQ(*MapScenePixelEdge(-1, 4, 1, SceneEdgeRounding::Floor), -1);
  SR_CHECK_EQ(*MapScenePixelEdge(1, 4, 1, SceneEdgeRounding::Ceil), 1);
  SR_CHECK_EQ(*MapScenePixelEdge(-1, 4, 1, SceneEdgeRounding::Ceil), 0);
}

SR_TEST(map_scene_pixel_edge_overflow_rejection) {
  // Coordinates can be outside target bounds, but must fit in int32_t.
  // INT32_MAX scaled up by 2/1 overflows int32_t
  auto overflow_pos = MapScenePixelEdge(std::numeric_limits<int32_t>::max(), 1, 2);
  SR_CHECK(!overflow_pos.has_value());

  auto overflow_neg = MapScenePixelEdge(std::numeric_limits<int32_t>::min(), 1, 2);
  SR_CHECK(!overflow_neg.has_value());

  // INT32_MAX scaled down by 1/2 fits in int32_t
  auto valid_down = MapScenePixelEdge(std::numeric_limits<int32_t>::max(), 2, 1);
  SR_CHECK(valid_down.has_value());
  // 2147483647 / 2 = 1073741823.5 -> nearest is 1073741824
  SR_CHECK_EQ(*valid_down, 1073741824);

  // INT32_MIN with 1:1 fits in int32_t exactly
  auto valid_min = MapScenePixelEdge(std::numeric_limits<int32_t>::min(), 1, 1);
  SR_CHECK(valid_min.has_value());
  SR_CHECK_EQ(*valid_min, std::numeric_limits<int32_t>::min());
}

SR_TEST(map_scene_pixel_rect_valid_and_empty_inverted_rejection) {
  SceneSurfaceMapping mapping{
      .guest = {.width = 1280, .height = 720},
      .host = {.width = 853, .height = 480},
  };

  // Valid full rect
  auto full = MapScenePixelRect(mapping, {.left = 0, .top = 0, .right = 1280, .bottom = 720});
  SR_CHECK(full.has_value());
  SR_CHECK_EQ(full->left, 0);
  SR_CHECK_EQ(full->top, 0);
  SR_CHECK_EQ(full->right, 853);
  SR_CHECK_EQ(full->bottom, 480);

  // Rejects empty rects (left == right or top == bottom)
  SR_CHECK(!MapScenePixelRect(mapping, {.left = 10, .top = 10, .right = 10, .bottom = 20}).has_value());
  SR_CHECK(!MapScenePixelRect(mapping, {.left = 10, .top = 10, .right = 20, .bottom = 10}).has_value());

  // Rejects inverted rects (left > right or top > bottom)
  SR_CHECK(!MapScenePixelRect(mapping, {.left = 50, .top = 10, .right = 20, .bottom = 100}).has_value());
  SR_CHECK(!MapScenePixelRect(mapping, {.left = 10, .top = 100, .right = 50, .bottom = 20}).has_value());

  // Does NOT clamp: rect outside [0..extent] is mapped if not inverted
  auto outside = MapScenePixelRect(mapping, {.left = -100, .top = -50, .right = 1400, .bottom = 800});
  SR_CHECK(outside.has_value());
  SR_CHECK(outside->left < 0);
  SR_CHECK(outside->top < 0);
  SR_CHECK(outside->right > 853);
  SR_CHECK(outside->bottom > 480);

  // Rect mapping that collapses to empty after rounding is rejected
  // Guest rect: left = 0, right = 1 on guest_extent 10000 -> host_extent 1
  SceneSurfaceMapping huge_downscale{
      .guest = {.width = 10000, .height = 10000},
      .host = {.width = 1, .height = 1},
  };
  // 0*1/10000 = 0, 1*1/10000 = 0.0001 -> nearest 0 -> mapped left == right == 0 -> empty!
  auto collapsed = MapScenePixelRect(huge_downscale, {.left = 0, .top = 0, .right = 1, .bottom = 1});
  SR_CHECK(!collapsed.has_value());
}

SR_TEST(map_scene_pixel_viewport_dlss_quality_exact_full_and_sub) {
  SceneSurfaceMapping mapping{
      .guest = {.width = 1280, .height = 720},
      .host = {.width = 853, .height = 480},
  };

  // Full viewport 0..guest MUST return exact host size
  ScenePixelViewport full_guest{
      .x = 0.0f,
      .y = 0.0f,
      .width = 1280.0f,
      .height = 720.0f,
      .min_depth = 0.0f,
      .max_depth = 1.0f,
  };
  auto full_host = MapScenePixelViewport(mapping, full_guest);
  SR_CHECK(full_host.has_value());
  SR_CHECK_EQ(full_host->x, 0.0f);
  SR_CHECK_EQ(full_host->y, 0.0f);
  SR_CHECK_EQ(full_host->width, 853.0f);
  SR_CHECK_EQ(full_host->height, 480.0f);
  SR_CHECK_EQ(full_host->min_depth, 0.0f);
  SR_CHECK_EQ(full_host->max_depth, 1.0f);

  // Sub-viewport with depths
  ScenePixelViewport sub_guest{
      .x = 100.0f,
      .y = 50.0f,
      .width = 640.0f,
      .height = 360.0f,
      .min_depth = 0.125f,
      .max_depth = 0.875f,
  };
  auto sub_host = MapScenePixelViewport(mapping, sub_guest);
  SR_CHECK(sub_host.has_value());
  float expected_w = static_cast<float>(640.0 * 853.0 / 1280.0); // 426.5
  float expected_h = static_cast<float>(360.0 * 480.0 / 720.0); // 240.0
  SR_CHECK(std::abs(sub_host->width - expected_w) < 1e-4f);
  SR_CHECK(std::abs(sub_host->height - expected_h) < 1e-4f);
  SR_CHECK_EQ(sub_host->min_depth, 0.125f);
  SR_CHECK_EQ(sub_host->max_depth, 0.875f);
}

SR_TEST(map_scene_pixel_viewport_nan_inf_zero_reversed_rejection) {
  SceneSurfaceMapping mapping{
      .guest = {.width = 1280, .height = 720},
      .host = {.width = 853, .height = 480},
  };

  ScenePixelViewport base{
      .x = 0.0f,
      .y = 0.0f,
      .width = 1280.0f,
      .height = 720.0f,
      .min_depth = 0.0f,
      .max_depth = 1.0f,
  };

  // NaN in any parameter
  auto nan_x = base; nan_x.x = std::numeric_limits<float>::quiet_NaN();
  SR_CHECK(!MapScenePixelViewport(mapping, nan_x).has_value());

  auto nan_w = base; nan_w.width = std::numeric_limits<float>::quiet_NaN();
  SR_CHECK(!MapScenePixelViewport(mapping, nan_w).has_value());

  auto nan_d = base; nan_d.min_depth = std::numeric_limits<float>::quiet_NaN();
  SR_CHECK(!MapScenePixelViewport(mapping, nan_d).has_value());

  // Infinity in any parameter
  auto inf_y = base; inf_y.y = std::numeric_limits<float>::infinity();
  SR_CHECK(!MapScenePixelViewport(mapping, inf_y).has_value());

  auto inf_h = base; inf_h.height = -std::numeric_limits<float>::infinity();
  SR_CHECK(!MapScenePixelViewport(mapping, inf_h).has_value());

  // Non-positive width or height
  auto zero_w = base; zero_w.width = 0.0f;
  SR_CHECK(!MapScenePixelViewport(mapping, zero_w).has_value());

  auto neg_h = base; neg_h.height = -10.0f;
  SR_CHECK(!MapScenePixelViewport(mapping, neg_h).has_value());

  // Reversed depth or out of [0, 1] bounds
  auto rev_d = base; rev_d.min_depth = 0.8f; rev_d.max_depth = 0.2f;
  SR_CHECK(!MapScenePixelViewport(mapping, rev_d).has_value());

  auto neg_d = base; neg_d.min_depth = -0.01f;
  SR_CHECK(!MapScenePixelViewport(mapping, neg_d).has_value());

  auto over_d = base; over_d.max_depth = 1.01f;
  SR_CHECK(!MapScenePixelViewport(mapping, over_d).has_value());
}

SR_TEST(map_scene_pixel_viewport_irregular_1079x607) {
  // Prime/irregular extents
  SceneSurfaceMapping irregular{
      .guest = {.width = 1079, .height = 607},
      .host = {.width = 1920, .height = 1080},
  };
  SR_CHECK(ValidSceneSurfaceMapping(irregular));

  ScenePixelViewport full_guest{
      .x = 0.0f,
      .y = 0.0f,
      .width = 1079.0f,
      .height = 607.0f,
      .min_depth = 0.0f,
      .max_depth = 1.0f,
  };
  auto host_vp = MapScenePixelViewport(irregular, full_guest);
  SR_CHECK(host_vp.has_value());
  SR_CHECK_EQ(host_vp->width, 1920.0f);
  SR_CHECK_EQ(host_vp->height, 1080.0f);
}

SR_TEST(same_scene_pixel_scale_exact_rational_comparison) {
  // Identical mappings
  SceneSurfaceMapping dlss1{
      .guest = {.width = 1280, .height = 720},
      .host = {.width = 853, .height = 480},
  };
  SceneSurfaceMapping dlss2 = dlss1;
  SR_CHECK(SameScenePixelScale(dlss1, dlss2));

  // Equivalent rational scales: 1280/640 == 2560/1280 (1/2) and 720/360 == 1440/720 (1/2)
  SceneSurfaceMapping half1{
      .guest = {.width = 1280, .height = 720},
      .host = {.width = 640, .height = 360},
  };
  SceneSurfaceMapping half2{
      .guest = {.width = 2560, .height = 1440},
      .host = {.width = 1280, .height = 720},
  };
  SR_CHECK(SameScenePixelScale(half1, half2));

  // Different ratios: dlss1 (853/1280, 480/720) vs half1 (640/1280, 360/720)
  SR_CHECK(!SameScenePixelScale(dlss1, half1));

  // One axis matches, other differs
  SceneSurfaceMapping diff_y{
      .guest = {.width = 1280, .height = 720},
      .host = {.width = 640, .height = 400}, // X is 1/2, Y is 400/720 != 360/720
  };
  SR_CHECK(!SameScenePixelScale(half1, diff_y));

  // Invalid mapping returns false
  SceneSurfaceMapping invalid{};
  SR_CHECK(!SameScenePixelScale(dlss1, invalid));
}

SR_TEST(plan_scene_pixel_copy_success_and_matching_footprints) {
  SceneSurfaceMapping mapping{
      .guest = {.width = 1280, .height = 720},
      .host = {.width = 853, .height = 480},
  };

  // Full surface copy 1:1 guest to guest
  auto full_copy = PlanScenePixelCopy(
      mapping, mapping,
      {.left = 0, .top = 0, .right = 1280, .bottom = 720},
      0, 0
  );
  SR_CHECK(full_copy.has_value());
  SR_CHECK_EQ(full_copy->source.left, 0);
  SR_CHECK_EQ(full_copy->source.top, 0);
  SR_CHECK_EQ(full_copy->source.right, 853);
  SR_CHECK_EQ(full_copy->source.bottom, 480);
  SR_CHECK_EQ(full_copy->dest_x, 0);
  SR_CHECK_EQ(full_copy->dest_y, 0);
}

SR_TEST(plan_scene_pixel_copy_rejects_subrect_footprint_mismatch) {
  // With 1280 -> 853, subrect rounding can produce a different footprint width
  // depending on source vs dest offsets!
  SceneSurfaceMapping mapping{
      .guest = {.width = 1280, .height = 720},
      .host = {.width = 853, .height = 480},
  };

  // guest_source: left = 0, right = 2
  // host_src: left = round(0) = 0, right = round(2 * 853 / 1280) = round(1.3328) = 1
  // Source footprint width = 1.
  // When placed at guest_dest_x = 2:
  // guest dest rect is [2, 4)
  // host_dest: left = round(2 * 853 / 1280) = 1
  // host_dest: right = round(4 * 853 / 1280) = round(2.6656) = 3
  // Dest footprint width = 3 - 1 = 2!
  // Footprint width mismatch (1 != 2) -> PlanScenePixelCopy MUST reject!
  auto mismatched = PlanScenePixelCopy(
      mapping, mapping,
      {.left = 0, .top = 0, .right = 2, .bottom = 2},
      2, 2
  );
  SR_CHECK(!mismatched.has_value());

  // Placed at guest_dest_x = 0, guest_dest_y = 0, both footprints match (width = 1, height = 1)
  auto matched = PlanScenePixelCopy(
      mapping, mapping,
      {.left = 0, .top = 0, .right = 2, .bottom = 2},
      0, 0
  );
  SR_CHECK(matched.has_value());
  SR_CHECK_EQ(matched->source.right - matched->source.left, 1);
  SR_CHECK_EQ(matched->source.bottom - matched->source.top, 1);
}

SR_TEST(plan_scene_pixel_copy_rejects_differing_scales_853x480_to_1280x720) {
  // Attempting to copy between surfaces with different scales (e.g. 853x480 -> 1280x720)
  // must be strictly rejected (NO upscale invented).
  SceneSurfaceMapping src{
      .guest = {.width = 1280, .height = 720},
      .host = {.width = 853, .height = 480},
  };
  SceneSurfaceMapping dst{
      .guest = {.width = 1280, .height = 720},
      .host = {.width = 1280, .height = 720},
  };

  auto copy = PlanScenePixelCopy(
      src, dst,
      {.left = 0, .top = 0, .right = 640, .bottom = 360},
      0, 0
  );
  SR_CHECK(!copy.has_value());
}

SR_TEST(plan_scene_pixel_copy_rejects_out_of_bounds_and_empty) {
  SceneSurfaceMapping mapping{
      .guest = {.width = 1280, .height = 720},
      .host = {.width = 853, .height = 480},
  };

  // Source out of guest bounds
  SR_CHECK(!PlanScenePixelCopy(mapping, mapping, {.left = 0, .top = 0, .right = 1281, .bottom = 720}, 0, 0).has_value());
  SR_CHECK(!PlanScenePixelCopy(mapping, mapping, {.left = -1, .top = 0, .right = 100, .bottom = 100}, 0, 0).has_value());

  // Destination out of guest bounds
  SR_CHECK(!PlanScenePixelCopy(mapping, mapping, {.left = 0, .top = 0, .right = 100, .bottom = 100}, 1200, 0).has_value());
  SR_CHECK(!PlanScenePixelCopy(mapping, mapping, {.left = 0, .top = 0, .right = 100, .bottom = 100}, 0, -5).has_value());

  // Empty or inverted source rect
  SR_CHECK(!PlanScenePixelCopy(mapping, mapping, {.left = 10, .top = 10, .right = 10, .bottom = 20}, 0, 0).has_value());
  SR_CHECK(!PlanScenePixelCopy(mapping, mapping, {.left = 20, .top = 10, .right = 10, .bottom = 20}, 0, 0).has_value());
}
