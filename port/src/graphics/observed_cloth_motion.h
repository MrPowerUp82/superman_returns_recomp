#pragma once
#include "guest/captured_batch.h"
#include "temporal_vertex_fetch.h"
#include <algorithm>
#include <cstring>
#include <bit>
#include <cmath>

namespace superman_returns::graphics {
struct ObservedClothShaderInputs {
  std::array<uint32_t,1028> position_words{};
  std::array<uint32_t,16> clip_rows{};
  bool operator==(const ObservedClothShaderInputs&) const=default;
};
inline bool CaptureObservedClothShaderInputs(uint64_t shader,
    std::span<const uint32_t,1024> floats,std::span<const uint32_t,4> bools,
    ObservedClothShaderInputs& output) {
  if(shader!=0xA5232A3184AA617Cull) return false;
  ObservedClothShaderInputs result;
  for(size_t i=0;i<floats.size();++i) {
    if(!std::isfinite(std::bit_cast<float>(floats[i]))) return false;
    result.position_words[i]=floats[i];
  }
  // A523: CPU-deformed local position -> world c3..6 -> clip c7..10.
  std::copy_n(floats.begin()+28,16,result.clip_rows.begin());
  std::copy(bools.begin(),bools.end(),result.position_words.begin()+1024);
  output=result;return true;
}
// Local canonical identities, valid only for the audited 82301510 cloth path.
// Dynamic ring allocations are replaced by source-proven persistent vertex
// slots. Static UV/index content and all fetch layouts still enter mesh identity.
// Actual dynamic versions remain separate for same-frame ambiguity rejection.
struct ObservedClothMotionIdentity {
  std::array<uint64_t,2> instance_words{};
  uint64_t generation=0;
  std::array<std::array<uint64_t,7>,4> mesh_streams{};
  std::array<std::array<uint64_t,3>,4> actual_streams{};
  std::array<uint64_t,3> index{};
  TemporalFetchSignature fetch{};
  bool operator==(const ObservedClothMotionIdentity&) const=default;
};

inline bool CaptureObservedClothMotionIdentity(uint64_t shader,
    const guest::WorkBatch& batch,const guest::WorkCmd& cmd,
    std::span<const TemporalFetchResource> resources,const TemporalFetchResource& index,
    const uint32_t (&metadata)[224][4],ObservedClothMotionIdentity& output) {
  const auto& owner=cmd.observed_render;
  const auto& c=owner.cloth;
  if(shader!=0xA5232A3184AA617Cull || owner.render!=0x82301510 || owner.vtable!=0x82021DA8 ||
     owner.token.address<0x10000 || (owner.token.address&3) || !owner.token.generation ||
     !c.source_route_observed || !c.topology_generation || c.caller!=0x822F43C0 || c.ring>2 ||
     !c.vertices || c.vertices>0x10000 || !c.quads || uint64_t(c.quads)*6>4194303 ||
     cmd.u[0]!=13 || cmd.u[1] || cmd.u[2] || uint64_t(c.quads)*4!=cmd.u[3] ||
     uint64_t(c.quads)*6!=cmd.u[5] || uint64_t(cmd.u[4])+uint64_t(cmd.u[5])*4>batch.bytes.size() ||
     !cmd.has_index || cmd.index32 || c.index32 || cmd.index.reset_index!=UINT32_MAX ||
     cmd.index.address!=c.indices.physical || cmd.index.size!=c.indices.bytes ||
     cmd.index_size!=c.indices.bytes || uint64_t(c.quads)*8>c.indices.bytes ||
     !index.key || index.key!=cmd.index.key || !index.generation || !index.content_version ||
     cmd.stream_count!=4 || uint64_t(cmd.stream_first)+4>batch.streams.size() || resources.size()!=4) return false;

  // Frozen normalized quad sequence, not an ordinal guessed from draw order.
  const auto triangle_index=[&](uint32_t i) {
    uint32_t value=0;std::memcpy(&value,batch.bytes.data()+cmd.u[4]+size_t(i)*4,4);return value;
  };
  for(uint32_t i=0;i<cmd.u[5];i+=6) {
    for(uint32_t j=0;j<6;++j) if(triangle_index(i+j)>=c.vertices) return false;
    if(triangle_index(i)!=triangle_index(i+3) || triangle_index(i+2)!=triangle_index(i+4)) return false;
  }
  ObservedClothMotionIdentity result;
  result.instance_words={owner.token.address,c.topology_generation};result.generation=owner.token.generation;
  result.index={index.key,index.generation,index.content_version};
  std::array<TemporalFetchResource,4> canonical{};
  uint32_t seen=0;
  for(uint32_t i=0;i<4;++i) {
    const auto& s=batch.streams[cmd.stream_first+i];
    if(s.stream>=4 || (seen&(1u<<s.stream))) return false;
    seen|=1u<<s.stream;const auto& expected=c.streams[s.stream];
    const uint32_t stride=s.stream==2?8:16;
    if(!expected.object || !expected.physical || !expected.bytes ||
       s.buffer.address!=expected.physical || s.buffer.size!=expected.bytes ||
       s.offset || s.stride!=stride || s.size!=expected.bytes ||
       uint64_t(c.vertices)*stride>expected.bytes) return false;
    const TemporalFetchResource* actual=nullptr;
    for(const auto& r:resources) if(r.key==s.buffer.key) {
      // Static IA legitimately has no raw SRV; nonzero shader-fetch descriptors
      // are checked independently against the actual resource map below.
      if(actual || !r.key || !r.generation || !r.content_version) return false;
      actual=&r;
    }
    if(!actual) return false;
    const uint32_t role=s.stream;
    result.actual_streams[role]={actual->key,actual->generation,actual->content_version};
    auto normalized=*actual;
    if(role!=2) {normalized.key=role+1;normalized.generation=c.topology_generation;normalized.content_version=1;}
    canonical[role]=normalized;
    result.mesh_streams[role]={normalized.key,normalized.generation,normalized.content_version,
        role,s.offset,s.size,s.stride};
  }
  // Reject ambiguous REAL descriptors before replacing dynamic identities.
  // Otherwise a static key could collide with a local canonical role number.
  TemporalFetchSignature actual_fetch;
  if(!CaptureTemporalVertexFetch(metadata,resources,actual_fetch) ||
     !CaptureTemporalVertexFetch(metadata,canonical,result.fetch)) return false;
  output=result;return true;
}
}
