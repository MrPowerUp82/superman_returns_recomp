#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <optional>
namespace superman_returns::graphics::d3d12 {
// Exact float4 clips from the original VS, expanded to nonindexed triangles.
// Source semantics/identity are intentionally outside this storage component.
class PostVertexClipCapture {
 public:
  static constexpr uint32_t kSlots=3,kMaxDraws=4096,kBytes=64u<<20;
  struct Range {D3D12_STREAM_OUTPUT_BUFFER_VIEW view{};uint32_t offset=0,vertices=0;};
  bool Initialize(ID3D12Device* device);
  // DIRECT queue. Caller fences this slot and all readers before reusing it,
  // or orders default-buffer writes after prior readers on the same queue.
  // Destruction requires completion of every submitted producer/reader.
  bool Begin(ID3D12GraphicsCommandList* list,uint32_t slot);
  std::optional<Range> Reserve(uint32_t vertices);
  // Unbind SO, transition clips to NON_PIXEL_SHADER_RESOURCE. Does not submit.
  void Finish(ID3D12GraphicsCommandList* list);
  ID3D12Resource* Buffer(uint32_t slot) const {return slot<kSlots ? slots_[slot].clips.Get() : nullptr;}
  uint32_t dropped() const {return slots_[slot_].dropped;}
  static D3D12_SHADER_BYTECODE GeometryShader();
 private:
  using Resource=Microsoft::WRL::ComPtr<ID3D12Resource>;
  struct Slot {
    Resource clips,counters;
    D3D12_RESOURCE_STATES clips_state=D3D12_RESOURCE_STATE_COPY_DEST,counters_state=D3D12_RESOURCE_STATE_COPY_DEST;
    uint32_t used=0,draws=0,dropped=0;
  };
  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Resource zeros_;
  std::array<Slot,kSlots> slots_;
  uint32_t slot_=0;
  bool active_=false;
};
}
