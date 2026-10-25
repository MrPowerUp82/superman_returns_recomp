#include "test_main.h"
#include "../../port/src/graphics/scene_color_vertex_input.h"

#include <array>
#include <bit>
#include <cstring>
#include <limits>

using namespace superman_returns::graphics;

namespace {
using Role = SceneColorVertexRole;
using Result = SceneColorVertexInputResult;
constexpr std::array<SceneColorClipVertex, 4> kCorners{{
    {-1, 1, 0, 1}, {1, 1, 0, 1}, {1, -1, 0, 1}, {-1, -1, 0, 1}}};
constexpr std::array<uint32_t, 6> kQuadOrder{0, 1, 2, 0, 2, 3};
constexpr std::array<float, 3> kNonfinite{
    std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::quiet_NaN()};

struct Fixture {
  std::array<std::byte, 192> bytes{};
  SceneColorVertexInput input;

  Fixture() {
    input.host_bytes = bytes;
    input.stride = 20;
    input.vertex_count = 4;
    input.position_components = 3;
    input.quads = true;
    input.role = Role::kPassthroughForceW1;
    input.matrix_rows = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (size_t i = 0; i < kCorners.size(); ++i) Position(i, kCorners[i]);
  }

  void Position(size_t i, SceneColorClipVertex value) {
    std::memcpy(bytes.data() + i * input.stride + input.position_offset,
                value.data(), input.position_components * sizeof(float));
  }

  bool Capture(Result& result) const { return DecodeSceneColorVertexInput(input, result); }
};

void Reject(const SceneColorVertexInput& input) {
  Result sentinel;
  sentinel.count = 99;
  sentinel.positions_decoded = sentinel.uv_layout_proven = true;
  sentinel.exact_clip_positions = sentinel.finite_positive_w = true;
  sentinel.constant_source_plane = sentinel.constant_output_w = true;
  for (auto& vertex : sentinel.clip_vertices) vertex = {17, 19, 23, 29};
  auto result = sentinel;
  SR_CHECK(!DecodeSceneColorVertexInput(input, result));
  SR_CHECK(result == sentinel);
}
}  // namespace

SR_TEST(scene_vertex_frozen_post_float3_quad_is_exact_and_expands_native_order) {
  Fixture fixture;
  for (Role role : {Role::kPassthroughForceW1, Role::kPassthroughPreserveW}) {
    fixture.input.role = role;
    // Both frozen post draws have this layout; matrix constants are unread.
    fixture.input.matrix_rows.fill(kNonfinite[2]);
    Result result;
    const bool decoded = fixture.Capture(result);
    SR_CHECK(decoded);
    if (!decoded) continue;
    SR_CHECK_EQ(result.count, 6u);
    for (size_t i = 0; i < kQuadOrder.size(); ++i)
      SR_CHECK(result.clip_vertices[i] == kCorners[kQuadOrder[i]]);
    SR_CHECK(result.positions_decoded);
    SR_CHECK(!result.uv_layout_proven);
    SR_CHECK(result.exact_clip_positions);
    SR_CHECK(result.finite_positive_w);
    SR_CHECK(result.constant_source_plane);
    SR_CHECK(result.constant_output_w);
  }
}

SR_TEST(scene_vertex_unaligned_float2_float3_float4_defaults_and_w_roles) {
  for (uint32_t components : {2u, 3u, 4u}) {
    Fixture fixture;
    fixture.input.stride = 25;
    fixture.input.position_offset = 1;  // memcpy, never cast an unaligned pointer.
    fixture.input.position_components = components;
    fixture.input.vertex_count = 3;
    fixture.input.quads = false;
    for (size_t i = 0; i < 3; ++i) fixture.Position(i, {2, 3, -4, float(i + 2)});
    for (Role role : {Role::kPassthroughForceW1, Role::kPassthroughPreserveW}) {
      fixture.input.role = role;
      Result result;
      const bool decoded = fixture.Capture(result);
      SR_CHECK(decoded);
      if (!decoded) continue;
      SR_CHECK_EQ(result.count, 3u);
      for (size_t i = 0; i < 3; ++i) {
        const float w = components == 4 && role == Role::kPassthroughPreserveW ? float(i + 2) : 1.f;
        SR_CHECK(result.clip_vertices[i] == (SceneColorClipVertex{2, 3, components >= 3 ? -4.f : 0.f, w}));
      }
      SR_CHECK_EQ(result.constant_source_plane, components != 4);
      SR_CHECK_EQ(result.constant_output_w, components != 4 || role == Role::kPassthroughForceW1);
      for (size_t i = 3; i < 6; ++i)
        SR_CHECK(result.clip_vertices[i] == SceneColorClipVertex{});
    }
  }
}

