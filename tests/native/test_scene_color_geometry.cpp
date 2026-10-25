#include "test_main.h"
#include "../../port/src/graphics/scene_color_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace superman_returns::graphics;

namespace {
using Vertex = SceneColorClipVertex;
constexpr std::array<Vertex, 3> kTriangle{{{-1, 1, .5f, 1}, {3, 1, .5f, 1}, {-1, -3, .5f, 1}}};
constexpr std::array<Vertex, 6> kQuad{{{-1, 1, .5f, 1}, {1, 1, .5f, 1}, {1, -1, .5f, 1},
                                    {-1, 1, .5f, 1}, {1, -1, .5f, 1}, {-1, -1, .5f, 1}}};
SceneColorGeometryInput Input(std::span<const Vertex> vertices) {
  SceneColorGeometryInput input;
  input.clip_vertices = vertices;
  input.target_width = 64; input.target_height = 32;
  input.valid_rect = input.scissor = {0, 0, 64, 32};
  input.viewport = {0, 0, 64, 32, 0, 1};
  input.raster_cull_keeps_all = true;
  return input;
}
Vertex Screen(float x, float y, float w = 1) {
  // Exact binary arithmetic for these small fixtures: target/viewport 64 x 32.
  return {(x / 32.f - 1.f) * w, (1.f - y / 16.f) * w, .5f * w, w};
}
std::array<Vertex, 6> Rect(float left, float top, float right, float bottom) {
  const auto a = Screen(left, top), b = Screen(right, top);
  const auto c = Screen(right, bottom), d = Screen(left, bottom);
  return {a, b, c, a, c, d};
}
}  // namespace

SR_TEST(scene_geometry_defaults_and_missing_cull_attestation_fail_closed) {
  SR_CHECK(!ProveSceneColorGeometryCoverage({}));
  auto input = Input(kQuad); input.raster_cull_keeps_all = false;
  SR_CHECK(!ProveSceneColorGeometryCoverage(input));
}

SR_TEST(scene_geometry_accepts_standard_fullscreen_triangle_and_shared_diagonal_quad) {
  SR_CHECK(ProveSceneColorGeometryCoverage(Input(kTriangle)));
  SR_CHECK(ProveSceneColorGeometryCoverage(Input(kQuad)));
  // Center lies exactly on the internal diagonal: only outer edges need margin.
  auto input = Input(kQuad); input.valid_rect = {31, 15, 33, 17};
  SR_CHECK(ProveSceneColorGeometryCoverage(input));
}

SR_TEST(scene_geometry_accepts_triangle_permutations_and_both_quad_diagonals) {
  std::array<int, 3> order{0, 1, 2};
  do {
    const std::array<Vertex, 3> triangle{kTriangle[order[0]], kTriangle[order[1]], kTriangle[order[2]]};
    SR_CHECK(ProveSceneColorGeometryCoverage(Input(triangle)));
  } while (std::next_permutation(order.begin(), order.end()));
  const std::array<Vertex, 4> corners{kQuad[0], kQuad[1], kQuad[2], kQuad[5]};
  for (int rotation = 0; rotation < 4; ++rotation) {
    std::array<Vertex, 6> quad{corners[rotation], corners[(rotation + 1) % 4], corners[(rotation + 2) % 4],
                               corners[rotation], corners[(rotation + 2) % 4], corners[(rotation + 3) % 4]};
    SR_CHECK(ProveSceneColorGeometryCoverage(Input(quad)));
    std::swap(quad[0], quad[2]); // Mixed winding allowed only by explicit cull attestation.
    std::swap(quad[3], quad[5]);
    SR_CHECK(ProveSceneColorGeometryCoverage(Input(quad)));
  }
}

SR_TEST(scene_geometry_half_pixel_boundary_is_conservative_without_top_left_model) {
  // Edge through pixel center x=.5 is deliberately rejected, regardless of winding.
  const auto boundary = Rect(.5f, 0, 64, 32);
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(boundary)));
  const auto almost = Rect(.5f - 1.f / 32.f, 0, 64, 32);
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(almost)));
  const auto exactly_margin = Rect(.5f - 1.f / 16.f, 0, 64, 32);
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(exactly_margin)));
  const auto safely_inside = Rect(.5f - 1.f / 8.f, 0, 64, 32);
  SR_CHECK(ProveSceneColorGeometryCoverage(Input(safely_inside)));
  const auto too_far = Rect(.5f + 1.f / 8.f, 0, 64, 32);
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(too_far)));
}

