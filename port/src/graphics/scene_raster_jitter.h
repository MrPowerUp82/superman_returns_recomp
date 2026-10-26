#pragma once
#include <cstdint>

namespace superman_returns::graphics {

struct SceneRasterJitter {
    float pixel_x = 0.0f, pixel_y = 0.0f, ndc_x = 0.0f, ndc_y = 0.0f;
    bool operator==(const SceneRasterJitter&) const = default;
};

namespace scene_raster_jitter_detail {
inline float Halton(uint32_t i, uint32_t b) {
    float f = 1.0f, r = 0.0f;
    while (i > 0) { f /= static_cast<float>(b); r += f * static_cast<float>(i % b); i /= b; }
    return r;
}

} // namespace scene_raster_jitter_detail

// Pixel units are right/down; the NDC delta is appended to the original
// shared half-pixel fixup. This helper proves no scene/camera/SDK readiness.
inline bool MakeSceneRasterJitter(uint64_t index, uint32_t width, uint32_t height,
                                  bool enabled, SceneRasterJitter& output) {
    if (width < 1 || width > 8192 || height < 1 || height > 8192 ||
        static_cast<uint64_t>(width) * height > 16ULL * 1024 * 1024) {
        return false;
    }
    if (!enabled) {
        output = {0.0f, 0.0f, 0.0f, 0.0f};
        return true;
    }
    const uint32_t n = static_cast<uint32_t>(index % 8ULL) + 1U;
    const float px = scene_raster_jitter_detail::Halton(n, 2) - 0.5f;
    const float py = scene_raster_jitter_detail::Halton(n, 3) - 0.5f;
    output = {px, py, (2.0f * px) / static_cast<float>(width), (-2.0f * py) / static_cast<float>(height)};
    return true;
}

} // namespace superman_returns::graphics
