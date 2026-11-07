#include "test_main.h"
#include "../../port/src/graphics/observed_rigid_motion.h"
#include <limits>
using namespace superman_returns::graphics;
namespace {
bool SamePart(const ObservedPartSelection& a,const ObservedPartSelection& b) {
  return a.model==b.model && a.sections==b.sections && a.origin==b.origin && a.lod_slice==b.lod_slice &&
      a.actor_palette==b.actor_palette && a.group==b.group && a.attachment==b.attachment &&
      a.stream_offset==b.stream_offset && a.submesh==b.submesh && a.source_route_observed==b.source_route_observed;
}
ObservedRigidPartRoute Route(bool derived=false) {
  ObservedRigidPartRoute r;
  r.token={0x20000,7};r.register_owner=r.token.address;
  r.render=derived?0x823103C8:0x8230F8E0;r.vtable=derived?0x82021930:0x820211C0;
  r.caller=derived?0x82310D50:0x823102B0;
  r.owner_link=0x30000;r.model_link=0x40000;r.root_model=r.saved_model=0x50000;
  r.parts=0x60000;r.count=r.register_count=8;r.slot=3;r.cursor=r.parts+r.slot*4;
  r.packet=r.selected_packet=0x70000;r.array_enabled=true;return r;
}
ObservedRenderInvocation Invocation(bool derived=false) {
  const auto route=Route(derived);
  ObservedRenderInvocation result{route.token,route.render,route.vtable,route.packet};
  CaptureObservedRigidPartSelection(route,result.part);return result;
}
constexpr std::array<uint64_t,7> shaders{0x4F17209D364D5B51ull,0x18B4F69400B7F0AFull,
    0xC5E8551EFDE01162ull,0xC63F6F22679609F0ull,0x7D1AA67BBDC418D1ull,
    0x8E5232F5E5E6760Eull,0x1023A9B52C1407C1ull};
std::array<uint32_t,16> MatrixWords(std::array<float,16> values) {
  std::array<uint32_t,16> result{};
  for(size_t i=0;i<16;++i) result[i]=std::bit_cast<uint32_t>(values[i]);
  return result;
}
ObservedRenderInvocation CommonInvocation(bool derived=false) {
  auto invocation=Invocation(derived);invocation.packet=0;invocation.part={};
  invocation.rigid_owner_world=MatrixWords({1,0,0,0, 0,1,0,0, 0,0,1,0, 7,8,9,1});
  invocation.rigid_owner_world_observed=true;return invocation;
}
std::array<uint32_t,16> CommonCamera() {
  return MatrixWords({2,0,0,10, 0,3,0,20, 0,0,4,30, 0,0,1,1});
}
std::array<uint32_t,1024> CommonFloats(uint64_t shader,const ObservedRenderInvocation& invocation,
                                     const std::array<uint32_t,16>& camera) {
  std::array<uint32_t,1024> floats{};
  const size_t clip=shader==shaders[2]?20:shader==shaders[4]?16:
      (shader==shaders[5] || shader==shaders[6])?233*4:32;
  // Independent double-precision reference for integer-valued test matrices.
  for(size_t row=0;row<4;++row) for(size_t column=0;column<4;++column) {
    double sum=0;
    for(size_t k=0;k<4;++k) sum+=double(std::bit_cast<float>(camera[row*4+k]))*
        double(std::bit_cast<float>(invocation.rigid_owner_world[column*4+k]));
    floats[clip+row*4+column]=std::bit_cast<uint32_t>(float(sum));
  }
  return floats;
}
}
SR_TEST(observed_rigid_routes_require_exact_asset_cursor_and_packet_in_both_scopes) {
  for(bool derived:{false,true}) {
    ObservedPartSelection part;
    SR_CHECK(CaptureObservedRigidPartSelection(Route(derived),part));
    SR_CHECK_EQ(part.origin,derived?5u:4u);
    SR_CHECK_EQ(part.submesh,3u);SR_CHECK_EQ(part.group,3u);SR_CHECK_EQ(part.stream_offset,12u);
    SR_CHECK(part.source_route_observed);
    const auto sentinel=part;
    for(unsigned fault=0;fault<19;++fault) {
      auto r=Route(derived);
      switch(fault) {
        case 0:r.token.generation=0;break;
        case 1:r.token.address=0;break;
        case 2:++r.token.address;break;
        case 3:++r.register_owner;break;
        case 4:++r.render;break;
        case 5:++r.vtable;break;
        case 6:++r.caller;break;
        case 7:r.owner_link=0;break;
        case 8:r.model_link=0;break;
        case 9:++r.saved_model;break;
        case 10:r.array_enabled=false;break;
        case 11:r.count=0;break;
        case 12:++r.register_count;break;
        case 13:r.slot=r.count;break;
        case 14:++r.cursor;break;
        case 15:++r.selected_packet;break;
        case 16:r.packet=r.selected_packet=0;break;
        case 17:r.parts=0xfffffffcu;r.count=r.register_count=8;r.cursor=8;break;
        case 18:++r.parts;break;
      }
      SR_CHECK(!CaptureObservedRigidPartSelection(r,part));SR_CHECK(SamePart(part,sentinel));
    }
  }
}
SR_TEST(observed_single_instance_route_requires_exact_source_stack_and_matrix_copy) {
  ObservedSingleInstancePartRoute route;
  route.part=Route();route.part.caller=0x82310280;
  route.stack=0x80000;route.counts_pointer=route.stack+120;
  route.matrix_array=route.stack+144;route.matrix_pointer=route.stack+256;
  route.count=route.groups=route.mode=route.enabled=1;route.rows=4;
  route.owner_world=route.submitted_world=MatrixWords({1,0,0,0,0,1,0,0,0,0,1,0,7,8,9,1});
  ObservedPartSelection part;
  SR_CHECK(CaptureObservedSingleInstancePartSelection(route,part));
  SR_CHECK(part.origin==8 && part.submesh==route.part.slot);
  const auto sentinel=part;
  for(unsigned fault=0;fault<16;++fault) {
    auto r=route;
    switch(fault) {
      case 0:r.part.caller=0x823102B0;break;
      case 1:r.counts_pointer+=4;break;
      case 2:r.matrix_array+=4;break;
      case 3:r.matrix_pointer+=16;break;
      case 4:r.count=2;break;
      case 5:r.groups=2;break;
      case 6:r.rows=3;break;
      case 7:r.mode=0;break;
      case 8:r.extra=1;break;
      case 9:r.enabled=0;break;
      case 10:r.submitted_world[12]^=1;break;
      case 11:r.part.slot++;break;
      case 12:r.part.register_owner+=4;break;
      case 13:r.part.token.generation=0;break;
      case 14:r.part.selected_packet+=4;break;
      case 15:r.stack=0xffffff00;break;
    }
    SR_CHECK(!CaptureObservedSingleInstancePartSelection(r,part));SR_CHECK(SamePart(part,sentinel));
  }
}
SR_TEST(observed_single_instance_shader_rejects_unknown_palette_slots_and_keeps_owner_lifetime) {
  auto invocation=CommonInvocation();invocation.part=Invocation().part;invocation.part.origin=8;
  invocation.packet=0x70000;
  std::array<uint32_t,1024> floats{};std::array<uint32_t,4> bools{};
  std::copy(invocation.rigid_owner_world.begin(),invocation.rigid_owner_world.end(),floats.begin()+400);
  const auto camera=CommonCamera();std::copy(camera.begin(),camera.end(),floats.begin()+232*4);
  ObservedRigidMotionInputs first;
  for(uint32_t count:{3u,600u,65536u}) {
    floats[16]=std::bit_cast<uint32_t>(float(count));
    SR_CHECK(CaptureObservedSingleInstanceShaderInputs(0xDF1EB50388CC2563ull,invocation,floats,bools,0,count,first));
    SR_CHECK(first.part.instance_words[5]==8 && first.part.generation==invocation.token.generation);
    SR_CHECK(first.actual_bindings_signature==0); // Only worker fetch checks may fill this.
    SR_CHECK(first.clip_rows==camera);
  }
  floats[16]=std::bit_cast<uint32_t>(600.f);
  SR_CHECK(CaptureObservedSingleInstanceShaderInputs(0xDF1EB50388CC2563ull,invocation,floats,bools,0,600,first));
  for(unsigned fault=0;fault<12;++fault) {
    auto owner=invocation;auto values=floats;uint32_t begin=0,end=600;uint64_t shader=0xDF1EB50388CC2563ull;
    switch(fault) {
      case 0:owner.part.origin=4;break;
      case 1:owner.rigid_owner_world_observed=false;break;
      case 2:owner.token.generation=0;break;
      case 3:values[400]^=1;break;
      case 4:values[17]=std::bit_cast<uint32_t>(1.f);break;
      case 5:values[16]=std::bit_cast<uint32_t>(601.f);break;
      case 6:begin=1;break;
      case 7:end=0;break;
      case 8:end=65537;values[16]=std::bit_cast<uint32_t>(float(end));break;
      case 9:shader=0xEA88A5BB6930F942ull;break;
      case 10:values[232*4]=0x7fc00000;break;
      case 11:owner.rigid_owner_world[3]=values[403]=std::bit_cast<uint32_t>(1.f);break;
    }
    ObservedRigidMotionInputs output;
    SR_CHECK(!CaptureObservedSingleInstanceShaderInputs(shader,owner,values,bools,begin,end,output));
  }
  invocation.rigid_owner_world[12]=floats[412]=std::bit_cast<uint32_t>(123.f);
  ObservedRigidMotionInputs moved;
  SR_CHECK(CaptureObservedSingleInstanceShaderInputs(0xDF1EB50388CC2563ull,invocation,floats,bools,0,600,moved));
  SR_CHECK(moved.part==first.part && moved.position_words!=first.position_words);
}
SR_TEST(observed_rigid_identity_tracks_generation_asset_slot_and_scope_not_packet) {
  auto invocation=Invocation();ObservedRigidPartKey original,changed;
  SR_CHECK(CaptureObservedRigidPartKey(invocation,original));
  invocation.packet+=0x100;
  SR_CHECK(CaptureObservedRigidPartKey(invocation,changed));SR_CHECK(changed==original);
  invocation.token.generation++;
  SR_CHECK(CaptureObservedRigidPartKey(invocation,changed));SR_CHECK(changed!=original);
  invocation=Invocation();invocation.part.group=invocation.part.submesh=4;invocation.part.stream_offset=16;
  SR_CHECK(CaptureObservedRigidPartKey(invocation,changed));SR_CHECK(changed!=original);
  SR_CHECK(CaptureObservedRigidPartKey(Invocation(true),changed));SR_CHECK(changed!=original);
  const auto sentinel=changed;
  for(unsigned fault=0;fault<6;++fault) {
    invocation=Invocation();
    switch(fault) {
      case 0:invocation.part.source_route_observed=false;break;
      case 1:invocation.part.origin=3;break;
      case 2:invocation.part.stream_offset++;break;
      case 3:invocation.part.submesh++;break;
      case 4:invocation.part.actor_palette=0x80000;break;
      case 5:invocation.part.lod_slice=1;break;
    }
    SR_CHECK(!CaptureObservedRigidPartKey(invocation,changed));SR_CHECK(changed==sentinel);
  }
}
SR_TEST(observed_rigid_shaders_preserve_current_inputs_without_freezing_world_identity) {
  for(bool derived:{false,true}) for(uint64_t shader:shaders) {
    std::array<uint32_t,1024> floats{};std::array<uint32_t,4> bools{1,2,3,4};
    for(size_t i=0;i<floats.size();++i)floats[i]=std::bit_cast<uint32_t>(float(i));
    ObservedRigidMotionInputs first,second;
    SR_CHECK(CaptureObservedRigidMotionInputs(shader,Invocation(derived),floats,bools,19,first));
    const size_t world=shader==0xC5E8551EFDE01162ull?4:0;
    const size_t clip=shader==0xC5E8551EFDE01162ull?20:shader==0x7D1AA67BBDC418D1ull?16:
        (shader==0x8E5232F5E5E6760Eull || shader==0x1023A9B52C1407C1ull)?233*4:32;
    SR_CHECK(std::equal(floats.begin(),floats.end(),first.position_words.begin()));
    SR_CHECK(std::equal(bools.begin(),bools.end(),first.position_words.begin()+1024));
    SR_CHECK(std::equal(floats.begin()+world,floats.begin()+world+16,first.world_rows.begin()));
    SR_CHECK(std::equal(floats.begin()+clip,floats.begin()+clip+16,first.clip_rows.begin()));
    floats[world]=std::bit_cast<uint32_t>(-40.f);floats[clip]=std::bit_cast<uint32_t>(-80.f);
    bools[0]++;floats[900]=std::bit_cast<uint32_t>(-900.f);
    SR_CHECK(CaptureObservedRigidMotionInputs(shader,Invocation(derived),floats,bools,20,second));
    SR_CHECK(first.part==second.part);SR_CHECK(first.position_words!=second.position_words);
    SR_CHECK(first.world_rows!=second.world_rows);SR_CHECK(first.clip_rows!=second.clip_rows);
    SR_CHECK(first.actual_bindings_signature!=second.actual_bindings_signature);
    const auto sentinel=second;
    SR_CHECK(!CaptureObservedRigidMotionInputs(shader,Invocation(derived),floats,bools,0,second));
    SR_CHECK(!CaptureObservedRigidMotionInputs(0xC20921D0DBBD2F1Full,Invocation(derived),floats,bools,20,second));
    floats[900]=std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN());
    SR_CHECK(!CaptureObservedRigidMotionInputs(shader,Invocation(derived),floats,bools,20,second));
    SR_CHECK(second==sentinel);
  }
}

