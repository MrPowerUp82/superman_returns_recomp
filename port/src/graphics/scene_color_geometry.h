#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

#include "scene_color_boundary.h"

namespace superman_returns::graphics {
// This is ONLY a necessary shape primitive for SceneColorDrawProof. It proves
// coverage of the valid rectangle's pixel CENTERS, not color writes, lineage,
// shader identity, HUD placement, multisample coverage, or DLSS readiness.
//
// GPU caller obligations before using true as full_cover_overwrite evidence:
// * Freeze the ACTUAL issued triangle-list POSITION inputs, index/base ordering,
//   declarations, buffer versions, and constants. Apply the source-audited VS
//   transforms and ALL final clip fixups; pass the resulting D3D clip positions.
//   Neither a shader hash nor a buffer/target size supplies this evidence.
// * Match the actual target, valid rect, viewport and scissor. Prove ordinary
//   solid triangle rasterization, no removed triangles (including cull/clip
//   distances), no later geometry/tessellation transform, and appropriate sample
//   locations. This covers centers only; MSAA needs a separate sample proof.
// * Separately prove no depth/stencil/alpha/discard/coverage suppression and
//   complete RGB overwrite, correct bindings/specialization and source lineage.
// * Bound CPU-to-GPU projection, clipping, viewport and raster setup error to
//   LESS than kSceneColorCoverageMarginPixels in each screen axis, including
//   fixed-point snapping. Shared endpoints must remain identical on the GPU,
//   with the standard watertight triangle fill rule along their internal edge.
//   The margin is a conservative policy, NOT a universal GPU precision theorem.
//   If that error bound or the executed shader arithmetic is unproven, reject
//   at the caller even when this mathematical shape test returns true.
//
// Numeric domain (deliberately conservative): targets <=16384 in each axis;
// viewport >=1 pixel and wholly inside target; clip W in [2^-16,2^16]; no
// nonzero subnormals; projected vertices within +/-32768 pixels. Computation is
// in outward-rounded binary64 intervals from the exact supplied float values;
// uncertain edge signs/clearance fail closed. Known fast-math modes decline all
// proofs; IEEE arithmetic is required. No epsilon welding: shared
// endpoints must have equal complete XYZW, not just near-equal projected XY.
// Thus some mathematically sufficient draws fail closed. Outer edges through
// or within 1/16 pixel of a required center are rejected without guessing their
// top-left ownership. Clip XY may extend outside [-W,W]; D3D XY clipping cannot
// remove the required centers, which must also lie strictly inside the viewport.
using SceneColorClipVertex = std::array<float, 4>;
struct SceneColorGeometryViewport {
  float x = 0, y = 0, width = 0, height = 0;
  float min_depth = 0, max_depth = 1;
};
struct SceneColorGeometryInput {
  std::span<const SceneColorClipVertex> clip_vertices{};
  uint32_t target_width = 0, target_height = 0;
  SceneColorRect valid_rect{}, scissor{};
  SceneColorGeometryViewport viewport{};
  bool raster_cull_keeps_all = false;
};
inline constexpr double kSceneColorCoverageMarginPixels = 1.0 / 16.0;

namespace scene_color_geometry_detail {
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53);
struct Interval { double low = 0, high = 0; };
struct Point { Interval x, y; };
inline Interval Exact(double value) { return {value, value}; }
inline Interval Enclose(double low, double high) {
  constexpr double infinity = std::numeric_limits<double>::infinity();
  return {std::nextafter(low, -infinity), std::nextafter(high, infinity)};
}
inline Interval Add(Interval a, Interval b) { return Enclose(a.low + b.low, a.high + b.high); }
inline Interval Subtract(Interval a, Interval b) { return Enclose(a.low - b.high, a.high - b.low); }
inline Interval Multiply(Interval a, Interval b) {
  const std::array<double, 4> products{a.low * b.low, a.low * b.high, a.high * b.low, a.high * b.high};
  return Enclose(*std::min_element(products.begin(), products.end()),
                 *std::max_element(products.begin(), products.end()));
}
inline Interval DividePositive(double numerator, double denominator) {
  const double value = numerator / denominator;
  return Enclose(value, value);
}
inline Interval Absolute(Interval a) {
  return {a.low > 0 ? a.low : a.high < 0 ? -a.high : 0,
          std::max(std::abs(a.low), std::abs(a.high))};
}
inline bool ValidRect(SceneColorRect rect, uint32_t width, uint32_t height) {
  return rect.left < rect.right && rect.top < rect.bottom &&
      rect.right <= width && rect.bottom <= height;
}
// Sign is zero in the uncertainty band. Scaling by edge L1 length gives an
// axis-wise pixel margin rather than an area epsilon dependent on target size.
inline int Side(Point a, Point b, Point p) {
  const auto dx = Subtract(b.x, a.x), dy = Subtract(b.y, a.y);
  const auto area = Subtract(Multiply(dx, Subtract(p.y, a.y)), Multiply(dy, Subtract(p.x, a.x)));
  const auto margin = Multiply(Exact(kSceneColorCoverageMarginPixels), Add(Absolute(dx), Absolute(dy)));
  return area.low > margin.high ? 1 : area.high < -margin.high ? -1 : 0;
}
}  // namespace scene_color_geometry_detail

