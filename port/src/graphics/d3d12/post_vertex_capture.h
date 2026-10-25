#pragma once
#include <rex/ui/d3d12/d3d12_api.h>
#include <wrl/client.h>
#include <array>
#include <optional>
#include <vector>
#include <cstdint>

namespace superman_returns::graphics::d3d12 {
// Data is produced by rerunning the original VS and copying an audited world varying in a GS.
// Only explicitly audited shader families may be submitted by the renderer.
class PostVertexCapture {
 public:
  static constexpr uint32_t kFrames=3, kMaxDraws=4096, kBytes=32u<<20;
  struct Range {
    D3D12_STREAM_OUTPUT_BUFFER_VIEW view{};
    uint32_t offset=0, vertices=0;
    uint64_t vs=0;
    bool character=false;
  };
  bool Initialize(ID3D12Device* device);
  // Caller has already waited for the slot fence, including any BLAS/trace work.
  bool Begin(ID3D12GraphicsCommandList* list, uint32_t slot);
  std::optional<Range> Reserve(uint32_t vertices, uint64_t shader, bool character=false);
  void Finish(ID3D12GraphicsCommandList* list, bool readback);
  void ReportCompleted(uint32_t slot);
  ID3D12Resource* vertices() const { return slots_[slot_].vertices.Get(); }
  const std::vector<Range>& ranges() const { return slots_[slot_].ranges; }
  uint32_t dropped() const { return slots_[slot_].dropped; }
  static D3D12_SHADER_BYTECODE GeometryShader(uint32_t world_texcoord);
 private:
  struct Slot {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertices, counters, readback;
    D3D12_RESOURCE_STATES vertex_state=D3D12_RESOURCE_STATE_COPY_DEST;
    D3D12_RESOURCE_STATES counter_state=D3D12_RESOURCE_STATE_COPY_DEST;
    std::vector<Range> ranges;
    uint32_t used=0, dropped=0;
    bool pending_readback=false;
  };
  Microsoft::WRL::ComPtr<ID3D12Resource> zeros_;
  std::array<Slot,kFrames> slots_;
  uint32_t slot_=0;
};
}
