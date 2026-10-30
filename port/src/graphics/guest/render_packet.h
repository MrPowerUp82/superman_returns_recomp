#pragma once
#include "captured_batch.h"
#include "constant_snapshot.h"
#include "packet_constants.h"
#include "replay_draw_state.h"
#include "render_state.h"
#include "vertex_layout.h"
#include <array>
#include <mutex>
#include <stdexcept>
#include <variant>
namespace superman_returns::native {
class Pm4Mirror;
}
namespace superman_returns::graphics::guest {
using ResourceId = uint64_t;
struct BufferUpdate {
  BufferPlan plan;
  std::vector<uint8_t> bytes;
  std::vector<uint32_t> normalized;
};
struct VertexStream {
  uint32_t stream = 0, offset = 0, size = 0, stride = 0;
  BufferUpdate update;
};
struct SurfaceDesc {
  ResourceId id = 0;
  bool depth = false;
  SurfaceGeometry geometry{};
};
struct DrawPacket {
  struct ReplayStorageTag {};
  // Keep this user-provided: value-initialization must not add a whole-packet
  // zeroing pass before the individual members initialize themselves.
  DrawPacket() {}
  // Only DecodeCapturedPacket may publish this storage. Float banks borrow
  // immutable snapshots; device/vertex/fetch state keeps normal initialization.
  explicit DrawPacket(ReplayStorageTag) : constants(ConstantSnapshot::ReplayStorageTag{}) {}
  int pass = 0;
  Primitive primitive = Primitive::kTriangles;
  bool indexed = false, inline_vertices = false;
  bool primitive_restart = false;
  int32_t depth_bias=0;float slope_bias=0;
  int32_t base_vertex = 0;
  uint32_t first = 0, count = 0, inline_stride = 0;
  PacketConstants constants;
  CapturedMemory memory;
  std::vector<VertexStream> streams;
  std::vector<VertexAttribute> attributes;
  std::array<VertexFetchMeta, 224> vertex_fetch{};
  BufferUpdate indices;
  std::vector<uint32_t> expanded_indices;
  std::vector<uint8_t> inline_data;
  Viewport viewport;
  Rect scissor{0, 0, 16384, 16384};
  bool tiling_active = false;
  std::array<uint32_t, 0x400> registers{};
  // Only PM4 registers 0x2380..0x2383 are consumed by FinalizeDrawConstants.
  // Keep their exact bits; the other 1,020 mirrored registers are not read.
  std::array<uint32_t, 4> polygon_offset_registers{};
  std::array<std::array<uint32_t, 6>, 32> texture_fetch{};
  uint32_t texture_mask = UINT32_MAX;
  std::array<SurfaceDesc, 4> colors{};
  SurfaceDesc depth;
  ResourceId shader_a = 0, shader_b = 0;
  std::shared_ptr<const ShaderCapture> vertex_shader, pixel_shader;
  std::array<std::shared_ptr<const TextureCapture>, 32> textures;
  std::vector<std::pair<uint32_t, std::string>> texture_errors;
  uint64_t command_serial = 0;
  bool has_device_state = false;
};
struct ClearPacket {
  uint32_t flags;
  std::array<float, 4> color;
  float depth;
  uint32_t stencil;
  std::vector<Rect> rects;
  CapturedMemory memory;
  std::array<SurfaceDesc,4> colors{};
  SurfaceDesc depth_surface;
};
struct ResolvePacket {
  uint64_t command_serial = 0;
  uint32_t flags, destination;
  bool has_source_rect=false,has_destination_point=false,tiling_active=false;
  bool has_copy_draw=false,copy_dest_swap=false;
  Rect source{};
  std::array<int32_t, 2> destination_point{};
  std::array<float, 4> clear_color{};
  float clear_depth = 0;
  uint32_t clear_stencil = 0;
  uint32_t level=0,slice=0;
  SurfaceDesc source_surface;
  SurfaceDesc clear_color_surface,clear_depth_surface;
  std::array<uint32_t,6> destination_fetch{};
  CapturedMemory memory;
};
struct SwapPacket {
  ResourceId frontbuffer;
  uint64_t guest_swap;
  CapturedMemory memory;
  std::shared_ptr<const std::array<uint32_t,256>> gamma;
  bool gamma_enabled=false;
};
struct PassPacket {
  Op operation;
  int pass;
  std::vector<Rect> rects;
  std::array<float, 4> color{};
  float depth = 0;
  uint32_t stencil = 0;
  bool clear_color = false;
  SurfaceDesc color_surface, depth_surface;
};
// Draws contain large constant/register arrays. Moving an inline variant
// copied those arrays again at every queue handoff and vector growth. Keep
// exclusive payload ownership so its address and captured state remain stable.
struct DrawPacketOwner {
  std::shared_ptr<DrawPacket> payload;
  DrawPacketOwner() = default;
  DrawPacketOwner(DrawPacket packet)
      : payload(std::make_shared<DrawPacket>(std::move(packet))) {}
  DrawPacketOwner(std::shared_ptr<DrawPacket> packet)
      : payload(std::move(packet)) {}
  DrawPacketOwner(const DrawPacketOwner&)=delete;
  DrawPacketOwner& operator=(const DrawPacketOwner&)=delete;
  DrawPacketOwner(DrawPacketOwner&&)=default;
  DrawPacketOwner& operator=(DrawPacketOwner&&)=default;
};
// Recycle empty backing vectors after the last payload alias releases them.
// A frame spans many batches (the frontend flushes at 32 commands), so retain
// enough small blocks for a frame while independently enforcing the byte cap.
class DrawPacketPool : public std::enable_shared_from_this<DrawPacketPool> {
public:
  explicit DrawPacketPool(size_t byte_budget=128ull*1024*1024,size_t block_budget=128)
      :byte_budget_(byte_budget),block_budget_(block_budget) {free_.reserve(block_budget_);}
  std::shared_ptr<std::vector<DrawPacket>> Acquire(size_t count) {
    auto pool=shared_from_this();
    std::unique_ptr<std::vector<DrawPacket>> block;
    {
      std::lock_guard lock(mutex_);
      auto best=free_.end();
      for(auto it=free_.begin();it!=free_.end();++it)
        if((*it)->capacity()>=count && (best==free_.end() || (*it)->capacity()<(*best)->capacity())) best=it;
      if(best!=free_.end()) {
        idle_bytes_-=(*best)->capacity()*sizeof(DrawPacket);
        block=std::move(*best);free_.erase(best);
      }
    }
    if(!block) block=std::make_unique<std::vector<DrawPacket>>();
    return {block.release(),[pool=std::move(pool)](std::vector<DrawPacket>* raw) noexcept {
      std::unique_ptr<std::vector<DrawPacket>> returned(raw);
      // Runs on the last alias release, potentially on the recorder thread.
      // Drop captures and other owned resources now, not on the next acquire.
      returned->clear();
      const size_t bytes=returned->capacity()*sizeof(DrawPacket);
      if(!pool->block_budget_ || bytes>pool->byte_budget_) return;
      std::lock_guard lock(pool->mutex_);
      // Evict old idle capacities so small startup batches cannot permanently
      // crowd out the larger gameplay arenas. In-flight blocks are never here.
      while(!pool->free_.empty() &&
            (pool->free_.size()>=pool->block_budget_ || bytes>pool->byte_budget_-pool->idle_bytes_)) {
        pool->idle_bytes_-=pool->free_.front()->capacity()*sizeof(DrawPacket);
        pool->free_.erase(pool->free_.begin());
      }
      pool->idle_bytes_+=bytes;pool->free_.push_back(std::move(returned));
    }};
  }
private:
  std::mutex mutex_;
  std::vector<std::unique_ptr<std::vector<DrawPacket>>> free_;
  size_t byte_budget_,block_budget_,idle_bytes_=0;
};
// One allocation for a batch's payloads instead of one large allocation per
// draw. Each alias keeps the block alive after replay recycles the WorkBatch.
// Next hands each freshly initialized element out at most once.
class DrawPacketArena {
public:
  explicit DrawPacketArena(size_t count,bool replay_storage=false,
                           std::shared_ptr<DrawPacketPool> pool={}) {
    packets_=pool?pool->Acquire(count):std::make_shared<std::vector<DrawPacket>>();
    packets_->reserve(count);
    for(size_t i=0;i<count;++i) {
      if(replay_storage) packets_->emplace_back(DrawPacket::ReplayStorageTag{});
      else packets_->emplace_back();
    }
  }
  DrawPacketArena(const DrawPacketArena&)=delete;
  DrawPacketArena& operator=(const DrawPacketArena&)=delete;
  DrawPacketArena(DrawPacketArena&&)=delete;
  DrawPacketArena& operator=(DrawPacketArena&&)=delete;
  DrawPacketOwner Next() {
    if(next_>=packets_->size()) throw std::out_of_range("Draw packet arena exhausted");
    return std::shared_ptr<DrawPacket>(packets_,&(*packets_)[next_++]);
  }
private:
  std::shared_ptr<std::vector<DrawPacket>> packets_;
  size_t next_=0;
};
using RenderPacket = std::variant<DrawPacketOwner, ClearPacket, ResolvePacket,
                                  SwapPacket, PassPacket>;
inline DrawPacket* GetDraw(RenderPacket& packet) {
  auto* owner=std::get_if<DrawPacketOwner>(&packet);
  return owner?owner->payload.get():nullptr;
}
inline const DrawPacket* GetDraw(const RenderPacket& packet) {
  auto* owner=std::get_if<DrawPacketOwner>(&packet);
  return owner?owner->payload.get():nullptr;
}
bool DecodeRenderPacket(const WorkBatch &, const WorkCmd &,
                        const native::Pm4Mirror &, RenderPacket &,
                        std::string &);
// Optional storage must be a fresh payload returned by DrawPacketArena::Next.
// Optional counters belong to the replay owner; null performs no clock reads.
struct ReplayProbe { uint64_t capture_ns=0,pm4_ns=0,decode_ns=0; };
struct PreparedReplayCommand {
  CapturedMemory memory;
  ReplayDrawState state;
  uint64_t vs_version=0,ps_version=0;
};
bool PrepareReplayCommand(const WorkBatch&,const WorkCmd&,native::Pm4Mirror&,
    PreparedReplayCommand&,std::string&,std::shared_ptr<const WorkBatch> ={},ReplayProbe* =nullptr);
bool DecodePreparedRenderPacket(const WorkBatch&,const WorkCmd&,const PreparedReplayCommand&,
    RenderPacket&,std::string&,DrawPacketOwner={},ReplayProbe* =nullptr);
bool ReplayCapturedRenderPacket(const WorkBatch&,const WorkCmd&,native::Pm4Mirror&,RenderPacket&,std::string&,DrawPacketOwner storage={},std::shared_ptr<const WorkBatch> capture_owner={},ReplayProbe* probe=nullptr);
// Original scale1/MSAA1 shader ABI state. Descriptor indices are populated by
// the backend after binding resources; these semantic constants are common.
void FinalizeDrawConstants(DrawPacket &);
} // namespace superman_returns::graphics::guest