SR_TEST(observed_rigid_moving_world_retains_correspondence_and_rejects_duplicate_view_conflict) {
  ObservedRigidMotionInputs first,second;std::array<uint32_t,1024> floats{};std::array<uint32_t,4> bools{};
  SR_CHECK(CaptureObservedRigidMotionInputs(shaders[0],Invocation(),floats,bools,19,first));
  floats[0]=std::bit_cast<uint32_t>(2.f);floats[32]=std::bit_cast<uint32_t>(3.f);
  SR_CHECK(CaptureObservedRigidMotionInputs(shaders[0],Invocation(),floats,bools,19,second));
  SR_CHECK(first.part==second.part);SR_CHECK(first.position_words!=second.position_words);
  TemporalGeometryHistory history;TemporalGeometrySample sample;
  sample.mesh=1;sample.camera_domain=2;sample.instance=3;sample.instance_generation=first.part.generation;
  sample.proof=GeometryHistoryProof::kVerifiedInstance;sample.geometry_inputs=4;sample.projection_inputs=5;sample.range={0,0,3};
  history.Begin(1,1280,720,false);history.Add(sample);history.Seal();history.End();
  sample.geometry_inputs=6;sample.projection_inputs=7;sample.range={1,16,3};
  history.Begin(2,1280,720,false);history.Add(sample);history.Seal();
  SR_CHECK(history.Resolve(0).status==GeometryHistoryStatus::kMatch);history.End();
  history.Begin(3,1280,720,false);history.Add(sample);sample.geometry_inputs++;history.Add(sample);history.Seal();
  SR_CHECK(history.Resolve(0).status==GeometryHistoryStatus::kAmbiguous);
}

