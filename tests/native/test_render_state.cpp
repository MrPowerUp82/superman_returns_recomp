#include "../../port/src/graphics/guest/render_state.h"
#include "test_main.h"
#include "../../port/src/graphics/guest/render_packet.h"
#include <bit>
#include <limits>
using namespace superman_returns::graphics::guest;
SR_TEST(polygon_offset_matches_reference_float_rounding) {
  DrawPacket d;d.registers[0x205]=1<<11;
  d.polygon_offset_registers[0]=std::bit_cast<uint32_t>(32.0f);
  d.polygon_offset_registers[1]=std::bit_cast<uint32_t>(1.0f/16777215.0f);
  FinalizeDrawConstants(d);SR_CHECK_EQ(d.depth_bias,1);SR_CHECK_EQ(d.slope_bias,2.0f);
}
SR_TEST(polygon_offset_keeps_culling_primitive_fallback_and_nonfinite_rules) {
  auto check=[](Primitive primitive,uint32_t mode,std::array<float,4> values,int32_t bias,float slope) {
    DrawPacket d;d.primitive=primitive;d.registers[0x205]=mode;
    for(uint32_t i=0;i<4;++i) d.polygon_offset_registers[i]=std::bit_cast<uint32_t>(values[i]);
    FinalizeDrawConstants(d);SR_CHECK_EQ(d.depth_bias,bias);SR_CHECK_EQ(d.slope_bias,slope);
  };
  const float unit=1.0f/16777215.0f;
  const std::array<float,4> values{32,unit,64,-2*unit};
  for(auto primitive:{Primitive::kTriangles,Primitive::kTriangleStrip,Primitive::kQuads,Primitive::kRectangles}) {
    check(primitive,1<<11,values,1,2);
    check(primitive,1<<12,values,-2,4);
    check(primitive,(1<<11)|1,values,0,0);
    check(primitive,(1<<12)|2,values,0,0);
    check(primitive,(1<<11)|(1<<12),values,1,2);
    check(primitive,(1<<11)|(1<<12),{0,0,64,-2*unit},-2,4);
  }
  for(auto primitive:{Primitive::kPoints,Primitive::kLines,Primitive::kLineStrip}) {
    check(primitive,1<<13,values,1,2);check(primitive,1<<11,values,0,0);
  }
  check(Primitive::kTriangles,1<<11,{std::numeric_limits<float>::quiet_NaN(),unit,0,0},1,0);
  check(Primitive::kTriangles,1<<11,{32,std::numeric_limits<float>::infinity(),0,0},0,2);
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
