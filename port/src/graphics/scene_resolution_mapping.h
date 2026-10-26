#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace superman_returns::graphics {

struct ScenePixelExtent {
  uint32_t width = 0;
  uint32_t height = 0;
  bool operator==(const ScenePixelExtent&) const = default;
};

struct SceneSurfaceMapping {
  ScenePixelExtent guest{};
  ScenePixelExtent host{};
  bool operator==(const SceneSurfaceMapping&) const = default;
};

struct ScenePixelRect {
  int32_t left = 0;
  int32_t top = 0;
  int32_t right = 0;
  int32_t bottom = 0;
  bool operator==(const ScenePixelRect&) const = default;
};

struct ScenePixelViewport {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
  float min_depth = 0;
  float max_depth = 1;
};

enum class SceneEdgeRounding {
  Nearest,
  Floor,
  Ceil
};

constexpr uint32_t kMinSceneExtent = 1;
constexpr uint32_t kMaxSceneExtent = 16384;

inline bool ValidSceneSurfaceMapping(const SceneSurfaceMapping& mapping) {
  return mapping.guest.width >= kMinSceneExtent && mapping.guest.width <= kMaxSceneExtent &&
         mapping.guest.height >= kMinSceneExtent && mapping.guest.height <= kMaxSceneExtent &&
         mapping.host.width >= kMinSceneExtent && mapping.host.width <= kMaxSceneExtent &&
         mapping.host.height >= kMinSceneExtent && mapping.host.height <= kMaxSceneExtent;
}

inline std::optional<int32_t> MapScenePixelEdge(
    int32_t edge,
    uint32_t guest_extent,
    uint32_t host_extent,
    SceneEdgeRounding rounding = SceneEdgeRounding::Nearest) {
  if (guest_extent < kMinSceneExtent || guest_extent > kMaxSceneExtent ||
      host_extent < kMinSceneExtent || host_extent > kMaxSceneExtent) {
    return std::nullopt;
  }

  const int64_t n = static_cast<int64_t>(edge) * static_cast<int64_t>(host_extent);
  const int64_t d = static_cast<int64_t>(guest_extent);

  int64_t result = 0;
  switch (rounding) {
    case SceneEdgeRounding::Floor: {
      if (n >= 0) {
        result = n / d;
      } else {
        result = (n % d == 0) ? (n / d) : ((n / d) - 1);
      }
      break;
    }
    case SceneEdgeRounding::Ceil: {
      if (n >= 0) {
        result = (n % d == 0) ? (n / d) : ((n / d) + 1);
      } else {
        result = n / d;
      }
      break;
    }
    case SceneEdgeRounding::Nearest: {
      if (n >= 0) {
        const int64_t q = n / d;
        const int64_t r = n % d;
        result = (2 * r >= d) ? (q + 1) : q;
      } else {
        const int64_t abs_n = -n;
        const int64_t q = abs_n / d;
        const int64_t r = abs_n % d;
        result = -((2 * r >= d) ? (q + 1) : q);
      }
      break;
    }
  }

  if (result < static_cast<int64_t>(std::numeric_limits<int32_t>::min()) ||
      result > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
    return std::nullopt;
  }

  return static_cast<int32_t>(result);
}

inline std::optional<ScenePixelRect> MapScenePixelRect(
    const SceneSurfaceMapping& mapping,
    ScenePixelRect rect,
    SceneEdgeRounding rounding = SceneEdgeRounding::Nearest) {
  if (!ValidSceneSurfaceMapping(mapping)) {
    return std::nullopt;
  }

  if (rect.left >= rect.right || rect.top >= rect.bottom) {
    return std::nullopt;
  }

  const auto mapped_left = MapScenePixelEdge(rect.left, mapping.guest.width, mapping.host.width, rounding);
  const auto mapped_right = MapScenePixelEdge(rect.right, mapping.guest.width, mapping.host.width, rounding);
  const auto mapped_top = MapScenePixelEdge(rect.top, mapping.guest.height, mapping.host.height, rounding);
  const auto mapped_bottom = MapScenePixelEdge(rect.bottom, mapping.guest.height, mapping.host.height, rounding);

  if (!mapped_left || !mapped_right || !mapped_top || !mapped_bottom) {
    return std::nullopt;
  }

  if (*mapped_left >= *mapped_right || *mapped_top >= *mapped_bottom) {
    return std::nullopt;
  }

  return ScenePixelRect{
      .left = *mapped_left,
      .top = *mapped_top,
      .right = *mapped_right,
      .bottom = *mapped_bottom,
  };
}