SR_TEST(scene_geometry_projection_roundoff_cannot_invent_strict_edge_clearance) {
  // Exact left edge = 7/16 + 2^-60: clearance is LESS than 1/16 pixel.
  // Unguarded binary64 projection rounds -17/24 down and loses tiny vp.x,
  // incorrectly manufacturing a clearance greater than 1/16.
  const Vertex a{-17, 24, 12, 24}, b{24, 24, 12, 24};
  const Vertex c{24, -24, 12, 24}, d{-17, -24, 12, 24};
  const std::array<Vertex, 6> quad{a, b, c, a, c, d};
  auto input = Input(quad);
  input.target_width = 4; input.target_height = 3;
  input.valid_rect = input.scissor = {0, 0, 3, 3};
  input.viewport = {0x1p-60f, 0, 3, 3, 0, 1};
  SR_CHECK(!ProveSceneColorGeometryCoverage(input));
}

SR_TEST(scene_geometry_accepts_subrect_scissor_and_offset_viewport) {
  auto input = Input(kQuad);
  input.valid_rect = input.scissor = {8, 4, 56, 28};
  input.viewport = {8, 4, 48, 24, 0, 1};
  SR_CHECK(ProveSceneColorGeometryCoverage(input));
  const auto local = Rect(8, 4, 56, 28);
  input = Input(local); input.valid_rect = input.scissor = {8, 4, 56, 28};
  SR_CHECK(ProveSceneColorGeometryCoverage(input));
  input.valid_rect = input.scissor = {20, 10, 21, 11};
  SR_CHECK(ProveSceneColorGeometryCoverage(input));
}

SR_TEST(scene_geometry_rejects_incomplete_scissor_or_viewport_despite_oversized_geometry) {
  auto input = Input(kTriangle); input.scissor.left = 1;
  SR_CHECK(!ProveSceneColorGeometryCoverage(input));
  input = Input(kTriangle); input.scissor.bottom = 31;
  SR_CHECK(!ProveSceneColorGeometryCoverage(input));
  input = Input(kTriangle); input.viewport.width = 32;
  SR_CHECK(!ProveSceneColorGeometryCoverage(input));
  input = Input(kTriangle); input.viewport.x = .5f; input.viewport.width = 63.5f;
  SR_CHECK(!ProveSceneColorGeometryCoverage(input));
}

SR_TEST(scene_geometry_rejects_empty_unsupported_counts_and_bad_rectangles) {
  std::array<Vertex, 9> vertices{}; std::copy(kQuad.begin(), kQuad.end(), vertices.begin());
  for (size_t count : {0u, 1u, 2u, 4u, 5u, 7u, 8u, 9u})
    SR_CHECK(!ProveSceneColorGeometryCoverage(Input(std::span(vertices).first(count))));
  for (SceneColorRect rect : {SceneColorRect{}, {4, 0, 4, 32}, {9, 8, 2, 20}, {0, 0, 65, 32},
                              {0, 0, 64, 33}, {0, 20, 64, 10}, {0, 0, UINT32_MAX, UINT32_MAX}}) {
    auto input = Input(kQuad); input.valid_rect = rect;
    SR_CHECK(!ProveSceneColorGeometryCoverage(input));
    input = Input(kQuad); input.scissor = rect;
    SR_CHECK(!ProveSceneColorGeometryCoverage(input));
  }
  auto input = Input(kQuad); input.target_width = 0;
  SR_CHECK(!ProveSceneColorGeometryCoverage(input));
  input = Input(kQuad); input.target_height = UINT32_MAX;
  SR_CHECK(!ProveSceneColorGeometryCoverage(input));
}

SR_TEST(scene_geometry_rejects_invalid_viewports) {
  for (SceneColorGeometryViewport viewport : {
      SceneColorGeometryViewport{}, {-1, 0, 64, 32, 0, 1}, {0, 0, -64, 32, 0, 1},
      {0, 0, 64, 0, 0, 1}, {0, 0, 65, 32, 0, 1}, {0, 0, 64, 32, -1, 1},
      {0, 0, 64, 32, 0, 2}, {0, 0, 64, 32, .8f, .2f}})
    { auto input = Input(kQuad); input.viewport = viewport;
      SR_CHECK(!ProveSceneColorGeometryCoverage(input)); }
}

SR_TEST(scene_geometry_rejects_disjoint_strips_even_when_union_contains_four_rect_corners) {
  // All four extreme centers are inside the union, but the middle has a hole.
  const std::array<Vertex, 6> strips{Screen(-64, -8), Screen(128, -8), Screen(32, 8),
                                     Screen(-64, 40), Screen(32, 24), Screen(128, 40)};
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(strips)));
  const std::array<Vertex, 6> disjoint{Screen(-4, -4), Screen(36, -4), Screen(-4, 20),
                                       Screen(68, 36), Screen(28, 36), Screen(68, 12)};
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(disjoint)));
}