// No state, allocation, mutation or inference from shader/resource names.
inline bool ProveSceneColorGeometryCoverage(const SceneColorGeometryInput& input) {
  using namespace scene_color_geometry_detail;
#if defined(__FAST_MATH__) || defined(_M_FP_FAST) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__)
  // Reassociation/finite-only assumptions invalidate the interval and NaN proof.
  return false;
#endif
  const auto count = input.clip_vertices.size();
  if (!input.raster_cull_keeps_all || (count != 3 && count != 6) ||
      !input.target_width || !input.target_height ||
      input.target_width > 16384 || input.target_height > 16384 ||
      !ValidRect(input.valid_rect, input.target_width, input.target_height) ||
      !ValidRect(input.scissor, input.target_width, input.target_height)) return false;
  const auto rect = input.valid_rect, scissor = input.scissor;
  if (scissor.left > rect.left || scissor.top > rect.top ||
      scissor.right < rect.right || scissor.bottom < rect.bottom) return false;
  const auto& vp = input.viewport;
  for (float v : {vp.x, vp.y, vp.width, vp.height, vp.min_depth, vp.max_depth})
    if (!std::isfinite(v) || std::fpclassify(v) == FP_SUBNORMAL) return false;
  if (vp.x < 0 || vp.y < 0 || vp.width < 1 || vp.height < 1 ||
      vp.width > input.target_width || vp.height > input.target_height ||
      vp.x > double(input.target_width) - vp.width ||
      vp.y > double(input.target_height) - vp.height ||
      vp.min_depth < 0 || vp.max_depth > 1 || vp.min_depth > vp.max_depth) return false;

  const double left = double(rect.left) + .5, top = double(rect.top) + .5;
  const double right = double(rect.right) - .5, bottom = double(rect.bottom) - .5;
  constexpr double margin = kSceneColorCoverageMarginPixels;
  if (Subtract(Exact(left), Exact(vp.x)).low <= margin ||
      Subtract(Exact(top), Exact(vp.y)).low <= margin ||
      Subtract(Add(Exact(vp.x), Exact(vp.width)), Exact(right)).low <= margin ||
      Subtract(Add(Exact(vp.y), Exact(vp.height)), Exact(bottom)).low <= margin) return false;

  std::array<Point, 6> points{};
  for (size_t i = 0; i < count; ++i) {
    const auto& v = input.clip_vertices[i];
    for (float component : v)
      if (!std::isfinite(component) || std::fpclassify(component) == FP_SUBNORMAL) return false;
    if (v[3] < 0x1p-16f || v[3] > 0x1p16f || v[2] < 0 || v[2] > v[3]) return false;
    const auto x = DividePositive(v[0], v[3]), y = DividePositive(v[1], v[3]);
    points[i] = {Add(Exact(vp.x), Multiply(Add(x, Exact(1)), Exact(.5 * vp.width))),
                 Add(Exact(vp.y), Multiply(Subtract(Exact(1), y), Exact(.5 * vp.height)))};
    for (const auto coordinate : {points[i].x, points[i].y})
      if (!std::isfinite(coordinate.low) || !std::isfinite(coordinate.high) ||
          coordinate.low < -32768 || coordinate.high > 32768) return false;
  }

  std::array<Point, 4> polygon{points[0], points[1], points[2], {}};
  size_t polygon_size = 3;
  if (count == 6) {
    std::array<size_t, 4> unique{};
    std::array<size_t, 6> index{};
    size_t unique_count = 0;
    for (size_t i = 0; i < count; ++i) {
      size_t j = 0;
      while (j < unique_count && input.clip_vertices[unique[j]] != input.clip_vertices[i]) ++j;
      if (j == unique_count) {
        if (unique_count == unique.size()) return false;
        unique[unique_count++] = i;
      }
      index[i] = j;
    }
    if (unique_count != 4) return false;
    for (size_t first : {size_t(0), size_t(3)})
      if (index[first] == index[first + 1] || index[first] == index[first + 2] ||
          index[first + 1] == index[first + 2]) return false;

    std::array<size_t, 2> shared{};
    size_t shared_count = 0, other_first = 0, other_second = 0;
    for (size_t i = 0; i < 3; ++i) {
      bool common = false;
      for (size_t j = 3; j < 6; ++j) common |= index[i] == index[j];
      if (common) {
        if (shared_count == shared.size()) return false;
        shared[shared_count++] = i;
      } else other_first = i;
    }
    if (shared_count != 2) return false;
    for (size_t j = 3; j < 6; ++j)
      if (index[j] != index[shared[0]] && index[j] != index[shared[1]]) other_second = j;

    const auto a = points[shared[0]], b = points[shared[1]];
    const int side = Side(a, b, points[other_first]);
    if (!side || Side(a, b, points[other_second]) != -side) return false;
    // An exactly shared diagonal with opposite other vertices gives disjoint
    // triangle interiors. Strict convexity below proves the union has no notch.
    polygon = {a, points[other_first], b, points[other_second]};
    polygon_size = 4;
  }
  const int winding = Side(polygon[0], polygon[1], polygon[2]);
  if (!winding) return false;
  for (size_t i = 0; i < polygon_size; ++i)
    if (Side(polygon[i], polygon[(i + 1) % polygon_size], polygon[(i + 2) % polygon_size]) != winding)
      return false;

  const std::array<Point, 4> centers{{{Exact(left), Exact(top)}, {Exact(right), Exact(top)},
                                     {Exact(right), Exact(bottom)}, {Exact(left), Exact(bottom)}}};
  for (size_t i = 0; i < polygon_size; ++i)
    for (const auto center : centers)
      if (Side(polygon[i], polygon[(i + 1) % polygon_size], center) != winding) return false;
  // A convex polygon contains the convex hull of these four centers, including
  // every pixel center in the rectangle. Four-corner tests alone on a UNION of
  // arbitrary triangles would be unsound; the topology proof above is required.
  return true;
}
}  // namespace superman_returns::graphics
