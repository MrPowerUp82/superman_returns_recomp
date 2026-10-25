#pragma once

#include <array>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

#include "scene_color_geometry.h"

namespace superman_returns::graphics {
// Caller-selected, source-audited arithmetic roles, not shader/hash classifiers.
// Local artifacts: 0408 lines 841..855 forces W=1; 815D 841..854 copies W;
// 6952 849..879 projects with dot(input.wzxy, row.wzxy) and shared XY fixups.
enum class SceneColorVertexRole : uint32_t {
  kUnknown,
  kPassthroughForceW1,
  kPassthroughPreserveW,
  kProjectedSprite,
};

struct SceneColorVertexInput {
  // HOST-order, already frozen F32 POSITION bytes, without a capture-file
  // header. Caller proves actual GPU declaration/fetch, bytes, versions and
  // topology/order. F32x2 defaults Z=0,W=1; F32x3 defaults W=1.
  // Unaligned positions are supported; unread color/UV/padding is ignored.
  std::span<const std::byte> host_bytes{};
  size_t stride = 0, vertex_count = 0, position_offset = 0;
  uint32_t position_components = 0;
  bool quads = false; // Four vertices -> 012023; otherwise triangle list, 3 or 6.
  SceneColorVertexRole role = SceneColorVertexRole::kUnknown;
  std::array<float, 16> matrix_rows{}; // Actual uploaded rows; unread for passthrough.
  // Actual uploaded shared state, never a guessed camera or mirror fallback.
  std::array<float, 4> screen_xform{1, 1, 0, 0};
  std::array<float, 2> half_pixel{};
};

struct SceneColorVertexInputResult {
  std::array<SceneColorClipVertex, 6> clip_vertices{};
  uint32_t count = 0;
  // positions_decoded verifies only the supplied CPU layout, not its GPU origin.
  // This API does not inspect UV data and NEVER establishes uv_layout_proven.
  bool positions_decoded = false, uv_layout_proven = false;
  // Exact numerical GPU-position correspondence is available for an audited
  // F32 copy/force-W1 role with identity shared fixups and no output subnormals.
  // CPU copies preserve bits, including signed zero; zero-sign differences in
  // the shader's identity arithmetic do not affect numerical geometry coverage.
  // Nonidentity fixups and ALL projected results require a caller-bounded
  // CPU/GPU arithmetic/raster error proof before coverage can be qualified.
  // finite_positive_w describes the returned CPU vertices only. In projected
  // paths GPU W positivity/finiteness still needs the caller's error/plane proof.
  bool exact_clip_positions = false, finite_positive_w = false;
  // Exact numeric equality of decoded source Z AND source W, before force-W1.
  // constant_output_w additionally requires equal CPU output W and a structural
  // mathematical proof: each varying source lane has a ZERO W-row coefficient
  // (or a passthrough role supplies constant W). Equal rounded W alone is not
  // proof. This suffices over the source component envelope; cancellation-only
  // constancy is deliberately unproved. Neither flag identifies a HUD/camera.
  bool constant_source_plane = false, constant_output_w = false;
  bool operator==(const SceneColorVertexInputResult&) const = default;
};

namespace scene_color_vertex_input_detail {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
// Explicit binary32 round points in the source's W,Z,X,Y order. Double holds
// finite F32 products; checks reject overflowing intermediates even if later
// terms could cancel them. GPU dot/FMA/reassociation may differ: approximate.
inline bool RoundF32(double value, float& output) {
  constexpr double limit = std::numeric_limits<float>::max();
  if (!std::isfinite(value) || value < -limit || value > limit) return false;
  output = static_cast<float>(value);
  return std::isfinite(output);
}
inline bool Project(const SceneColorClipVertex& source,
                    const std::array<float, 16>& rows, SceneColorClipVertex& clip) {
  constexpr std::array<size_t, 4> order{3, 2, 0, 1};
  for (size_t row = 0; row < 4; ++row) {
    float sum = 0;
    for (size_t term = 0; term < order.size(); ++term) {
      const size_t lane = order[term];
      float product;
      if (!RoundF32(double(source[lane]) * rows[row * 4 + lane], product)) return false;
      if (term == 0) sum = product;
      else if (!RoundF32(double(sum) + product, sum)) return false;
    }
    clip[row] = sum;
  }
  return true;
}
inline bool Fixup(SceneColorClipVertex& clip, const std::array<float, 4>& screen,
                  const std::array<float, 2>& half_pixel) {
  for (size_t axis = 0; axis < 2; ++axis) {
    float scaled, offset, half, sum;
    if (!RoundF32(double(clip[axis]) * screen[axis], scaled) ||
        !RoundF32(double(screen[axis + 2]) * clip[3], offset) ||
        !RoundF32(double(half_pixel[axis]) * clip[3], half) ||
        !RoundF32(double(scaled) + offset, sum) ||
        !RoundF32(double(sum) + half, clip[axis])) return false;
  }
  return true;
}
}  // namespace scene_color_vertex_input_detail

// Allocation-free, no classification, coverage, pipeline-state or lineage proof.
// Rejects invalid layouts, overflow/nonfinite intermediates and output W <= 0;
// permits ANY finite Z (D3D depth/clip range belongs to the coverage consumer).
// Failure leaves every output field untouched; input spans are never written.
// IEEE round-to-nearest is required. Subnormals may decode but are never marked
// exact; caller must account for GPU denormal handling in approximate paths.
inline bool DecodeSceneColorVertexInput(const SceneColorVertexInput& input,
                                       SceneColorVertexInputResult& output) {
  using namespace scene_color_vertex_input_detail;
#if defined(__FAST_MATH__) || defined(_M_FP_FAST) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__)
  return false;
#endif
  const bool projected = input.role == SceneColorVertexRole::kProjectedSprite;
  const bool force_w = input.role == SceneColorVertexRole::kPassthroughForceW1;
  if ((!projected && !force_w && input.role != SceneColorVertexRole::kPassthroughPreserveW) ||
      (input.quads ? input.vertex_count != 4 : input.vertex_count != 3 && input.vertex_count != 6) ||
      input.position_components < 2 || input.position_components > 4 || !input.stride ||
      input.position_offset > input.stride || std::fegetround() != FE_TONEAREST) return false;
  const size_t position_bytes = input.position_components * sizeof(float);
  if (position_bytes > input.stride - input.position_offset) return false;
  const size_t first_end = input.position_offset + position_bytes; // <= stride, cannot overflow.
  if (first_end > input.host_bytes.size() ||
      input.vertex_count - 1 > (input.host_bytes.size() - first_end) / input.stride) return false;
  for (float value : input.screen_xform) if (!std::isfinite(value)) return false;
  for (float value : input.half_pixel) if (!std::isfinite(value)) return false;
  if (projected) for (float value : input.matrix_rows) if (!std::isfinite(value)) return false;
  const bool identity_fix = input.screen_xform == std::array<float, 4>{1, 1, 0, 0} &&
                            input.half_pixel == std::array<float, 2>{0, 0};

  std::array<SceneColorClipVertex, 6> source{}, clip{};
  std::array<bool, 4> constant{true, true, true, true};
  bool exact = !projected && identity_fix;
  for (size_t vertex = 0; vertex < input.vertex_count; ++vertex) {
    source[vertex] = {0, 0, 0, 1};
    std::memcpy(source[vertex].data(), input.host_bytes.data() +
                vertex * input.stride + input.position_offset, position_bytes);
    for (size_t lane = 0; lane < 4; ++lane) {
      if (!std::isfinite(source[vertex][lane])) return false;
      constant[lane] = constant[lane] && source[vertex][lane] == source[0][lane];
    }
    if (projected) {
      if (!Project(source[vertex], input.matrix_rows, clip[vertex])) return false;
    } else {
      clip[vertex] = source[vertex];
      if (force_w) clip[vertex][3] = 1;
    }
    if (clip[vertex][3] <= 0) return false;
    // Identity shared state takes the exact finite-F32 copy path. Otherwise
    // execute scale + offset*W, THEN half_pixel*W, preserving Z/W.
    if (!identity_fix && !Fixup(clip[vertex], input.screen_xform, input.half_pixel)) return false;
    for (float value : clip[vertex]) {
      if (!std::isfinite(value)) return false;
      if (std::fpclassify(value) == FP_SUBNORMAL) exact = false;
    }
  }

  bool constant_w = force_w || (!projected && constant[3]);
  if (projected) {
    constant_w = true;
    for (size_t lane = 0; lane < 4; ++lane)
      if (!constant[lane] && input.matrix_rows[12 + lane] != 0) constant_w = false;
  }
  for (size_t vertex = 1; vertex < input.vertex_count; ++vertex)
    constant_w = constant_w && clip[vertex][3] == clip[0][3];

  SceneColorVertexInputResult result;
  result.count = input.quads ? 6 : static_cast<uint32_t>(input.vertex_count);
  constexpr std::array<size_t, 6> quad_order{0, 1, 2, 0, 2, 3};
  for (size_t vertex = 0; vertex < result.count; ++vertex)
    result.clip_vertices[vertex] = clip[input.quads ? quad_order[vertex] : vertex];
  result.positions_decoded = result.finite_positive_w = true;
  result.exact_clip_positions = exact;
  result.constant_source_plane = constant[2] && constant[3];
  result.constant_output_w = constant_w;
  output = result;
  return true;
}
}  // namespace superman_returns::graphics