inline std::optional<ScenePixelViewport> MapScenePixelViewport(
    const SceneSurfaceMapping& mapping,
    ScenePixelViewport vp) {
  if (!ValidSceneSurfaceMapping(mapping)) {
    return std::nullopt;
  }

  if (!std::isfinite(vp.x) || !std::isfinite(vp.y) ||
      !std::isfinite(vp.width) || !std::isfinite(vp.height) ||
      !std::isfinite(vp.min_depth) || !std::isfinite(vp.max_depth)) {
    return std::nullopt;
  }

  if (vp.width <= 0.0f || vp.height <= 0.0f) {
    return std::nullopt;
  }

  if (vp.min_depth < 0.0f || vp.min_depth > vp.max_depth || vp.max_depth > 1.0f) {
    return std::nullopt;
  }

  const double guest_w_d = static_cast<double>(mapping.guest.width);
  const double host_w_d = static_cast<double>(mapping.host.width);
  const double guest_h_d = static_cast<double>(mapping.guest.height);
  const double host_h_d = static_cast<double>(mapping.host.height);

  const float host_x = (vp.x == 0.0f) ? 0.0f : static_cast<float>(static_cast<double>(vp.x) * host_w_d / guest_w_d);
  const float host_y = (vp.y == 0.0f) ? 0.0f : static_cast<float>(static_cast<double>(vp.y) * host_h_d / guest_h_d);

  const float host_width = (vp.width == static_cast<float>(mapping.guest.width))
      ? static_cast<float>(mapping.host.width)
      : static_cast<float>(static_cast<double>(vp.width) * host_w_d / guest_w_d);

  const float host_height = (vp.height == static_cast<float>(mapping.guest.height))
      ? static_cast<float>(mapping.host.height)
      : static_cast<float>(static_cast<double>(vp.height) * host_h_d / guest_h_d);

  if (!std::isfinite(host_x) || !std::isfinite(host_y) ||
      !std::isfinite(host_width) || !std::isfinite(host_height) ||
      host_width <= 0.0f || host_height <= 0.0f) {
    return std::nullopt;
  }

  return ScenePixelViewport{
      .x = host_x,
      .y = host_y,
      .width = host_width,
      .height = host_height,
      .min_depth = vp.min_depth,
      .max_depth = vp.max_depth,
  };
}

inline bool SameScenePixelScale(
    const SceneSurfaceMapping& a,
    const SceneSurfaceMapping& b) {
  if (!ValidSceneSurfaceMapping(a) || !ValidSceneSurfaceMapping(b)) {
    return false;
  }

  const uint64_t a_host_w = a.host.width;
  const uint64_t a_guest_w = a.guest.width;
  const uint64_t b_host_w = b.host.width;
  const uint64_t b_guest_w = b.guest.width;

  const uint64_t a_host_h = a.host.height;
  const uint64_t a_guest_h = a.guest.height;
  const uint64_t b_host_h = b.host.height;
  const uint64_t b_guest_h = b.guest.height;

  const bool same_x = (a_host_w * b_guest_w == b_host_w * a_guest_w);
  const bool same_y = (a_host_h * b_guest_h == b_host_h * a_guest_h);

  return same_x && same_y;
}

struct ScenePixelCopy {
  ScenePixelRect source;
  int32_t dest_x = 0;
  int32_t dest_y = 0;
};

