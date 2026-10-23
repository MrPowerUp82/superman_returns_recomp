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

SR_TEST(global_layout_fallback_indexes_all_streams_without_changing_offsets) {
  guest::DrawPacket draw;
  for(uint32_t i=32;i-->0;) {
    guest::VertexStream stream{};stream.stream=i;stream.offset=i*16;stream.stride=16;
    draw.streams.push_back(stream);
  }
  for(uint32_t i=0;i<224;++i) {
    const uint32_t stream=i%32;
    draw.vertex_fetch[i]={stream,stream*16+3,4,0x2c235fu};
  }
  GlobalVertexLayout out;Error e;
  SR_CHECK(PlanGlobalVertexLayout(draw,out,e));
  SR_CHECK_EQ(out.streams_used,0xffffffffu);
  for(uint32_t i=0;i<224;++i) {
    SR_CHECK_EQ(out.attributes[i][0],i%32);
    SR_CHECK_EQ(out.attributes[i][1],3u);
  }
  std::vector<uint32_t> before,after;
  SR_CHECK(GlobalVertexLayoutKey(draw,before,e));
  // Legacy behavior: the first stream with a given ID determines its base.
  guest::VertexStream duplicate{};duplicate.stream=5;duplicate.offset=500;
  draw.streams.push_back(duplicate);
  SR_CHECK(PlanGlobalVertexLayout(draw,out,e));
  SR_CHECK_EQ(out.attributes[5][1],3u);
  SR_CHECK(GlobalVertexLayoutKey(draw,after,e));
  SR_CHECK(before==after);

  // A missing source stream must still fail rather than emit a bad offset.
  guest::DrawPacket missing;
  missing.vertex_fetch[0]={31,4,4,0x2c235fu};
  SR_CHECK(!PlanGlobalVertexLayout(missing,out,e));
}
