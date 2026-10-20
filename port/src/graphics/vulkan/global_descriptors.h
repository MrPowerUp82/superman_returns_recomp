#pragma once
#include "descriptor_sets.h"
#include <tuple>
namespace superman_returns::graphics::vulkan {
// Recording-thread cache. A slot is immutable while any submission uses it;
// the cache itself holds only a weak reference between submissions.
class GlobalSlotTable {
public:
  explicit GlobalSlotTable(uint32_t capacity):capacity_(capacity) {}
  bool Acquire(const std::shared_ptr<void>& owner,uint64_t offset,uint64_t size,
               uint64_t serial,uint32_t& slot,bool& fresh,Error&);
  void Retire(uint64_t completed_serial);
  uint64_t EvictionCandidatesExamined() const {return eviction_candidates_examined_;}
private:
  using Key=std::tuple<uintptr_t,uint64_t,uint64_t>;
  struct Entry {Key key{};std::weak_ptr<void> owner;uint64_t serial=0;bool used=false;};
  void Erase(uint32_t);
  uint32_t capacity_,eviction_cursor_=0;uint64_t completed_=0,latest_=0;
  uint64_t eviction_candidates_examined_=0;
  std::vector<Entry> entries_;std::vector<uint32_t> free_;
  std::map<Key,uint32_t> cache_;
  std::map<uint64_t,std::vector<std::shared_ptr<void>>> pending_;
};
struct GlobalHeapCapacities {
  uint32_t images_2d=4096,buffers=65536,samplers=512,images_3d=256,images_cube=1024;
};
bool ValidateGlobalHeapCapacities(const GlobalHeapCapacities&,const VkPhysicalDeviceDescriptorIndexingProperties&,Error&);
// Backend for sr-vulkan-global-v1. The caller negotiates and enables these
// features at device creation and retires only serials whose fences completed.
class GlobalDescriptorHeap {
public:
  explicit GlobalDescriptorHeap(Context& c):c_(c) {}
  ~GlobalDescriptorHeap();
  bool Initialize(const VkPhysicalDeviceDescriptorIndexingFeatures& enabled,
                  const GlobalHeapCapacities&,Error&);
  bool Buffer(const std::shared_ptr<BufferResource>&,uint64_t serial,uint32_t& slot,Error&);
  bool BufferRange(const std::shared_ptr<BufferResource>&,VkDeviceSize offset,VkDeviceSize size,
                   uint64_t serial,uint32_t& slot,Error&);
  bool Image(const std::shared_ptr<TextureResource>&,uint64_t serial,uint32_t& slot,Error&);
  bool Sampler(const std::shared_ptr<SamplerResource>&,uint64_t serial,uint32_t& slot,Error&);
  void Retire(uint64_t completed_serial);
  VkDescriptorSetLayout Layout() const {return layout_;}
  VkDescriptorSet Set() const {return set_;}
  uint64_t Writes() const {return writes_;}
private:
  Context& c_;VkDescriptorSetLayout layout_=VK_NULL_HANDLE;
  VkDescriptorPool pool_=VK_NULL_HANDLE;VkDescriptorSet set_=VK_NULL_HANDLE;
  std::array<std::unique_ptr<GlobalSlotTable>,5> slots_;
  uint64_t writes_=0;
};
}
