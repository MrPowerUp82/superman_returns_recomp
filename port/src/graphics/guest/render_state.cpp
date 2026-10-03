#include "render_state.h"
#include <algorithm>
#include <cmath>
namespace superman_returns::graphics::guest {
Viewport NormalizeViewport(Viewport v, Extent fallback,
                           std::optional<Extent> screen_space, float scale) {
  if (v.width <= 0 || v.height <= 0 || v.width > 8192 || v.height > 8192) {
    v.x = v.y = 0;
    v.width = float(fallback.width);
    v.height = float(fallback.height);
  }
  v.min_depth = std::clamp(v.min_depth, 0.0f, 1.0f);
  v.max_depth = std::clamp(v.max_depth, 0.0f, 1.0f);
  if (screen_space) {
    v.x = v.y = 0;
    v.width = float(screen_space->width);
    v.height = float(screen_space->height);
  }
  v.x *= scale;
  v.y *= scale;
  v.width *= scale;
  v.height *= scale;
  return v;
}
Rect DecodeScissor(ScissorRegisters r, bool enabled, bool tiling, float scale) {
  if (!enabled || tiling || !r.window_br)
    return {0, 0, 16384, 16384};
  int32_t x0 = r.window_tl & 0x7fff, y0 = (r.window_tl >> 16) & 0x7fff;
  int32_t x1 = r.window_br & 0x7fff, y1 = (r.window_br >> 16) & 0x7fff;
  if (!(r.window_tl >> 31)) {
    auto signed15 = [](uint32_t v) {
      int32_t n = int32_t(v & 0x7fff);
      return n & 0x4000 ? n - 0x8000 : n;
    };
    auto ox = signed15(r.window_offset), oy = signed15(r.window_offset >> 16);
    x0 += ox;
    x1 += ox;
    y0 += oy;
    y1 += oy;
  }
  if (r.screen_br) {
    x0 = std::max(x0, int32_t(r.screen_tl & 0x7fff));
    y0 = std::max(y0, int32_t((r.screen_tl >> 16) & 0x7fff));
    x1 = std::min(x1, int32_t(r.screen_br & 0x7fff));
    y1 = std::min(y1, int32_t((r.screen_br >> 16) & 0x7fff));
  }
  x0 = std::max(x0, 0);
  y0 = std::max(y0, 0);
  x1 = std::max(x1, x0);
  y1 = std::max(y1, y0);
  auto scaled = [scale](int32_t v) {
    return int32_t(std::lround(double(v) * double(scale)));
  };
  return {scaled(x0), scaled(y0), scaled(x1), scaled(y1)};
}
SurfaceGeometry DecodeSurfaceGeometry(uint32_t surface_info,
                                      uint32_t color_depth_info,
                                      uint32_t size_bits, bool depth) {
  uint32_t width = (size_bits >> 18) + 1,
           height = ((size_bits >> 3) & 0x7fff) + 1;
  uint32_t format = (color_depth_info >> 16) & 15,
           msaa = (surface_info >> 16) & 3;
  uint32_t pitch = surface_info & 0x3fff;
  if (!pitch)
    pitch = width;
  uint32_t tile_width =
      (!depth && (format == 5 || format == 7 || format == 15)) ? 40 : 80;
  uint32_t tiles =
      ((pitch * (msaa >= 2 ? 2 : 1) + tile_width - 1) / tile_width) *
      ((height * (msaa >= 1 ? 2 : 1) + 15) / 16);
  return {width, height, color_depth_info & 0xfff, format, msaa, tiles};
}
bool DecodePrimitive(uint32_t xenos, Primitive &out, std::string &error) {
  error.clear();
  switch (xenos) {
  case 1:
    out = Primitive::kPoints;
    break;
  case 2:
    out = Primitive::kLines;
    break;
  case 3:
    out = Primitive::kLineStrip;
    break;
  case 4:
    out = Primitive::kTriangles;
    break;
  case 6:
    out = Primitive::kTriangleStrip;
    break;
  case 8:
    out = Primitive::kRectangles;
    break;
  case 13:
    out = Primitive::kQuads;
    break;
  default:
    error = "Unsupported Xenos primitive " + std::to_string(xenos);
    return false;
  }
  return true;
}
} // namespace superman_returns::graphics::guest