SR_TEST(scene_vertex_rejects_unsupported_topology_roles_layout_and_bounds_atomically) {
  Fixture fixture;
  for (bool quads : {false, true}) for (size_t count : {0u, 1u, 2u, 4u, 5u, 7u}) {
    if (quads && count == 4) continue;
    auto input = fixture.input; input.quads = quads; input.vertex_count = count;
    Reject(input);
  }
  for (size_t count : {3u, 6u}) {
    auto input = fixture.input; input.vertex_count = count;
    Reject(input);  // Quad flag requires exactly four inputs.
  }
  for (uint32_t components : {0u, 1u, 5u, UINT32_MAX}) {
    auto input = fixture.input; input.position_components = components;
    Reject(input);
  }
  for (Role role : {Role::kUnknown, static_cast<Role>(UINT32_MAX)}) {
    auto input = fixture.input; input.role = role;
    Reject(input);
  }
  auto input = fixture.input; input.host_bytes = {};
  Reject(input);
  input = fixture.input; input.host_bytes = std::span(fixture.bytes).first(71); // Last XYZ ends at 72.
  Reject(input);
  for (size_t stride : {size_t(0), size_t(11), std::numeric_limits<size_t>::max()}) {
    input = fixture.input; input.stride = stride;
    Reject(input);
  }
  for (size_t offset : {size_t(9), size_t(20), std::numeric_limits<size_t>::max()}) {
    input = fixture.input; input.position_offset = offset;
    Reject(input);
  }
  input = fixture.input; input.vertex_count = std::numeric_limits<size_t>::max();
  Reject(input);
  // Unread trailing padding is not required by the bounded position decoder.
  input = fixture.input; input.host_bytes = std::span(fixture.bytes).first(72);
  Result result;
  SR_CHECK(DecodeSceneColorVertexInput(input, result));
}

SR_TEST(scene_vertex_rejects_only_nonfinite_declared_position_lanes_not_uv_or_color_bytes) {
  Fixture fixture;
  fixture.input.stride = 24;
  fixture.input.position_components = 4;
  for (float invalid : kNonfinite) for (size_t vertex = 0; vertex < 4; ++vertex) {
    for (size_t lane = 0; lane < 4; ++lane) {
      for (size_t i = 0; i < 4; ++i) fixture.Position(i, kCorners[i]);
      auto position = kCorners[vertex]; position[lane] = invalid;
      fixture.Position(vertex, position);
      Reject(fixture.input); // Also rejects nonfinite declared W when force-W1 is selected.
    }
  }
  fixture.input.position_components = 3;
  for (size_t i = 0; i < 4; ++i) {
    fixture.Position(i, kCorners[i]);
    // Real sprite data has packed color which looks like NaN if misread as F32.
    std::memcpy(fixture.bytes.data() + i * 24 + 12, &kNonfinite[2], sizeof(float));
  }
  Result result;
  SR_CHECK(fixture.Capture(result));
  SR_CHECK(!result.uv_layout_proven);
}

SR_TEST(scene_vertex_validates_all_read_matrix_and_shared_fixup_lanes) {
  Fixture fixture;
  for (float invalid : kNonfinite) {
    for (Role role : {Role::kPassthroughForceW1, Role::kPassthroughPreserveW, Role::kProjectedSprite}) {
      for (size_t lane = 0; lane < 6; ++lane) {
        auto input = fixture.input; input.role = role;
        if (lane < 4) input.screen_xform[lane] = invalid;
        else input.half_pixel[lane - 4] = invalid;
        Reject(input);
      }
    }
    for (size_t lane = 0; lane < 16; ++lane) {
      auto input = fixture.input; input.role = Role::kProjectedSprite;
      input.matrix_rows[lane] = invalid;
      Reject(input);
    }
  }
}