SR_TEST(observed_rigid_common_uses_transposed_affine_owner_actual_clip_and_separate_domains) {
  const auto camera=CommonCamera();std::array<uint32_t,4> bools{};
  for(bool derived:{false,true}) for(uint64_t shader:shaders) {
    auto invocation=CommonInvocation(derived);auto floats=CommonFloats(shader,invocation,camera);
    ObservedRigidMotionInputs result,asset;
    SR_CHECK(CaptureObservedRigidCommonMotionInputs(shader,invocation,floats,bools,19,camera,true,false,result));
    SR_CHECK_EQ(result.part.instance_words[5],derived?7u:6u);
    SR_CHECK_EQ(std::bit_cast<float>(result.clip_rows[3]),24.f);
    SR_CHECK_EQ(std::bit_cast<float>(result.clip_rows[7]),44.f);
    SR_CHECK_EQ(std::bit_cast<float>(result.clip_rows[11]),66.f);
    SR_CHECK_EQ(std::bit_cast<float>(result.clip_rows[15]),10.f);
    // The lighting-world bank remains all zero: it is not this certificate.
    SR_CHECK(std::all_of(result.world_rows.begin(),result.world_rows.end(),[](uint32_t word){return word==0;}));
    SR_CHECK(CaptureObservedRigidMotionInputs(shader,Invocation(derived),floats,bools,19,asset));
    SR_CHECK(asset.part!=result.part);
    invocation.packet=0xffffffff;invocation.part=Invocation(!derived).part;
    ObservedRigidMotionInputs same;
    SR_CHECK(CaptureObservedRigidCommonMotionInputs(shader,invocation,floats,bools,19,camera,true,false,same));
    SR_CHECK(same.part==result.part); // Neither packet nor guessed asset route is identity.
  }
}