inline std::optional<ScenePixelCopy> PlanScenePixelCopy(
    const SceneSurfaceMapping& source,
    const SceneSurfaceMapping& destination,
    ScenePixelRect guest_source,
    int32_t guest_dest_x,
    int32_t guest_dest_y) {
  if (!ValidSceneSurfaceMapping(source) || !ValidSceneSurfaceMapping(destination)) {
    return std::nullopt;
  }

  if (!SameScenePixelScale(source, destination)) {
    return std::nullopt;
  }

  if (guest_source.left >= guest_source.right || guest_source.top >= guest_source.bottom) {
    return std::nullopt;
  }

  if (guest_source.left < 0 || guest_source.top < 0) {
    return std::nullopt;
  }
  if (guest_source.right > static_cast<int32_t>(source.guest.width) ||
      guest_source.bottom > static_cast<int32_t>(source.guest.height)) {
    return std::nullopt;
  }

  const int64_t guest_width = static_cast<int64_t>(guest_source.right) - static_cast<int64_t>(guest_source.left);
  const int64_t guest_height = static_cast<int64_t>(guest_source.bottom) - static_cast<int64_t>(guest_source.top);

  if (guest_dest_x < 0 || guest_dest_y < 0) {
    return std::nullopt;
  }
  const int64_t guest_dest_right = static_cast<int64_t>(guest_dest_x) + guest_width;
  const int64_t guest_dest_bottom = static_cast<int64_t>(guest_dest_y) + guest_height;

  if (guest_dest_right > static_cast<int64_t>(destination.guest.width) ||
      guest_dest_bottom > static_cast<int64_t>(destination.guest.height)) {
    return std::nullopt;
  }

  if (guest_dest_right > static_cast<int64_t>(std::numeric_limits<int32_t>::max()) ||
      guest_dest_bottom > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
    return std::nullopt;
  }

  const auto host_src_left = MapScenePixelEdge(guest_source.left, source.guest.width, source.host.width, SceneEdgeRounding::Nearest);
  const auto host_src_right = MapScenePixelEdge(guest_source.right, source.guest.width, source.host.width, SceneEdgeRounding::Nearest);
  const auto host_src_top = MapScenePixelEdge(guest_source.top, source.guest.height, source.host.height, SceneEdgeRounding::Nearest);
  const auto host_src_bottom = MapScenePixelEdge(guest_source.bottom, source.guest.height, source.host.height, SceneEdgeRounding::Nearest);

  if (!host_src_left || !host_src_right || !host_src_top || !host_src_bottom) {
    return std::nullopt;
  }

  const auto host_dest_left = MapScenePixelEdge(guest_dest_x, destination.guest.width, destination.host.width, SceneEdgeRounding::Nearest);
  const auto host_dest_right = MapScenePixelEdge(static_cast<int32_t>(guest_dest_right), destination.guest.width, destination.host.width, SceneEdgeRounding::Nearest);
  const auto host_dest_top = MapScenePixelEdge(guest_dest_y, destination.guest.height, destination.host.height, SceneEdgeRounding::Nearest);
  const auto host_dest_bottom = MapScenePixelEdge(static_cast<int32_t>(guest_dest_bottom), destination.guest.height, destination.host.height, SceneEdgeRounding::Nearest);

  if (!host_dest_left || !host_dest_right || !host_dest_top || !host_dest_bottom) {
    return std::nullopt;
  }

  if (*host_src_left >= *host_src_right || *host_src_top >= *host_src_bottom) {
    return std::nullopt;
  }

  if (*host_dest_left >= *host_dest_right || *host_dest_top >= *host_dest_bottom) {
    return std::nullopt;
  }

  if (*host_src_left < 0 || *host_src_top < 0 ||
      *host_src_right > static_cast<int32_t>(source.host.width) ||
      *host_src_bottom > static_cast<int32_t>(source.host.height)) {
    return std::nullopt;
  }

  if (*host_dest_left < 0 || *host_dest_top < 0 ||
      *host_dest_right > static_cast<int32_t>(destination.host.width) ||
      *host_dest_bottom > static_cast<int32_t>(destination.host.height)) {
    return std::nullopt;
  }

  const int32_t src_host_width = *host_src_right - *host_src_left;
  const int32_t src_host_height = *host_src_bottom - *host_src_top;
  const int32_t dest_host_width = *host_dest_right - *host_dest_left;
  const int32_t dest_host_height = *host_dest_bottom - *host_dest_top;

  if (src_host_width != dest_host_width || src_host_height != dest_host_height) {
    return std::nullopt;
  }

  ScenePixelCopy plan;
  plan.source = ScenePixelRect{
      .left = *host_src_left,
      .top = *host_src_top,
      .right = *host_src_right,
      .bottom = *host_src_bottom,
  };
  plan.dest_x = *host_dest_left;
  plan.dest_y = *host_dest_top;
  return plan;
}

}  // namespace superman_returns::graphics