SR_TEST(scene_geometry_rejects_notches_overlap_bowtie_and_nonshared_quad) {
  // Four unique positions are necessary but insufficient: a concave union has a notch.
  const auto a = Screen(-8, -8), b = Screen(72, -8), c = Screen(32, 12), d = Screen(-8, 40);
  const std::array<Vertex, 6> notch{a, b, c, a, c, d};
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(notch)));
  const auto p = Screen(0, 0), q = Screen(64, 0), r = Screen(64, 32), s = Screen(0, 32);
  const std::array<Vertex, 6> bowtie{p, r, q, p, q, s}; // Other vertices same side of shared edge.
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(bowtie)));
  const std::array<Vertex, 6> repeated{p, q, r, r, p, q};
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(repeated)));
  auto mismatch = kQuad; mismatch[3][3] = 2; // Same index-like placement is no shared clip position.
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(mismatch)));
}

SR_TEST(scene_geometry_rejects_degenerate_collinear_and_zero_area_triangles) {
  const std::array<Vertex, 3> repeated{kTriangle[0], kTriangle[0], kTriangle[1]};
  const std::array<Vertex, 3> line{Screen(0, 0), Screen(32, 16), Screen(64, 32)};
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(repeated)));
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(line)));
  const std::array<Vertex, 6> flat{Screen(0, 0), Screen(64, 0), Screen(32, 0),
                                  Screen(0, 0), Screen(32, 0), Screen(0, 32)};
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(flat)));
}

SR_TEST(scene_geometry_accepts_positive_perspective_w_and_d3d_depth_boundaries) {
  auto triangle = kTriangle;
  for (size_t i = 0; i < triangle.size(); ++i)
    for (float& component : triangle[i]) component *= float(1u << i);
  SR_CHECK(ProveSceneColorGeometryCoverage(Input(triangle)));
  auto quad = kQuad;
  const std::array<float, 6> weights{1, 2, 4, 1, 4, 8};
  for (size_t i = 0; i < quad.size(); ++i)
    for (float& component : quad[i]) component *= weights[i];
  SR_CHECK(ProveSceneColorGeometryCoverage(Input(quad)));
  for (float depth : {0.f, 1.f}) {
    quad = kQuad; for (auto& vertex : quad) vertex[2] = depth;
    SR_CHECK(ProveSceneColorGeometryCoverage(Input(quad)));
  }
}

SR_TEST(scene_geometry_rejects_nonpositive_w_and_depth_outside_zero_to_w) {
  for (float w : {0.f, -0.f, -1.f}) {
    auto triangle = kTriangle; triangle[1][3] = w;
    SR_CHECK(!ProveSceneColorGeometryCoverage(Input(triangle)));
  }
  for (float z : {-.0001f, 1.0001f}) {
    auto triangle = kTriangle; triangle[1][2] = z;
    SR_CHECK(!ProveSceneColorGeometryCoverage(Input(triangle)));
  }
}

SR_TEST(scene_geometry_rejects_nonfinite_vertices_and_viewport_components) {
  for (float invalid : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
    for (size_t vertex = 0; vertex < 6; ++vertex) for (size_t lane = 0; lane < 4; ++lane) {
      auto quad = kQuad; quad[vertex][lane] = invalid;
      SR_CHECK(!ProveSceneColorGeometryCoverage(Input(quad)));
    }
    for (size_t lane = 0; lane < 6; ++lane) {
      auto input = Input(kQuad);
      std::array<float*, 6> components{&input.viewport.x, &input.viewport.y, &input.viewport.width,
                                     &input.viewport.height, &input.viewport.min_depth, &input.viewport.max_depth};
      *components[lane] = invalid;
      SR_CHECK(!ProveSceneColorGeometryCoverage(input));
    }
  }
}

SR_TEST(scene_geometry_rejects_unbounded_precision_domain_and_leaves_inputs_unchanged) {
  auto triangle = kTriangle;
  triangle[1][0] = std::numeric_limits<float>::max();
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(triangle)));
  for (float w : {0x1p-17f, 0x1p17f}) {
    triangle = kTriangle;
    for (auto& vertex : triangle) for (auto& component : vertex) component *= w;
    SR_CHECK(!ProveSceneColorGeometryCoverage(Input(triangle)));
  }
  triangle = kTriangle; triangle[1][2] = std::numeric_limits<float>::denorm_min();
  SR_CHECK(!ProveSceneColorGeometryCoverage(Input(triangle)));
  auto quad = kQuad; const auto copy = quad; const auto input = Input(quad);
  SR_CHECK(ProveSceneColorGeometryCoverage(input));
  SR_CHECK(copy == quad);
  SR_CHECK(input.valid_rect == (SceneColorRect{0, 0, 64, 32}));
}