SR_TEST(scene_vertex_applies_actual_shared_camera_fixups_without_a_depth_range_gate) {
  Fixture fixture;
  fixture.input.stride = 24; fixture.input.position_components = 4;
  fixture.input.role = Role::kPassthroughPreserveW;
  fixture.input.screen_xform = {2, -3, .5f, -.25f};
  fixture.input.half_pixel = {.125f, .25f};
  for (size_t i = 0; i < 4; ++i) fixture.Position(i, {2, 3, 4, 2});
  Result result;
  const bool decoded = fixture.Capture(result);
  SR_CHECK(decoded);
  if (!decoded) return;
  for (const auto& vertex : result.clip_vertices)
    SR_CHECK(vertex == (SceneColorClipVertex{5.25f, -9, 4, 2})); // Z > W is allowed here.
  SR_CHECK(!result.exact_clip_positions);
  for (size_t lane = 0; lane < 6; ++lane) {
    auto input = fixture.input;
    if (lane < 4) input.screen_xform[lane] += 1;
    else input.half_pixel[lane - 4] += 1;
    Result changed;
    SR_CHECK(DecodeSceneColorVertexInput(input, changed));
    SR_CHECK(changed.clip_vertices != result.clip_vertices);
  }
}

SR_TEST(scene_vertex_projected_sprite_uses_row_dots_in_wzxy_rounding_order) {
  Fixture fixture;
  fixture.input.role = Role::kProjectedSprite;
  fixture.input.position_components = 4; fixture.input.stride = 24;
  fixture.input.vertex_count = 3; fixture.input.quads = false;
  fixture.input.matrix_rows = {-16777216, 1, 1, 16777216, 2, 3, 5, 7,
                               0, 0, 1, 0, 0, 0, 0, 1};
  for (size_t i = 0; i < 3; ++i) fixture.Position(i, {1, 1, 1, 1});
  Result result;
  const bool decoded = fixture.Capture(result);
  SR_CHECK(decoded);
  if (!decoded) return;
  // (((W*row.W + Z*row.Z) + X*row.X) + Y*row.Y) = 1; XYZW accumulation gives 2.
  SR_CHECK(result.clip_vertices[0] == (SceneColorClipVertex{1, 17, 1, 1}));
  SR_CHECK(!result.exact_clip_positions);
  SR_CHECK(result.constant_output_w);
  auto input = fixture.input; input.matrix_rows[3] = 16777218;
  Result changed;
  SR_CHECK(DecodeSceneColorVertexInput(input, changed));
  SR_CHECK(changed.clip_vertices != result.clip_vertices);
  // Distinct XYZW and asymmetric rows also detect transposed-matrix mistakes.
  fixture.input.matrix_rows = {1, 2, 3, 4, 5, 7, 11, 13, 0, 0, 1, 0, 0, 0, 0, 1};
  for (size_t i = 0; i < 3; ++i) fixture.Position(i, {2, 3, 5, 7});
  SR_CHECK(fixture.Capture(result));
  SR_CHECK(result.clip_vertices[0] == (SceneColorClipVertex{51, 177, 5, 7}));
  for (const auto& [coefficient, expected_z] :
       std::array<std::array<float, 2>, 2>{{{-1, -5}, {4, 20}}}) {
    input = fixture.input; input.matrix_rows[10] = coefficient;
    SR_CHECK(DecodeSceneColorVertexInput(input, result));
    SR_CHECK_EQ(result.clip_vertices[0][2], expected_z); // Neither Z < 0 nor Z > W is a decode error.
  }
}

