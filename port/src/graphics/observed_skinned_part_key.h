#pragma once
#include "observed_instance_lifetimes.h"
#include <array>
namespace superman_returns::graphics {
// This key may be supplied only from the audited source-route observer.
// Asset namespaces plus actual section/attachment/stream selections disambiguate
// repeated mesh slices; packet addresses, draw order and palette addresses do not.
// Canonical GPU mesh/fetch versions and exact clip/input checks remain required.
struct ObservedSkinnedPartKey {
  std::array<uint64_t,9> instance_words{};
  uint64_t generation=0;
  bool operator==(const ObservedSkinnedPartKey&) const=default;
};
inline bool CaptureObservedSkinnedPartKey(const ObservedRenderInvocation& invocation,
    ObservedSkinnedPartKey& result) {
  const auto& p=invocation.part;
  const bool c209=invocation.render==0x82300328 && invocation.vtable==0x82021D18;
  const bool body=invocation.render==0x82306BB0 && invocation.vtable==0x820217E0;
  if((!c209 && !body) ||
     invocation.token.address<0x10000 || (invocation.token.address&3) ||
     !invocation.token.generation || !p.source_route_observed ||
     p.model<0x10000 || (p.model&3) || p.sections<0x10000 || (p.sections&3) ||
     (c209 && p.origin!=1 && p.origin!=2) || (body && p.origin!=3) || p.stream_offset>=0x20000 ||
     p.submesh>0xFFFF || p.group>0xFFFF || p.attachment>0xFFFF ||
     (p.origin==1 && p.attachment) || (p.origin==2 && p.attachment==0xFFFF) ||
     (body && (p.attachment || p.lod_slice || p.submesh!=p.group || uint64_t(p.group)*4!=p.stream_offset))) return false;
  ObservedSkinnedPartKey captured;
  captured.instance_words={invocation.token.address,p.model,p.sections,p.origin,
      p.lod_slice,p.group,p.attachment,p.stream_offset,p.submesh};
  captured.generation=invocation.token.generation;
  result=captured;return true;
}
}
