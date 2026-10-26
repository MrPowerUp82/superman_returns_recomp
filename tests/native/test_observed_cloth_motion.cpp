#include "test_main.h"
#include "../../port/src/graphics/observed_cloth_motion.h"
#include <cstring>
#include <bit>
#include <limits>
using namespace superman_returns::graphics;
namespace {
struct Fixture {
  guest::WorkBatch batch;
  guest::WorkCmd cmd;
  std::array<TemporalFetchResource,4> resources{};
  TemporalFetchResource index{0,100,8,4};
  uint32_t metadata[224][4]{};
  Fixture() {
    cmd.observed_render.token={0x20000,7};
    cmd.observed_render.render=0x82301510;cmd.observed_render.vtable=0x82021DA8;
    auto& c=cmd.observed_render.cloth;
    c.topology_generation=11;c.vertices=4;c.quads=1;c.ring=0;
    c.caller=0x822F43C0;c.source_route_observed=true;
    cmd.u[0]=13;cmd.u[3]=4;cmd.u[5]=6;cmd.has_index=true;
    cmd.index={};cmd.index.key=100;cmd.index.address=0x90000;
    cmd.index.size=cmd.index_size=8;c.indices={0x80000,0x90000,8};
    cmd.stream_count=4;
    for(uint32_t role=0;role<4;++role) {
      const uint32_t stride=role==2?8:16,address=0x30000+role*0x1000;
      c.streams[role]={0x70000+role*0x100,address,4*stride};
      guest::StreamPlan s;s.stream=role;s.stride=stride;s.size=4*stride;
      s.buffer.key=1000+role;s.buffer.address=address;s.buffer.size=s.size;
      batch.streams.push_back(s);resources[role]={10+role,1000+role,2,3};
      metadata[role][0]=10+role;metadata[role][2]=stride;
    }
    const uint32_t triangles[]{0,1,2,0,2,3};
    batch.bytes.resize(sizeof(triangles));std::memcpy(batch.bytes.data(),triangles,sizeof(triangles));
  }
  bool Capture(ObservedClothMotionIdentity& out) {
    return CaptureObservedClothMotionIdentity(0xA5232A3184AA617Cull,batch,cmd,resources,index,metadata,out);
  }
};
}
SR_TEST(cloth_motion_ring_rotation_preserves_slots_but_retains_actual_versions) {
  Fixture f;ObservedClothMotionIdentity first,second;
  if(!f.Capture(first)) {SR_CHECK(false);return;}
  f.cmd.observed_render.cloth.ring=2;
  for(uint32_t role:{0u,1u,3u}) {
    f.cmd.observed_render.cloth.streams[role].physical+=0x10000;
    f.batch.streams[role].buffer.address+=0x10000;
    f.batch.streams[role].buffer.key+=100;
    f.resources[role].key+=100;++f.resources[role].generation;++f.resources[role].content_version;
  }
  SR_CHECK(f.Capture(second));
  SR_CHECK(first.mesh_streams==second.mesh_streams && first.fetch==second.fetch);
  SR_CHECK(first.instance_words==second.instance_words && first.generation==second.generation);
  SR_CHECK(first.actual_streams!=second.actual_streams);
  ++f.resources[2].content_version;SR_CHECK(f.Capture(second));
  SR_CHECK(first.mesh_streams!=second.mesh_streams && first.fetch!=second.fetch);
  ++f.index.content_version;SR_CHECK(f.Capture(second));SR_CHECK(first.index!=second.index);
}
SR_TEST(cloth_motion_requires_source_route_and_exact_captured_bindings) {
  for(unsigned fault=0;fault<27;++fault) {
    Fixture f;ObservedClothMotionIdentity out;out.generation=999;const auto sentinel=out;
    auto& c=f.cmd.observed_render.cloth;
    switch(fault) {
      case 0:f.cmd.observed_render.token.generation=0;break;
      case 1:f.cmd.observed_render.token.address=0xFFFF;break;
      case 2:f.cmd.observed_render.render^=4;break;
      case 3:f.cmd.observed_render.vtable^=4;break;
      case 4:c.source_route_observed=false;break;
      case 5:c.topology_generation=0;break;
      case 6:c.caller^=4;break;
      case 7:c.ring=3;break;
      case 8:c.vertices=0;break;
      case 9:c.quads=UINT32_MAX;break;
      case 10:f.cmd.u[0]=4;break;
      case 11:f.cmd.u[1]=1;break;
      case 12:f.cmd.u[2]=1;break;
      case 13:++f.cmd.u[3];break;
      case 14:++f.cmd.u[5];break;
      case 15:f.cmd.u[4]=UINT32_MAX;break;
      case 16:f.cmd.index32=true;break;
      case 17:++f.cmd.index.address;break;
      case 18:--f.cmd.index_size;break;
      case 19:f.resources[0].content_version=0;break;
      case 20:f.index.generation=0;break;
      case 21:f.batch.streams[0].stream=1;break;
      case 22:++f.batch.streams[0].buffer.address;break;
      case 23:f.batch.streams[0].offset=1;break;
      case 24:++f.batch.streams[2].stride;break;
      case 25:f.metadata[0][0]=99;break;
      case 26:f.batch.bytes[0]=4;break;
    }
    SR_CHECK(!f.Capture(out));SR_CHECK(out==sentinel);
  }
}
SR_TEST(cloth_motion_rejects_wrong_shader_index_expansion_and_rebuilds_identity) {
  Fixture f;ObservedClothMotionIdentity a,b;
  SR_CHECK(!CaptureObservedClothMotionIdentity(0,f.batch,f.cmd,f.resources,f.index,f.metadata,a));
  SR_CHECK(f.Capture(a));++f.cmd.observed_render.cloth.topology_generation;
  SR_CHECK(f.Capture(b));SR_CHECK(a.instance_words!=b.instance_words);
  f.batch.bytes[12]=1;SR_CHECK(!f.Capture(b)); // Second triangle must reuse quad vertex a.
}

