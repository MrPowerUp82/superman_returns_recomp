#pragma once
#include "descriptors.h"
#include "resources.h"
#include <unordered_map>
namespace superman_returns::graphics::vulkan {
struct DescriptorPage {
  Context* context=nullptr;
  VkDescriptorPool pool=VK_NULL_HANDLE;
  uint32_t draws=0;
  ~DescriptorPage();
};
struct SamplerResource {
  Context* context=nullptr;
  VkSampler handle=VK_NULL_HANDLE;
  ~SamplerResource();
};
// Sets 1-3 (textures, samplers, vertex buffers) shared by every draw that binds
// the same resources. The entry only watches buffers/textures (weak), so it
// never extends their lifetime: a hit requires all of them alive, which also
// guarantees their addresses (the key) were not reused. Draws hold strong
// references while their submission is in flight.
struct DescriptorCacheEntry {
  Context* context=nullptr;
  std::vector<uint64_t> key;
  std::array<VkDescriptorSet,3> sets{};
  std::shared_ptr<DescriptorPage> pool;
  std::vector<std::weak_ptr<void>> watched;
  std::vector<std::shared_ptr<void>> samplers;
  uint64_t last_used=0;
  // Returns the sets to their pool; runs once no submission or cache holds it.
  ~DescriptorCacheEntry();
};
struct DescriptorDraw {
  std::array<VkDescriptorSet,4> sets{};
  // Set 0 binds the draw's constants with dynamic offsets: bind with these three offsets.
  std::array<uint32_t,3> dynamic_offsets{};
  std::shared_ptr<DescriptorCacheEntry> shared;
  std::vector<std::shared_ptr<void>> resources;
};
class DescriptorStore {
public:
  explicit DescriptorStore(Context& context):c_(context) {}
  ~DescriptorStore();
  bool Initialize(Error&);
  std::shared_ptr<DescriptorDraw> Prepare(const DrawBindings&,
      const std::array<std::array<uint32_t,6>,32>& fetch,
      ResourceStore&,uint64_t submission,Error&);
  void Retire(uint64_t completed_serial);
  const std::array<VkDescriptorSetLayout,4>& Layouts() const {return layouts_;}
private:
  std::shared_ptr<SamplerResource> Sampler(std::span<const uint32_t,6>,Error&);
  std::shared_ptr<DescriptorCacheEntry> Shared(const DrawBindings&,const std::array<std::array<uint32_t,6>,32>&,ResourceStore&,uint64_t serial,std::vector<std::shared_ptr<void>>& bound,Error&);
  void Evict(uint64_t serial);
  Context& c_;
  std::array<VkDescriptorSetLayout,4> layouts_{};
  std::map<std::array<uint32_t,2>,std::shared_ptr<SamplerResource>> samplers_;
  // Set 0 (the draw's constants, bound with dynamic offsets): one set per arena chunk.
  struct ConstantSetEntry {std::weak_ptr<BufferResource> owner;VkDescriptorSet set=VK_NULL_HANDLE;};
  VkDescriptorSet ConstantSet(const BufferResource& block,Error&);
  void FreeConstantSet(VkDescriptorSet);
  std::unordered_map<VkBuffer,ConstantSetEntry> constant_sets_;
  std::shared_ptr<DescriptorPage> constant_pool_;
  std::map<VkFormat,VkFormatFeatureFlags> filter_features_;
  std::unordered_map<uint64_t,std::vector<std::shared_ptr<DescriptorCacheEntry>>> cache_;
  std::vector<std::shared_ptr<DescriptorPage>> cache_pools_;
  size_t cache_entries_=0;uint64_t last_evict_=0;
public:
  struct CacheStats {uint64_t hits=0,misses=0,evicted=0,entries=0;};
  CacheStats TakeCacheStats() {auto s=cache_stats_;s.entries=cache_entries_;cache_stats_={};return s;}
private:
  CacheStats cache_stats_;
  SubmissionResources pending_;
  uint64_t completed_=0;
};
} // namespace superman_returns::graphics::vulkan
