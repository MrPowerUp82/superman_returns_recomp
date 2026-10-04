#include "../../port/src/graphics/guest/render_state.h"
#include "test_main.h"
#include "../../port/src/graphics/guest/render_packet.h"
#include <bit>
using namespace superman_returns::graphics::guest;
SR_TEST(polygon_offset_matches_reference_float_rounding) {
  DrawPacket d;d.registers[0x205]=1<<11;
  d.mirrored_registers[0x380]=std::bit_cast<uint32_t>(32.0f);
  d.mirrored_registers[0x381]=std::bit_cast<uint32_t>(1.0f/16777215.0f);
  FinalizeDrawConstants(d);SR_CHECK_EQ(d.depth_bias,1);SR_CHECK_EQ(d.slope_bias,2.0f);
}
SR_TEST(viewport_normalization_preserves_reference_policy) {
  auto viewport =
      NormalizeViewport({4, 5, 0, 9000, -1, 2}, {1280, 720}, std::nullopt, 1);
  SR_CHECK_EQ(viewport.x, 0);
  SR_CHECK_EQ(viewport.width, 1280);
  SR_CHECK_EQ(viewport.min_depth, 0);
  SR_CHECK_EQ(viewport.max_depth, 1);
  viewport = NormalizeViewport({4, 5, 300, 200, 0, 1}, {1280, 720},
                               Extent{640, 360}, 2);
  SR_CHECK_EQ(viewport.x, 0);
  SR_CHECK_EQ(viewport.width, 1280);
  SR_CHECK_EQ(viewport.height, 720);
}
SR_TEST(scissor_offsets_and_screen_clamp_match_reference) {
  // Window 2,3..12,13 with signed offset -2,-1, screen 3,4..9,10.
  auto rect = DecodeScissor({2u | (3u << 16), 12u | (13u << 16),
                             0x7ffeu | (0x7fffu << 16), 3u | (4u << 16),
                             9u | (10u << 16)},
                            true, false, 1);
  SR_CHECK_EQ(rect.left, 3);
  SR_CHECK_EQ(rect.top, 4);
  SR_CHECK_EQ(rect.right, 9);
  SR_CHECK_EQ(rect.bottom, 10);
  rect = DecodeScissor({}, true, true, 2);
  SR_CHECK_EQ(rect.right, 16384);
}
SR_TEST(surface_geometry_preserves_guest_footprint) {
  auto geometry = DecodeSurfaceGeometry(1280, (3u << 16) | 7,
                                        (1279u << 18) | (719u << 3), false);
  SR_CHECK_EQ(geometry.width, 1280);
  SR_CHECK_EQ(geometry.height, 720);
  SR_CHECK_EQ(geometry.edram_base, 7);
  SR_CHECK_EQ(geometry.format, 3);
  SR_CHECK_EQ(geometry.edram_tiles, 16u * 45u);
}
SR_TEST(guest_primitive_mapping_rejects_unknown_topology) {
  Primitive primitive;
  std::string error;
  SR_CHECK(DecodePrimitive(0x0d, primitive, error));
  SR_CHECK(primitive == Primitive::kQuads);
  SR_CHECK(DecodePrimitive(0x08, primitive, error));
  SR_CHECK(primitive == Primitive::kRectangles);
  SR_CHECK(!DecodePrimitive(UINT32_MAX, primitive, error));
}
