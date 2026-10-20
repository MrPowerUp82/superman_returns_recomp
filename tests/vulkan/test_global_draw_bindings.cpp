#include "global_draw_bindings.h"
#include "test_main.h"
using namespace superman_returns::graphics::vulkan;
namespace guest=superman_returns::graphics::guest;
SR_TEST(global_layout_separates_attribute_offsets_from_stream_state) {
  guest::DrawPacket draw;guest::VertexStream stream{};stream.stream=7;stream.offset=12;stream.stride=4;draw.streams.push_back(stream);
  draw.attributes.push_back({7,2,0x2c235f,13,15});
  GlobalVertexLayout out;Error e;SR_CHECK(PlanGlobalVertexLayout(draw,out,e));
  SR_CHECK_EQ(out.attributes[223][0],7u);SR_CHECK_EQ(out.attributes[223][1],2u);SR_CHECK_EQ(out.attributes[223][3],0x2c235fu);
  SR_CHECK_EQ(out.streams_used,1u<<7);SR_CHECK_EQ(out.attributes[0][0],32u);
  std::vector<uint32_t> key1,key2;SR_CHECK(GlobalVertexLayoutKey(draw,key1,e));
  draw.streams[0].offset=120;draw.streams[0].stride=48;SR_CHECK(GlobalVertexLayoutKey(draw,key2,e));SR_CHECK(key1==key2);
  draw.attributes[0].type=0x2c23a5;SR_CHECK(GlobalVertexLayoutKey(draw,key2,e));SR_CHECK(key1!=key2);
}
SR_TEST(global_layout_fallback_and_inline_preserve_legacy_fetch_addresses) {
  guest::DrawPacket draw;guest::VertexStream stream{};stream.stream=7;stream.offset=12;stream.stride=4;draw.streams.push_back(stream);
  draw.vertex_fetch[223]={7,14,4,0x2c235f};GlobalVertexLayout out;Error e;
  SR_CHECK(PlanGlobalVertexLayout(draw,out,e));SR_CHECK_EQ(out.attributes[223][1],2u);
  draw.inline_vertices=true;draw.vertex_fetch[223].offset=2;
  SR_CHECK(PlanGlobalVertexLayout(draw,out,e));SR_CHECK_EQ(out.attributes[223][0],0u);SR_CHECK_EQ(out.attributes[223][1],2u);SR_CHECK_EQ(out.streams_used,1u);
  draw.inline_vertices=false;draw.streams.clear();SR_CHECK(!PlanGlobalVertexLayout(draw,out,e));
}
