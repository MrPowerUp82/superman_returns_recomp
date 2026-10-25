#include "post_vertex_capture.h"
#include "shaders/post_vertex_world1_gs.h"
#include "shaders/post_vertex_world2_gs.h"
#include "shaders/post_vertex_world0_gs.h"
#include <rex/logging.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>

namespace superman_returns::graphics::d3d12 {
namespace {
void Change(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
            D3D12_RESOURCE_STATES& before, D3D12_RESOURCE_STATES after) {
  if(before==after) return;
  D3D12_RESOURCE_BARRIER b{};
  b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};
  list->ResourceBarrier(1,&b);
  before=after;
}
bool Buffer(ID3D12Device* d, uint64_t bytes, D3D12_HEAP_TYPE type,
            D3D12_RESOURCE_STATES state, ID3D12Resource** out) {
  D3D12_HEAP_PROPERTIES heap{}; heap.Type=type;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width=bytes; desc.Height=1; desc.DepthOrArraySize=1; desc.MipLevels=1;
  desc.SampleDesc.Count=1; desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return SUCCEEDED(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,
                                               nullptr,IID_PPV_ARGS(out)));
}
}
D3D12_SHADER_BYTECODE PostVertexCapture::GeometryShader(uint32_t texcoord) {
  if(texcoord==0) return {kPostVertexWorld0GS,sizeof(kPostVertexWorld0GS)};
  if(texcoord==1) return {kPostVertexWorld1GS,sizeof(kPostVertexWorld1GS)};
  if(texcoord==2) return {kPostVertexWorld2GS,sizeof(kPostVertexWorld2GS)};
  return {};
}
bool PostVertexCapture::Initialize(ID3D12Device* device) {
  if(!device || !Buffer(device,kBytes,D3D12_HEAP_TYPE_UPLOAD,
                        D3D12_RESOURCE_STATE_GENERIC_READ,&zeros_)) return false;
  void* mapped=nullptr;
  const D3D12_RANGE empty{0,0};
  if(FAILED(zeros_->Map(0,&empty,&mapped))) return false;
  std::memset(mapped,0,kBytes);
  zeros_->Unmap(0,nullptr);
  for(auto& slot:slots_) {
    if(!Buffer(device,kBytes,D3D12_HEAP_TYPE_DEFAULT,slot.vertex_state,&slot.vertices) ||
       !Buffer(device,kMaxDraws*8,D3D12_HEAP_TYPE_DEFAULT,slot.counter_state,&slot.counters))
      return false;
    slot.ranges.reserve(kMaxDraws);
  }
  return true;
}
bool PostVertexCapture::Begin(ID3D12GraphicsCommandList* list,uint32_t slot_index) {
  if(!list || slot_index>=kFrames || !zeros_) return false;
  ReportCompleted(slot_index);
  slot_=slot_index;
  auto& s=slots_[slot_];
  s.ranges.clear(); s.used=s.dropped=0;
  Change(list,s.vertices.Get(),s.vertex_state,D3D12_RESOURCE_STATE_COPY_DEST);
  Change(list,s.counters.Get(),s.counter_state,D3D12_RESOURCE_STATE_COPY_DEST);
  // Unemitted strip vertices (restarts/degenerates) remain zero-length triangles.
  // This permits a bounded BLAS input without a CPU stall on the SO byte counter.
  list->CopyBufferRegion(s.vertices.Get(),0,zeros_.Get(),0,kBytes);
  list->CopyBufferRegion(s.counters.Get(),0,zeros_.Get(),0,kMaxDraws*8);
  Change(list,s.vertices.Get(),s.vertex_state,D3D12_RESOURCE_STATE_STREAM_OUT);
  Change(list,s.counters.Get(),s.counter_state,D3D12_RESOURCE_STATE_STREAM_OUT);
  return true;
}
std::optional<PostVertexCapture::Range> PostVertexCapture::Reserve(
    uint32_t vertices,uint64_t shader,bool character) {
  auto& s=slots_[slot_];
  const uint64_t bytes=uint64_t(vertices)*12;
  if(!vertices || vertices%3 || bytes>kBytes-s.used || s.ranges.size()>=kMaxDraws) {
    ++s.dropped; return {};
  }
  Range r{};
  r.offset=s.used; r.vertices=vertices; r.vs=shader; r.character=character;
  r.view={s.vertices->GetGPUVirtualAddress()+s.used,bytes,
          s.counters->GetGPUVirtualAddress()+s.ranges.size()*8};
  s.ranges.push_back(r); s.used+=uint32_t(bytes);
  return r;
}
void PostVertexCapture::Finish(ID3D12GraphicsCommandList* list,bool readback) {
  auto& s=slots_[slot_];
  list->SOSetTargets(0,0,nullptr);
  if(readback && s.used) {
    if(!s.readback) {
      Microsoft::WRL::ComPtr<ID3D12Device> d;
      if(FAILED(s.vertices->GetDevice(IID_PPV_ARGS(&d))) ||
         !Buffer(d.Get(),uint64_t(kBytes)+kMaxDraws*8,D3D12_HEAP_TYPE_READBACK,
                 D3D12_RESOURCE_STATE_COPY_DEST,&s.readback)) readback=false;
    }
    if(readback) {
      Change(list,s.vertices.Get(),s.vertex_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
      Change(list,s.counters.Get(),s.counter_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->CopyBufferRegion(s.readback.Get(),0,s.counters.Get(),0,s.ranges.size()*8);
      list->CopyBufferRegion(s.readback.Get(),kMaxDraws*8,s.vertices.Get(),0,s.used);
      s.pending_readback=true;
    }
  }
  Change(list,s.vertices.Get(),s.vertex_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}
void PostVertexCapture::ReportCompleted(uint32_t slot_index) {
  auto& s=slots_[slot_index];
  if(!s.pending_readback || !s.readback) return;
  s.pending_readback=false;
  const D3D12_RANGE read{0,size_t(kMaxDraws*8)+s.used};
  void* mapped=nullptr;
  if(FAILED(s.readback->Map(0,&read,&mapped))) return;
  const auto* bytes=static_cast<const uint8_t*>(mapped);
  uint64_t vertices=0; uint32_t invalid=0;
  std::map<uint64_t,std::pair<uint32_t,uint64_t>> families;
  for(size_t i=0;i<s.ranges.size();++i) {
    uint32_t written=0;
    std::memcpy(&written,bytes+i*8,4);
    const auto& r=s.ranges[i];
    if(written>uint64_t(r.vertices)*12 || written%36) { ++invalid; continue; }
    vertices+=written/12;
    auto& family=families[r.vs]; ++family.first; family.second+=written/12;
    if(family.first==1 && written>=36) {
      float p[9]; std::memcpy(p,bytes+kMaxDraws*8+r.offset,sizeof(p));
      REXLOG_INFO("native post-VS sample: vs={:016X} triangle=({:.6g},{:.6g},{:.6g}) ({:.6g},{:.6g},{:.6g}) ({:.6g},{:.6g},{:.6g}) vertices={} (audited world varying; scene coverage incomplete)",
                  r.vs,p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],written/12);
    }
  }
  const D3D12_RANGE empty{0,0}; s.readback->Unmap(0,&empty);
  REXLOG_INFO("native post-VS capture: draws={} vertices={} invalid={} dropped={} (not a complete RT scene)",s.ranges.size(),vertices,invalid,s.dropped);
  for(const auto& [shader,stats]:families)
    REXLOG_INFO("native post-VS family: vs={:016X} draws={} vertices={} (producer family; no logical character identity)",shader,stats.first,stats.second);
}
}