SR_TEST(cloth_motion_shader_inputs_use_c7_projection_and_current_uploaded_deformation) {
  std::array<uint32_t,1024> f{};std::array<uint32_t,4> b{1,2,3,4};
  for(size_t i=0;i<f.size();++i) f[i]=std::bit_cast<uint32_t>(float(i));
  ObservedClothShaderInputs out;
  SR_CHECK(CaptureObservedClothShaderInputs(0xA5232A3184AA617Cull,f,b,out));
  SR_CHECK(std::equal(f.begin()+28,f.begin()+44,out.clip_rows.begin()));
  SR_CHECK(std::equal(f.begin(),f.end(),out.position_words.begin()));
  SR_CHECK(std::equal(b.begin(),b.end(),out.position_words.begin()+1024));
  const auto sentinel=out;
  f[12]=std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN());
  SR_CHECK(!CaptureObservedClothShaderInputs(0xA5232A3184AA617Cull,f,b,out));SR_CHECK(out==sentinel);
  f[12]=0;SR_CHECK(!CaptureObservedClothShaderInputs(0,f,b,out));SR_CHECK(out==sentinel);
}

SR_TEST(cloth_motion_rejects_actual_descriptor_alias_before_role_normalization) {
  Fixture f;ObservedClothMotionIdentity out;
  // A static resource can numerically equal a local canonical role identity.
  // That must not hide two different REAL resources using one descriptor.
  f.resources[2]={f.resources[0].descriptor,1,11,1};
  f.batch.streams[2].buffer.key=1;f.metadata[2][0]=f.metadata[0][0];
  SR_CHECK(!f.Capture(out));
}

SR_TEST(cloth_motion_accepts_static_ia_without_unused_raw_buffer_descriptors) {
  Fixture f;ObservedClothMotionIdentity out;
  // BindVertexStreams allocates raw SRVs only for shader-driven vertex fetch.
  // The original cape uses static IA; its resident versions remain required.
  for(uint32_t role=0;role<4;++role) {f.resources[role].descriptor=0;f.metadata[role][0]=0;}
  SR_CHECK(f.Capture(out));
  f.resources[1].generation=0;SR_CHECK(!f.Capture(out));
}