SR_TEST(scene_vertex_frozen_sprite_plane_does_not_imply_mathematically_constant_output_w) {
  Fixture fixture;
  fixture.input.stride = 24; fixture.input.vertex_count = 6;
  fixture.input.quads = false; fixture.input.role = Role::kProjectedSprite;
  // Exact F32 words from frozen d3174 rows at offset 64; no file or hash trust.
  constexpr std::array<uint32_t, 16> rows{
      0xbf9472d9, 0xb1200000, 0, 0, 0x31a00000, 0x4003f44f, 0xb1ac0000, 0xb9800000,
      0, 0x298c0000, 0xb7e905fd, 0x3f0000e9, 0xb3000000, 0xaf000000, 0x3f800001, 0};
  fixture.input.matrix_rows = std::bit_cast<std::array<float, 16>>(rows);
  fixture.input.half_pixel = {std::bit_cast<float>(0x3a4ccccdu), std::bit_cast<float>(0xbab60b61u)};
  const float extent = std::bit_cast<float>(0x4009f5cfu);
  const std::array<SceneColorClipVertex, 6> positions{{
      {extent, -extent, 100, 1}, {-extent, -extent, 100, 1}, {extent, extent, 100, 1},
      {extent, extent, 100, 1}, {-extent, -extent, 100, 1}, {-extent, extent, 100, 1}}};
  for (size_t i = 0; i < 6; ++i) fixture.Position(i, positions[i]);
  const auto bytes_before = fixture.bytes;
  const auto matrix_before = fixture.input.matrix_rows;
  const auto screen_before = fixture.input.screen_xform;
  const auto half_before = fixture.input.half_pixel;
  Result result;
  const bool decoded = fixture.Capture(result);
  SR_CHECK(decoded);
  if (!decoded) return;
  SR_CHECK(result.constant_source_plane);
  SR_CHECK(!result.constant_output_w); // Nonzero row3.x/y, even though rounded CPU W all match.
  SR_CHECK(!result.exact_clip_positions);
  SR_CHECK(result.finite_positive_w);
  for (const auto& vertex : result.clip_vertices)
    SR_CHECK_EQ(std::bit_cast<uint32_t>(vertex[3]), 0x42c80002u);
  SR_CHECK(fixture.bytes == bytes_before);
  SR_CHECK(fixture.input.matrix_rows == matrix_before);
  SR_CHECK(fixture.input.screen_xform == screen_before);
  SR_CHECK(fixture.input.half_pixel == half_before);
  auto input = fixture.input; input.matrix_rows[12] = input.matrix_rows[13] = 0;
  SR_CHECK(DecodeSceneColorVertexInput(input, result));
  SR_CHECK(result.constant_output_w); // Structural proof over this source plane, not a camera label.
}

SR_TEST(scene_vertex_source_z_and_source_w_variation_are_reported_separately_from_output_w) {
  Fixture fixture;
  fixture.input.stride = 24; fixture.input.position_components = 4;
  for (size_t i = 0; i < 4; ++i) fixture.Position(i, {float(i), 2, 100, 1});
  Result result;
  SR_CHECK(fixture.Capture(result));
  SR_CHECK(result.constant_source_plane);
  fixture.Position(3, {3, 2, 101, 1});
  SR_CHECK(fixture.Capture(result));
  SR_CHECK(!result.constant_source_plane);
  SR_CHECK(result.constant_output_w); // Forced W stays one despite changing Z.
  fixture.Position(3, {3, 2, 100, 2});
  SR_CHECK(fixture.Capture(result));
  SR_CHECK(!result.constant_source_plane);
  SR_CHECK(result.constant_output_w); // Flag concerns source W before force-W1.
  fixture.input.role = Role::kPassthroughPreserveW;
  SR_CHECK(fixture.Capture(result));
  SR_CHECK(!result.constant_output_w);
  fixture.input.role = Role::kProjectedSprite;
  fixture.input.matrix_rows = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0};
  SR_CHECK(fixture.Capture(result));
  SR_CHECK(result.constant_output_w); // W = source Z; varying source W has zero coefficient.
}

SR_TEST(scene_vertex_nonpositive_output_w_and_arithmetic_overflow_fail_atomically) {
  Fixture fixture;
  fixture.input.stride = 24; fixture.input.position_components = 4;
  fixture.input.role = Role::kPassthroughPreserveW;
  // Re-encode the complete fixture after changing layout. Earlier vertices
  // must be valid so rejection really exercises the final vertex's W.
  for (size_t i = 0; i < 4; ++i) fixture.Position(i, {1, 1, -100, 1});
  Result baseline;
  SR_CHECK(fixture.Capture(baseline));
  for (float w : {0.f, -0.f, -1.f}) {
    fixture.Position(3, {1, 1, -100, w});
    Reject(fixture.input);
    auto input = fixture.input; input.role = Role::kPassthroughForceW1;
    Result result;
    SR_CHECK(DecodeSceneColorVertexInput(input, result));
  }
  for (size_t i = 0; i < 4; ++i) fixture.Position(i, {2, 2, 2, 2});
  for (size_t lane = 0; lane < 16; ++lane) {
    auto input = fixture.input; input.role = Role::kProjectedSprite;
    input.matrix_rows[lane] = std::numeric_limits<float>::max();
    Reject(input);
  }
  for (size_t lane = 0; lane < 6; ++lane) {
    auto input = fixture.input;
    if (lane < 4) input.screen_xform[lane] = std::numeric_limits<float>::max();
    else input.half_pixel[lane - 4] = std::numeric_limits<float>::max();
    Reject(input);
  }
  auto input = fixture.input; input.role = Role::kProjectedSprite;
  input.matrix_rows[15] = -4;
  Reject(input);
  // Every product is finite, but ordered partial sum overflows, even if a later term cancels.
  input = fixture.input; input.role = Role::kProjectedSprite;
  const float half_max = std::numeric_limits<float>::max() / 2;
  input.matrix_rows[0] = -half_max; input.matrix_rows[2] = half_max; input.matrix_rows[3] = half_max;
  Reject(input);
}