SR_TEST(observed_rigid_common_rejects_missing_provenance_nonaffine_nonfinite_and_one_ulp_clip_error) {
  std::array<uint32_t,4> bools{};ObservedRigidMotionInputs result;
  auto invocation=CommonInvocation();auto camera=CommonCamera();auto floats=CommonFloats(shaders[0],invocation,camera);
  SR_CHECK(CaptureObservedRigidCommonMotionInputs(shaders[0],invocation,floats,bools,19,camera,true,false,result));
  const auto sentinel=result;
  for(unsigned fault=0;fault<20;++fault) {
    invocation=CommonInvocation();camera=CommonCamera();floats=CommonFloats(shaders[0],invocation,camera);
    uint64_t shader=shaders[0],bindings=19;bool valid=true,conflict=false;
    switch(fault) {
      case 0:invocation.token.address=0;break;
      case 1:invocation.token.address++;break;
      case 2:invocation.token.generation=0;break;
      case 3:invocation.render++;break;
      case 4:invocation.vtable++;break;
      case 5:invocation.rigid_owner_world_observed=false;break;
      case 6:valid=false;break;
      case 7:conflict=true;break;
      case 8:invocation.rigid_owner_world[3]=std::bit_cast<uint32_t>(1.f);break;
      case 9:invocation.rigid_owner_world[7]=std::bit_cast<uint32_t>(1.f);break;
      case 10:invocation.rigid_owner_world[11]=std::bit_cast<uint32_t>(1.f);break;
      case 11:invocation.rigid_owner_world[15]=0;break;
      case 12:invocation.rigid_owner_world[0]=std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN());break;
      case 13:camera[0]=std::bit_cast<uint32_t>(std::numeric_limits<float>::infinity());break;
      case 14:floats[32]++;break; // A single representable float step must reject.
      case 15:floats[900]=std::bit_cast<uint32_t>(std::numeric_limits<float>::infinity());break;
      case 16:bindings=0;break;
      case 17:shader=0x2C45BFFE76D77398ull;break;
      case 18:floats[47]=std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN());break;
      case 19:floats[35]=std::bit_cast<uint32_t>(10.f);break; // Raw owner orientation loses translation.
    }
    SR_CHECK(!CaptureObservedRigidCommonMotionInputs(shader,invocation,floats,bools,bindings,camera,valid,conflict,result));
    SR_CHECK(result==sentinel);
  }
}

