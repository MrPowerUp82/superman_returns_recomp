#include "post_vertex_clip_capture.h"
#include "shaders/post_vertex_clip_gs.h"
#include <cstring>
namespace superman_returns::graphics::d3d12 {
namespace {
void Change(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES& before,D3D12_RESOURCE_STATES after) {
  if(before==after) return;
  D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};
  list->ResourceBarrier(1,&barrier);before=after;
}
bool MakeBuffer(ID3D12Device* device,uint32_t bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,ID3D12Resource** output) {
  D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
  D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
  desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;
  desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(output)));
}
}
bool PostVertexClipCapture::Initialize(ID3D12Device* device) {
  if(!device || device_) return false;
  Resource zeros;
  if(!MakeBuffer(device,kBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,&zeros)) return false;
  void* data=nullptr;const D3D12_RANGE empty{0,0};
  if(FAILED(zeros->Map(0,&empty,&data))) return false;
  std::memset(data,0,kBytes);zeros->Unmap(0,nullptr);
  std::array<Slot,kSlots> slots;
  for(auto& slot:slots)
    if(!MakeBuffer(device,kBytes,D3D12_HEAP_TYPE_DEFAULT,slot.clips_state,&slot.clips) ||
       !MakeBuffer(device,kMaxDraws*8,D3D12_HEAP_TYPE_DEFAULT,slot.counters_state,&slot.counters)) return false;
  device_=device;zeros_=std::move(zeros);slots_=std::move(slots);return true;
}
bool PostVertexClipCapture::Begin(ID3D12GraphicsCommandList* list,uint32_t slot) {
  if(!device_ || !list || slot>=kSlots || list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT) return false;
  Microsoft::WRL::ComPtr<ID3D12Device> owner;
  if(FAILED(list->GetDevice(IID_PPV_ARGS(&owner))) || owner.Get()!=device_.Get()) return false;
  slot_=slot;auto& s=slots_[slot_];s.used=s.draws=s.dropped=0;
  Change(list,s.clips.Get(),s.clips_state,D3D12_RESOURCE_STATE_COPY_DEST);
  Change(list,s.counters.Get(),s.counters_state,D3D12_RESOURCE_STATE_COPY_DEST);
  // Strip restarts may leave unused capacity. Zero clip has w=0 and cannot
  // produce a valid motion receiver; never expose stale triangles there.
  list->CopyBufferRegion(s.clips.Get(),0,zeros_.Get(),0,kBytes);
  list->CopyBufferRegion(s.counters.Get(),0,zeros_.Get(),0,kMaxDraws*8);
  Change(list,s.clips.Get(),s.clips_state,D3D12_RESOURCE_STATE_STREAM_OUT);
  Change(list,s.counters.Get(),s.counters_state,D3D12_RESOURCE_STATE_STREAM_OUT);
  active_=true;return true;
}
std::optional<PostVertexClipCapture::Range> PostVertexClipCapture::Reserve(uint32_t vertices) {
  auto& slot=slots_[slot_];const uint64_t bytes=uint64_t(vertices)*16;
  if(!active_ || !vertices || vertices%3 || bytes>kBytes-slot.used || slot.draws>=kMaxDraws) {
    ++slot.dropped;return {};
  }
  Range range;
  range.offset=slot.used;range.vertices=vertices;
  range.view={slot.clips->GetGPUVirtualAddress()+range.offset,bytes,
              slot.counters->GetGPUVirtualAddress()+uint64_t(slot.draws)*8};
  slot.used+=uint32_t(bytes);++slot.draws;return range;
}
void PostVertexClipCapture::Finish(ID3D12GraphicsCommandList* list) {
  if(!active_ || !list) return;
  list->SOSetTargets(0,0,nullptr);
  auto& slot=slots_[slot_];Change(list,slot.clips.Get(),slot.clips_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  active_=false;
}
D3D12_SHADER_BYTECODE PostVertexClipCapture::GeometryShader() {return {kPostVertexClipGS,sizeof(kPostVertexClipGS)};}
}