SR_TEST(scene_vertex_exact_post_copy_preserves_finite_bits_and_input_snapshots) {
  Fixture fixture;
  fixture.input.position_components = 4; fixture.input.stride = 24;
  fixture.input.role = Role::kPassthroughPreserveW;
  for (uint32_t bits : {0u, 0x80000000u, 1u, 0x7f7fffffu, 0xff7fffffu}) {
    const float value = std::bit_cast<float>(bits);
    for (size_t i = 0; i < 4; ++i) fixture.Position(i, {value, value, value, 1});
    const auto bytes_before = fixture.bytes;
    const auto matrix_before = fixture.input.matrix_rows;
    const auto screen_before = fixture.input.screen_xform;
    const auto half_before = fixture.input.half_pixel;
    Result result;
    const bool decoded = fixture.Capture(result);
    SR_CHECK(decoded);
    if (!decoded) continue;
    for (const auto& vertex : result.clip_vertices)
      for (size_t lane = 0; lane < 3; ++lane)
        SR_CHECK_EQ(std::bit_cast<uint32_t>(vertex[lane]), bits);
    // Nonzero subnormals need an external GPU denormal-mode proof; decoding accepts them.
    SR_CHECK_EQ(result.exact_clip_positions, bits != 1);
    SR_CHECK(fixture.bytes == bytes_before);
    SR_CHECK(fixture.input.matrix_rows == matrix_before);
    SR_CHECK(fixture.input.screen_xform == screen_before);
    SR_CHECK(fixture.input.half_pixel == half_before);
    SR_CHECK_EQ(fixture.input.stride, 24u);
    SR_CHECK_EQ(fixture.input.position_offset, 0u);
    SR_CHECK_EQ(fixture.input.vertex_count, 4u);
    SR_CHECK_EQ(fixture.input.position_components, 4u);
    SR_CHECK(fixture.input.host_bytes.data() == fixture.bytes.data());
    SR_CHECK(fixture.input.quads);
    SR_CHECK(fixture.input.role == Role::kPassthroughPreserveW);
  }
}

SR_TEST(scene_vertex_nondefault_cpu_rounding_fails_closed_and_restores_normally) {
  Fixture fixture;
  const int original = std::fegetround();
  for (int mode : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    if (std::fesetround(mode) == 0) Reject(fixture.input);
  }
  SR_CHECK_EQ(std::fesetround(original), 0);
  Result result;
  SR_CHECK(fixture.Capture(result));
}

SR_TEST(scene_vertex_decoder_leaves_coverage_and_pipeline_proofs_to_the_caller) {
  Fixture fixture;
  Result result;
  const bool decoded = fixture.Capture(result);
  SR_CHECK(decoded);
  if (!decoded) return;
  SceneColorGeometryInput geometry;
  geometry.clip_vertices = std::span(result.clip_vertices).first(result.count);
  geometry.target_width = 1280; geometry.target_height = 720;
  geometry.valid_rect = geometry.scissor = {0, 0, 1280, 720};
  geometry.viewport = {0, 0, 1280, 720, 0, 1};
  SR_CHECK(!ProveSceneColorGeometryCoverage(geometry));
  geometry.raster_cull_keeps_all = true;
  SR_CHECK(ProveSceneColorGeometryCoverage(geometry));
  for (size_t i = 0; i < 4; ++i) {
    auto position = kCorners[i]; position[0] *= .25f;
    fixture.Position(i, position);
  }
  SR_CHECK(fixture.Capture(result)); // Decoder success supplies no full-coverage/classification flag.
  SR_CHECK(!ProveSceneColorGeometryCoverage(geometry));
}
