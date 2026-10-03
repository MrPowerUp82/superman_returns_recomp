#pragma once
#include "draw_state.h"
#include <optional>
namespace superman_returns::graphics::guest {
struct Extent {
  float width, height;
};
struct Viewport {
  float x = 0, y = 0, width = 0, height = 0, min_depth = 0, max_depth = 1;
};
struct Rect {
  int32_t left = 0, top = 0, right = 0, bottom = 0;
};
struct ScissorRegisters {
  uint32_t window_tl, window_br, window_offset, screen_tl, screen_br;
};
struct SurfaceGeometry {
  uint32_t width, height, edram_base, format, guest_msaa, edram_tiles;
};
Viewport NormalizeViewport(Viewport, Extent, std::optional<Extent> screen_space,
                           float scale);
Rect DecodeScissor(ScissorRegisters, bool enabled, bool tiling, float scale);
SurfaceGeometry DecodeSurfaceGeometry(uint32_t surface_info,
                                      uint32_t color_depth_info,
                                      uint32_t size_bits, bool depth);
bool DecodePrimitive(uint32_t xenos, Primitive &, std::string &);
} // namespace superman_returns::graphics::guest
