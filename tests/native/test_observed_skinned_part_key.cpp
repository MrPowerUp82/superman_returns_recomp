#include "test_main.h"
#include "../../port/src/graphics/observed_skinned_part_key.h"
using namespace superman_returns::graphics;
namespace {
ObservedRenderInvocation ValidPart() {
  ObservedRenderInvocation i;i.token={0x10000,9};i.render=0x82300328;i.vtable=0x82021D18;
  i.part.model=0x20000;i.part.sections=0x30000;i.part.origin=1;
  i.part.group=2;i.part.stream_offset=0x80;i.part.submesh=3;i.part.source_route_observed=true;
  return i;
}
}
SR_TEST(observed_part_key_rejects_unobserved_or_malformed_routes_atomically) {
  ObservedSkinnedPartKey out;out.generation=99;const auto sentinel=out;
  for(unsigned fault=0;fault<13;++fault) {
    auto i=ValidPart();
    switch(fault) {
      case 0:i.part.source_route_observed=false;break;case 1:i.token.generation=0;break;
      case 2:i.token.address=0;break;case 3:++i.token.address;break;
      case 4:i.render=0x8230F8E0;break;case 5:i.vtable=0x820211C0;break;
      case 6:i.part.model=0;break;case 7:i.part.sections=0;break;
      case 8:i.part.origin=0;break;case 9:i.part.stream_offset=0x20000;break;
      case 10:i.part.submesh=0x10000;break;case 11:i.part.attachment=1;break;
      case 12:i.part.origin=2;i.part.attachment=0xFFFF;break;
    }
    SR_CHECK(!CaptureObservedSkinnedPartKey(i,out));SR_CHECK(out==sentinel);
  }
  SR_CHECK(CaptureObservedSkinnedPartKey(ValidPart(),out));
}
SR_TEST(observed_part_key_retains_asset_role_lod_slot_and_stream_selections) {
  ObservedSkinnedPartKey first,next;
  if(!CaptureObservedSkinnedPartKey(ValidPart(),first)) {SR_CHECK(false);return;}
  for(unsigned component=0;component<8;++component) {
    auto i=ValidPart();
    switch(component) {
      case 0:i.token.address+=16;break;case 1:i.part.model+=16;break;
      case 2:i.part.sections+=16;break;case 3:i.part.origin=2;i.part.attachment=0;break;
      case 4:++i.part.lod_slice;break;case 5:++i.part.group;break;
      case 6:i.part.stream_offset+=2;break;case 7:++i.part.submesh;break;
    }
    SR_CHECK(CaptureObservedSkinnedPartKey(i,next));SR_CHECK(first!=next);
  }
  auto i=ValidPart();i.part.origin=2;i.part.attachment=7;
  SR_CHECK(CaptureObservedSkinnedPartKey(i,first));++i.part.attachment;
  SR_CHECK(CaptureObservedSkinnedPartKey(i,next));SR_CHECK(first!=next);
}
SR_TEST(observed_part_key_ignores_ephemeral_packets_and_invalidates_generation_reuse) {
  auto i=ValidPart();ObservedSkinnedPartKey first,next;
  if(!CaptureObservedSkinnedPartKey(i,first)) {SR_CHECK(false);return;}
  i.packet=0x90000;i.packet_word16=5;i.part.actor_palette=0xA0000;
  SR_CHECK(CaptureObservedSkinnedPartKey(i,next));SR_CHECK(first==next);
  ++i.token.generation;SR_CHECK(CaptureObservedSkinnedPartKey(i,next));
  SR_CHECK(first.instance_words==next.instance_words);SR_CHECK(first.generation!=next.generation);
}

SR_TEST(observed_body_part_uses_audited_array_slot_and_rejects_cross_class_routes) {
  auto i=ValidPart();i.render=0x82306BB0;i.vtable=0x820217E0;i.part.origin=3;
  i.part.group=4;i.part.submesh=4;i.part.stream_offset=16;
  ObservedSkinnedPartKey out;
  SR_CHECK(CaptureObservedSkinnedPartKey(i,out));
  auto bad=i;bad.render=0x82300328;bad.vtable=0x82021D18;
  SR_CHECK(!CaptureObservedSkinnedPartKey(bad,out));
  bad=i;bad.part.origin=2;SR_CHECK(!CaptureObservedSkinnedPartKey(bad,out));
  bad=i;bad.part.attachment=1;SR_CHECK(!CaptureObservedSkinnedPartKey(bad,out));
  bad=i;++bad.part.submesh;SR_CHECK(!CaptureObservedSkinnedPartKey(bad,out));
  bad=i;bad.part.stream_offset=15;SR_CHECK(!CaptureObservedSkinnedPartKey(bad,out));
  bad=i;bad.part.lod_slice=1;SR_CHECK(!CaptureObservedSkinnedPartKey(bad,out));
}
