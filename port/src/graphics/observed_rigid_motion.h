#pragma once
#include "observed_instance_lifetimes.h"
#include "static_world_shader_dependencies.h"

namespace superman_returns::graphics {
// Frozen values read at entry to the existing 8261CD90 hook. All chain fields
// are observations, not addresses inferred from draw order or shader names.
struct ObservedRigidPartRoute {
  ObservedInstanceToken token;
  uint32_t render=0,vtable=0,caller=0,register_owner=0;
  uint32_t owner_link=0,model_link=0,root_model=0,saved_model=0;
  uint32_t parts=0,count=0,register_count=0,slot=0,cursor=0;
  uint32_t packet=0,selected_packet=0;
  bool array_enabled=false;
};
inline bool ObservedRigidAddress(uint32_t address) {
  return address>=0x10000 && !(address&3);
}
inline bool CaptureObservedRigidPartSelection(const ObservedRigidPartRoute& route,
                                             ObservedPartSelection& output) {
  const bool base=route.render==0x8230F8E0 && route.vtable==0x820211C0 && route.caller==0x823102B0;
  const bool derived=route.render==0x823103C8 && route.vtable==0x82021930 && route.caller==0x82310D50;
  if((!base && !derived) || !ObservedRigidAddress(route.token.address) || !route.token.generation ||
     route.register_owner!=route.token.address || !ObservedRigidAddress(route.owner_link) ||
     !ObservedRigidAddress(route.model_link) || !ObservedRigidAddress(route.root_model) ||
     route.root_model!=route.saved_model || !route.array_enabled ||
     !ObservedRigidAddress(route.parts) || !route.count || route.count!=route.register_count ||
     route.slot>=route.count || route.slot>0xffff ||
     uint64_t(route.parts)+uint64_t(route.slot)*4!=route.cursor ||
     !ObservedRigidAddress(route.packet) || route.packet!=route.selected_packet) return false;
  ObservedPartSelection result;
  result.model=route.root_model;result.sections=route.parts;result.origin=base?4:5;
  result.group=result.submesh=route.slot;result.stream_offset=route.slot*4;
  result.source_route_observed=true;output=result;return true;
}
struct ObservedRigidPartKey {
  std::array<uint64_t,9> instance_words{};
  uint64_t generation=0;
  bool operator==(const ObservedRigidPartKey&) const=default;
};
struct ObservedSingleInstancePartRoute {
  ObservedRigidPartRoute part;
  uint32_t stack=0,counts_pointer=0,matrix_array=0,matrix_pointer=0;
  uint32_t count=0,groups=0,rows=0,mode=0,extra=0,enabled=0;
  std::array<uint32_t,16> owner_world{},submitted_world{};
};
inline bool CaptureObservedSingleInstancePartSelection(const ObservedSingleInstancePartRoute& route,
                                                      ObservedPartSelection& output) {
  if(route.part.render!=0x8230F8E0 || route.part.vtable!=0x820211C0 || route.part.caller!=0x82310280 ||
     !ObservedRigidAddress(route.stack) || uint64_t(route.stack)+320>UINT32_MAX ||
     route.counts_pointer!=route.stack+120 || route.matrix_array!=route.stack+144 ||
     route.matrix_pointer!=route.stack+256 || route.count!=1 || route.groups!=1 || route.rows!=4 ||
     route.mode!=1 || route.extra || route.enabled!=1 || route.owner_world!=route.submitted_world) return false;
  auto part_route=route.part;part_route.caller=0x823102B0;
  ObservedPartSelection part;
  if(!CaptureObservedRigidPartSelection(part_route,part)) return false;
  part.origin=8;output=part;return true;
}
inline bool CaptureObservedRigidPartKey(const ObservedRenderInvocation& invocation,
                                       ObservedRigidPartKey& output) {
  const auto& p=invocation.part;
  const bool base=invocation.render==0x8230F8E0 && invocation.vtable==0x820211C0 && (p.origin==4 || p.origin==8);
  const bool derived=invocation.render==0x823103C8 && invocation.vtable==0x82021930 && p.origin==5;
  if((!base && !derived) || !ObservedRigidAddress(invocation.token.address) || !invocation.token.generation ||
     !p.source_route_observed || !ObservedRigidAddress(p.model) || !ObservedRigidAddress(p.sections) ||
     !ObservedRigidAddress(invocation.packet) || p.lod_slice || p.attachment || p.actor_palette ||
     p.group>0xffff || p.submesh!=p.group || uint64_t(p.group)*4!=p.stream_offset ||
     uint64_t(p.sections)+p.stream_offset>UINT32_MAX) return false;
  ObservedRigidPartKey result;
  result.instance_words={invocation.token.address,invocation.render,invocation.vtable,
      p.model,p.sections,p.origin,p.group,p.stream_offset,p.submesh};
  result.generation=invocation.token.generation;output=result;return true;
}
struct ObservedRigidMotionInputs {
  ObservedRigidPartKey part;
  uint64_t shader=0,actual_bindings_signature=0;
  std::array<uint32_t,1028> position_words{};
  std::array<uint32_t,16> world_rows{},clip_rows{};
  bool operator==(const ObservedRigidMotionInputs&) const=default;
};
// binding signature must cover the CURRENT actual complete bindings. This pure
// helper cannot verify its provenance: retain canonical mesh/version/fetch and
// exact vertex-order checks in the consumer before granting correspondence.
inline bool CaptureObservedRigidMotionInputs(uint64_t shader,const ObservedRenderInvocation& invocation,
    std::span<const uint32_t,1024> floats,std::span<const uint32_t,4> bools,
    uint64_t actual_bindings_signature,ObservedRigidMotionInputs& output) {
  ObservedRigidMotionInputs result;
  StaticWorldShaderDependencies dependencies;
  std::array<uint32_t,32> loops{};
  if(invocation.part.origin==8 || !actual_bindings_signature || !CaptureObservedRigidPartKey(invocation,result.part) ||
     !CaptureStaticWorldShaderDependencies(shader,floats,bools,loops,dependencies) ||
     dependencies.clip_matrix_kind!=StaticWorldClipMatrixKind::kObjectToClip) return false;
  for(size_t i=0;i<floats.size();++i) {
    if(!std::isfinite(std::bit_cast<float>(floats[i]))) return false;
    result.position_words[i]=floats[i];
  }
  std::copy(bools.begin(),bools.end(),result.position_words.begin()+1024);
  std::copy_n(dependencies.position_words.begin(),16,result.world_rows.begin());
  result.clip_rows=dependencies.clip_rows;result.shader=shader;
  result.actual_bindings_signature=actual_bindings_signature;output=result;return true;
}

// This is the synchronous, source-audited count-one route only. The actual
// shader fetch/resource/order certificates are still required by its caller.
inline bool CaptureObservedSingleInstanceShaderInputs(uint64_t shader,const ObservedRenderInvocation& invocation,
    std::span<const uint32_t,1024> floats,std::span<const uint32_t,4> bools,
    uint32_t first,uint32_t end,ObservedRigidMotionInputs& output) {
  ObservedRigidMotionInputs result;
  if(shader!=0xDF1EB50388CC2563ull || invocation.part.origin!=8 || !invocation.rigid_owner_world_observed ||
     !CaptureObservedRigidPartKey(invocation,result.part) || first || !end || end>65536 ||
     floats[16]!=std::bit_cast<uint32_t>(float(end))) return false;
  for(size_t i=17;i<20;++i) if((floats[i]&0x7fffffffu)!=0) return false;
  const float infinity=std::numeric_limits<float>::infinity();
  const auto lower=[&](float v){return std::nextafter(v,-infinity);};
  const auto upper=[&](float v){return std::nextafter(v,infinity);};
  const float reciprocal=1.f/float(end);
  const float low=lower(lower(.5f)*lower(lower(reciprocal)));
  const float high=upper(upper(float(end-1)+.5f)*upper(upper(reciprocal)));
  if(low<0 || !(high<1)) return false;
  const auto& owner=invocation.rigid_owner_world;
  if((owner[3]&0x7fffffffu) || (owner[7]&0x7fffffffu) || (owner[11]&0x7fffffffu) ||
     owner[15]!=std::bit_cast<uint32_t>(1.f)) return false;
  for(size_t i=0;i<16;++i) {
    if(floats[400+i]!=owner[i]) return false;
    result.world_rows[i]=owner[i];result.clip_rows[i]=floats[232*4+i];
  }
  for(size_t i=0;i<floats.size();++i) {
    if(!std::isfinite(std::bit_cast<float>(floats[i]))) return false;
    result.position_words[i]=floats[i];
  }
  std::copy(bools.begin(),bools.end(),result.position_words.begin()+1024);
  // Binding signature remains zero here. Only the consumer's successful
  // actual fetch/resource/version checks may fill it and grant history.
  result.shader=shader;output=result;return true;
}

// This numerical certificate is independent of lighting-world banks and of
// asset-part routing. No tolerance: normalize only the two encodings of zero.
inline uint32_t ObservedRigidCanonicalZero(uint32_t word) {
  return (word&0x7fffffffu)==0 ? 0 : word;
}
inline bool CaptureObservedRigidCommonMotionInputs(uint64_t shader,const ObservedRenderInvocation& invocation,
    std::span<const uint32_t,1024> floats,std::span<const uint32_t,4> bools,
    uint64_t actual_bindings_signature,std::span<const uint32_t,16> camera,
    bool camera_valid,bool camera_conflict,ObservedRigidMotionInputs& output) {
  const bool base=invocation.render==0x8230F8E0 && invocation.vtable==0x820211C0;
  const bool derived=invocation.render==0x823103C8 && invocation.vtable==0x82021930;
  if((!base && !derived) || !ObservedRigidAddress(invocation.token.address) || !invocation.token.generation ||
     !invocation.rigid_owner_world_observed || !actual_bindings_signature || !camera_valid || camera_conflict) return false;
  const auto& owner=invocation.rigid_owner_world;
  for(size_t i=0;i<16;++i)
    if(!std::isfinite(std::bit_cast<float>(owner[i])) || !std::isfinite(std::bit_cast<float>(camera[i]))) return false;
  if(ObservedRigidCanonicalZero(owner[3]) || ObservedRigidCanonicalZero(owner[7]) ||
     ObservedRigidCanonicalZero(owner[11]) || owner[15]!=std::bit_cast<uint32_t>(1.f)) return false;
  StaticWorldShaderDependencies dependencies;std::array<uint32_t,32> loops{};
  if(!CaptureStaticWorldShaderDependencies(shader,floats,bools,loops,dependencies) ||
     dependencies.clip_matrix_kind!=StaticWorldClipMatrixKind::kObjectToClip) return false;
  for(size_t row=0;row<4;++row) for(size_t column=0;column<4;++column) {
    // owner[column*4+k] is transpose(owner)[k,column]. Volatile
    // intermediates explicitly round every operation, preventing FMA fusion.
    volatile float value=std::bit_cast<float>(camera[row*4])*std::bit_cast<float>(owner[column*4]);
    for(size_t k=1;k<4;++k) {
      volatile float product=std::bit_cast<float>(camera[row*4+k])*std::bit_cast<float>(owner[column*4+k]);
      volatile float sum=value+product;value=sum;
    }
    const float expected=value;
    if(!std::isfinite(expected) || ObservedRigidCanonicalZero(std::bit_cast<uint32_t>(expected))!=
       ObservedRigidCanonicalZero(dependencies.clip_rows[row*4+column])) return false;
  }
  ObservedRigidMotionInputs result;
  for(size_t i=0;i<floats.size();++i) {
    if(!std::isfinite(std::bit_cast<float>(floats[i]))) return false;
    result.position_words[i]=floats[i];
  }
  std::copy(bools.begin(),bools.end(),result.position_words.begin()+1024);
  std::copy_n(dependencies.position_words.begin(),16,result.world_rows.begin());
  result.clip_rows=dependencies.clip_rows;result.shader=shader;
  result.actual_bindings_signature=actual_bindings_signature;
  result.part.instance_words={invocation.token.address,invocation.render,invocation.vtable,0,0,base?6u:7u,0,0,0};
  result.part.generation=invocation.token.generation;output=result;return true;
}
}