SR_TEST(observed_rigid_common_normalizes_only_signed_zero) {
  auto invocation=CommonInvocation();auto camera=CommonCamera();auto floats=CommonFloats(shaders[0],invocation,camera);
  invocation.rigid_owner_world[3]=0x80000000u;camera[1]=0x80000000u;floats[33]=0x80000000u;
  std::array<uint32_t,4> bools{};ObservedRigidMotionInputs result;
  SR_CHECK(CaptureObservedRigidCommonMotionInputs(shaders[0],invocation,floats,bools,19,camera,true,false,result));
  floats[33]=1; // The smallest positive subnormal is not another encoding of zero.
  SR_CHECK(!CaptureObservedRigidCommonMotionInputs(shaders[0],invocation,floats,bools,19,camera,true,false,result));
}

SR_TEST(observed_rigid_common_history_rejects_alternating_parts_but_keeps_real_owner_motion) {
  auto invocation=CommonInvocation();auto camera=CommonCamera();auto floats=CommonFloats(shaders[0],invocation,camera);
  std::array<uint32_t,4> bools{};ObservedRigidMotionInputs first,second;
  SR_CHECK(CaptureObservedRigidCommonMotionInputs(shaders[0],invocation,floats,bools,19,camera,true,false,first));
  TemporalGeometryHistory history;TemporalGeometrySample sample;
  sample.mesh=1;sample.camera_domain=2;sample.instance=3;sample.instance_generation=first.part.generation;
  sample.proof=GeometryHistoryProof::kVerifiedInstance;sample.geometry_inputs=4;sample.projection_inputs=5;sample.range={0,0,3};
  history.Begin(1,1280,720,false);history.Add(sample);history.Seal();history.End();
  // A/B have identical owner and canonical mesh, no duplicate in either frame.
  // History alone would match, but a different part-local clip is rejected.
  floats[35]=std::bit_cast<uint32_t>(26.f);
  SR_CHECK(!CaptureObservedRigidCommonMotionInputs(shaders[0],invocation,floats,bools,19,camera,true,false,second));
  sample.geometry_inputs=6;sample.projection_inputs=7;
  history.Begin(2,1280,720,false);history.Add(sample);history.Seal();
  SR_CHECK(history.Resolve(0).status==GeometryHistoryStatus::kMatch);history.End();
  // Actual owner motion plus a consistent new scene camera remains eligible.
  invocation.rigid_owner_world[12]=std::bit_cast<uint32_t>(8.f);camera[3]=std::bit_cast<uint32_t>(11.f);
  floats=CommonFloats(shaders[0],invocation,camera);
  SR_CHECK(CaptureObservedRigidCommonMotionInputs(shaders[0],invocation,floats,bools,19,camera,true,false,second));
  SR_CHECK(first.part==second.part);SR_CHECK(first.position_words!=second.position_words);
  history.Begin(3,1280,720,false);history.Add(sample);history.Seal();
  SR_CHECK(history.Resolve(0).status==GeometryHistoryStatus::kMatch);
}
