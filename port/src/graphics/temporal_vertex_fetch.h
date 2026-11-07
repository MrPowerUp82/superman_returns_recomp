#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace superman_returns::graphics {
struct TemporalFetchResource {
  uint32_t descriptor=0;
  uint64_t key=0,generation=0,content_version=0;
};
using TemporalFetchSignature=std::array<std::array<uint64_t,6>,224>;
// A transient descriptor index is a binding, not resource/content identity.
// Only already-proven host resource generations and upload versions may replace it.
// Static IA does not read raw descriptor/offset/stride fields. Preserve its
// attribute type conversions; actual IA buffers/layout remain in the mesh key.
inline bool CaptureTemporalVertexFetch(const uint32_t (&metadata)[224][4],
    std::span<const TemporalFetchResource> resources,TemporalFetchSignature& output,
    bool shader_vertex_fetch=true) {
  TemporalFetchSignature result{};
  for(size_t i=0;i<224;++i) {
    const uint32_t descriptor=shader_vertex_fetch ? metadata[i][0] : 0;
    const TemporalFetchResource* found=nullptr;
    if(descriptor) {
      for(const auto& resource:resources) {
        if(resource.descriptor!=descriptor) continue;
        if(!resource.key || !resource.generation || !resource.content_version) return false;
        if(found && (found->key!=resource.key || found->generation!=resource.generation ||
                     found->content_version!=resource.content_version)) return false;
        found=&resource;
      }
      if(!found) return false;
    }
    result[i]={found?found->key:0,found?found->generation:0,found?found->content_version:0,
               shader_vertex_fetch ? metadata[i][1] : 0,shader_vertex_fetch ? metadata[i][2] : 0,metadata[i][3]};
  }
  output=result;return true;
}
}
