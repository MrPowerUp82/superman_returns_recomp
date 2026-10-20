#pragma once
#include "global_descriptors.h"
namespace superman_returns::graphics::vulkan {
struct GlobalVertexLayout {
  std::array<std::array<uint32_t,4>,224> attributes{};
  uint32_t streams_used=0;
};
bool PlanGlobalVertexLayout(const guest::DrawPacket&,GlobalVertexLayout&,Error&);
bool GlobalVertexLayoutKey(const guest::DrawPacket&,std::vector<uint32_t>&,Error&);
struct GlobalDrawBinding {uint32_t draw_id=0,table_slot=0;};
struct GlobalBindingStats {
  uint64_t draws=0,pages=0,resource_acquires=0,transient_allocations=0;
  uint64_t texture_preparations=0,texture_reuses=0;
  enum Phase {kLayout,kStreams,kPage,kConstants,kTextures,kFinish,kCount};
  std::array<uint64_t,kCount> ns{};
};
class GlobalDrawBindings {
public:
  explicit GlobalDrawBindings(Context& c):c_(c),heap_(c) {}
  bool Initialize(Error&);
  bool BeginSubmission(ResourceStore&,uint64_t serial,Error&);
  bool Prepare(const guest::DrawPacket&,ResourceStore&,uint64_t serial,GlobalDrawBinding&,Error&);
  void Retire(uint64_t serial);
  VkDescriptorSetLayout Layout() const {return heap_.Layout();}
  VkDescriptorSet Set() const {return heap_.Set();}
  const GlobalBindingStats& Stats() const {return stats_;}
  GlobalBindingStats TakeStats() {auto result=stats_;stats_={};return result;}
  void EnableProfile(bool enabled) {profiling_=enabled;}
private:
  bool BufferSlot(const std::shared_ptr<BufferResource>&,uint64_t,uint32_t&,Error&);
  bool ImageSlot(const std::shared_ptr<TextureResource>&,uint64_t,uint32_t&,Error&);
  bool SamplerSlot(const std::shared_ptr<SamplerResource>&,uint64_t,uint32_t&,Error&);
  std::shared_ptr<SamplerResource> Sampler(std::span<const uint32_t,6>,Error&);
  struct LayoutEntry {GlobalVertexLayout plan;TransientSlice slice;};
  struct Page {TransientSlice constants,streams,table;uint32_t count=0,slot=0;};
  Context& c_;GlobalDescriptorHeap heap_;
  std::map<std::array<uint32_t,2>,std::shared_ptr<SamplerResource>> samplers_;
  std::map<VkFormat,VkFormatFeatureFlags> filtering_;
  std::map<std::vector<uint32_t>,LayoutEntry> layouts_;
  std::vector<uint32_t> layout_key_;
  std::map<const BufferResource*,std::vector<Page>> pages_;
  // Entries are valid only for this recording serial. The heap retains each
  // successful acquisition, preventing pointer reuse until that serial retires.
  std::unordered_map<const BufferResource*,uint32_t> buffer_slots_;
  std::unordered_map<const TextureResource*,uint32_t> image_slots_;
  std::unordered_map<const SamplerResource*,uint32_t> sampler_slots_;
  // Two recent descriptions per guest slot; the resource lookup is still
  // required on a hit because an upload or resolve may replace the same ID.
  struct TextureBinding {
    std::array<uint32_t,6> fetch{};guest::ResourceId id=0;
    const TextureResource* resource=nullptr;uint32_t image=0,sampler=0;
  };
  std::array<std::array<TextureBinding,2>,32> texture_bindings_{};
  uint64_t serial_=0,completed_=0;uint32_t dummy_buffer_=0;
  GlobalBindingStats stats_;
  bool profiling_=false;
  bool texture_reuse_=true;
};
}
