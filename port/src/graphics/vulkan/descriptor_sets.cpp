#include "descriptor_sets.h"
#include "device_requirements.h"
#include <algorithm>
namespace superman_returns::graphics::vulkan {
DescriptorPage::~DescriptorPage() {if(pool) context->f.vkDestroyDescriptorPool(context->device,pool,nullptr);}
DescriptorCacheEntry::~DescriptorCacheEntry() {
  if(!context || !pool || !sets[0]) return;
  context->f.vkFreeDescriptorSets(context->device,pool->pool,3,sets.data());--pool->draws;
}
SamplerResource::~SamplerResource() {if(handle) context->f.vkDestroySampler(context->device,handle,nullptr);}
DescriptorStore::~DescriptorStore() {
  if(c_.device) c_.f.vkDeviceWaitIdle(c_.device);
  pending_.Retire(UINT64_MAX);pages_.clear();free_pages_.clear();cache_.clear();cache_pools_.clear();samplers_.clear();
  for(auto layout:layouts_) if(layout) c_.f.vkDestroyDescriptorSetLayout(c_.device,layout,nullptr);
}
bool DescriptorStore::Initialize(Error& e) {
  if(layouts_[0]) {e={"Descriptor layouts",VK_ERROR_INITIALIZATION_FAILED,"Descriptor store already initialized"};return false;}
  auto requirements=GameBindingLayout();
  auto missing=CheckDeviceRequirements({}, {},requirements,GetDeviceCaps(c_.properties.limits,c_.enabled_features));
  if(!missing.empty()) {e={"Descriptor layout limits",VK_ERROR_FEATURE_NOT_PRESENT,missing.front()};return false;}
  std::array<std::vector<VkDescriptorSetLayoutBinding>,4> bindings;
  for(auto r:requirements) bindings[r.set].push_back({r.binding,r.type,r.count,r.stages,nullptr});
  std::array<VkDescriptorSetLayout,4> next{};
  for(uint32_t set=0;set<4;++set) {
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount=uint32_t(bindings[set].size());info.pBindings=bindings[set].data();
    if(!Check(c_.f.vkCreateDescriptorSetLayout(c_.device,&info,nullptr,&next[set]),"Create game descriptor layout",e)) {
      for(auto layout:next) if(layout) c_.f.vkDestroyDescriptorSetLayout(c_.device,layout,nullptr);
      return false;
    }
  }
  layouts_=next;e={};return true;
}
std::shared_ptr<SamplerResource> DescriptorStore::Sampler(std::span<const uint32_t,6> fetch,Error& e) {
  std::array<uint32_t,2> key{fetch[0]&0x7fc00u,fetch[3]&0xff80000u};
  if(auto found=samplers_.find(key);found!=samplers_.end()) return found->second;
  VkSamplerCreateInfo info{};
  if(!PlanSampler(fetch,c_.enabled_features,c_.properties.limits,c_.mirror_clamp_enabled,info,e)) return {};
  if(samplers_.size()>=c_.properties.limits.maxSamplerAllocationCount) {
    e={"Sampler cache",VK_ERROR_TOO_MANY_OBJECTS,"Sampler allocation limit exhausted"};return {};
  }
  auto sampler=std::make_shared<SamplerResource>();sampler->context=&c_;
  if(!Check(c_.f.vkCreateSampler(c_.device,&info,nullptr,&sampler->handle),"Create game sampler",e)) return {};
  samplers_.emplace(key,sampler);return sampler;
}
std::shared_ptr<DescriptorCacheEntry> DescriptorStore::Shared(const DrawBindings& bindings,
    const std::array<std::array<uint32_t,6>,32>& fetch,ResourceStore& store,uint64_t serial,std::vector<std::shared_ptr<void>>& bound,Error& e) {
  // Key: identity of every bound resource plus the sampler/filter state that
  // decides which sampler and which format checks apply. Consecutive slots
  // mostly bind the same dummy IDs, so repeated lookups are memoized.
  std::vector<uint64_t> key;key.reserve(32*7);
  guest::ResourceId last_buffer=~0ull,last_texture=~0ull;const void* buffer_ptr=nullptr;const void* texture_ptr=nullptr;
  auto texture_key=[&](guest::ResourceId id) -> const void* {
    if(id!=last_texture) {auto* t=store.FindTexture(id);texture_ptr=t?t->get():nullptr;last_texture=id;}
    return texture_ptr;
  };
  // Draws binding per-submission transient buffers (inline geometry) never
  // repeat: they are written but not cached, so they cannot pin arena chunks.
  bool complete=true,cacheable=true;
  for(uint32_t slot=0;slot<32;++slot) {
    if(bindings.vertex_buffers[slot]!=last_buffer) {auto* b=store.FindBuffer(bindings.vertex_buffers[slot]);buffer_ptr=b?b->get():nullptr;last_buffer=bindings.vertex_buffers[slot];if(b && (*b)->owner) cacheable=false;}
    key.push_back(uint64_t(reinterpret_cast<uintptr_t>(buffer_ptr)));complete&=buffer_ptr!=nullptr;
    for(uint32_t dimension=0;dimension<3;++dimension) {auto* t=texture_key(bindings.textures[dimension][slot]);key.push_back(uint64_t(reinterpret_cast<uintptr_t>(t)));complete&=t!=nullptr;}
    // Only the sampler/filter bits of texture slots matter. Other slots hold
    // vertex fetch constants whose addresses change every frame.
    if((fetch[slot][0]&3)==2) {
      key.push_back((uint64_t(fetch[slot][0]&0x7fc00u)<<32)|(fetch[slot][3]&0xff80000u));
      key.push_back((uint64_t(1)<<63)|(uint64_t((fetch[slot][5]>>9)&3)<<32)|bindings.texture_indices[slot]);
    } else key.push_back(bindings.texture_indices[slot]);
  }
  uint64_t hash=14695981039346656037ull;for(auto v:key) {hash^=v;hash*=1099511628211ull;}
  if(complete && cacheable) if(auto found=cache_.find(hash);found!=cache_.end()) {
    auto& list=found->second;
    for(auto it=list.begin();it!=list.end();++it) if((*it)->key==key) {
      auto entry=*it;bound.clear();
      for(auto& weak:entry->watched) {auto strong=weak.lock();if(!strong) {bound.clear();break;}bound.push_back(std::move(strong));}
      if(bound.size()!=entry->watched.size()) {list.erase(it);--cache_entries_;break;}  // a resource died: rebuild
      entry->last_used=serial;++cache_stats_.hits;e={};return entry;
    }
  }
  ++cache_stats_.misses;bound.clear();
  // Miss: resolve, validate and write the sets exactly as an uncached draw would.
  auto entry=std::make_shared<DescriptorCacheEntry>();entry->key=std::move(key);entry->last_used=serial;entry->context=&c_;
  std::array<VkDescriptorBufferInfo,32> vertex_info{};
  std::array<std::array<VkDescriptorImageInfo,32>,3> texture_info{};
  std::array<VkDescriptorImageInfo,32> sampler_info{};
  for(uint32_t slot=0;slot<32;++slot) {
    auto buffer=store.Buffer(bindings.vertex_buffers[slot],e);if(!buffer) return {};
    vertex_info[slot]={buffer->handle,buffer->offset,buffer->size};bound.push_back(buffer);
    for(uint32_t dimension=0;dimension<3;++dimension) {
      auto texture=store.Texture(bindings.textures[dimension][slot],e);if(!texture) return {};
      auto expected=dimension==0?VK_IMAGE_VIEW_TYPE_2D:(dimension==1?VK_IMAGE_VIEW_TYPE_3D:VK_IMAGE_VIEW_TYPE_CUBE);
      if(texture->view_type!=expected) {e={"Texture descriptors",VK_ERROR_INITIALIZATION_FAILED,"Texture view dimension mismatches shader array"};return {};}
      texture_info[dimension][slot]={VK_NULL_HANDLE,texture->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};bound.push_back(texture);
    }
    // Non-texture slots only expose the dummy texture: any sampler will do.
    static constexpr std::array<uint32_t,6> no_texture{};
    auto sampler=Sampler((fetch[slot][0]&3)==2?std::span<const uint32_t,6>(fetch[slot]):std::span<const uint32_t,6>(no_texture),e);if(!sampler) return {};
    sampler_info[slot]={sampler->handle,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
    if(entry->samplers.empty() || entry->samplers.back()!=sampler) entry->samplers.push_back(sampler);
    if((fetch[slot][0]&3)==2 && (((fetch[slot][3]>>19)&3)==1 || ((fetch[slot][3]>>21)&3)==1 || ((fetch[slot][3]>>25)&7)>1)) {
      uint32_t dimension=(fetch[slot][5]>>9)&3;dimension=dimension?dimension-1:0;
      auto texture=store.Texture(bindings.textures[dimension][bindings.texture_indices[slot]&0x7fffu],e);if(!texture) return {};
      auto features=filter_features_.find(texture->format);
      if(features==filter_features_.end()) {VkFormatProperties properties{};c_.f.vkGetPhysicalDeviceFormatProperties(c_.physical,texture->format,&properties);features=filter_features_.emplace(texture->format,properties.optimalTilingFeatures).first;}
      if(!(features->second&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        e={"Texture filtering",VK_ERROR_FORMAT_NOT_SUPPORTED,"Guest sampler requests linear filtering of an unsupported format"};return {};
      }
    }
  }
  // Bind each distinct buffer/texture once; the entry watches them weakly.
  std::sort(bound.begin(),bound.end());bound.erase(std::unique(bound.begin(),bound.end()),bound.end());
  for(auto& resource:bound) entry->watched.push_back(resource);
  constexpr uint32_t entries_per_pool=64;
  // Entries own old texture versions (e.g. movie frames): sweep regularly.
  // A full cache stops admitting entries instead of scanning on every miss.
  if(serial>=last_evict_+4) {Evict(serial);last_evict_=serial;}
  if(cache_entries_>=4096) cacheable=false;
  for(auto& pool:cache_pools_) if(pool->draws<entries_per_pool) {entry->pool=pool;break;}
  if(!entry->pool) {
    auto pool=std::make_shared<DescriptorPage>();pool->context=&c_;
    VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,32*entries_per_pool},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,96*entries_per_pool},{VK_DESCRIPTOR_TYPE_SAMPLER,32*entries_per_pool}};
    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};info.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;info.maxSets=3*entries_per_pool;info.poolSizeCount=3;info.pPoolSizes=sizes;
    if(!Check(c_.f.vkCreateDescriptorPool(c_.device,&info,nullptr,&pool->pool),"Create shared descriptor pool",e)) return {};
    cache_pools_.push_back(pool);entry->pool=pool;
  }
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=entry->pool->pool;allocate.descriptorSetCount=3;allocate.pSetLayouts=layouts_.data()+1;
  if(!Check(c_.f.vkAllocateDescriptorSets(c_.device,&allocate,entry->sets.data()),"Allocate shared descriptor sets",e)) return {};
  ++entry->pool->draws;
  std::array<VkWriteDescriptorSet,5> writes{};
  auto write=[&](uint32_t index,uint32_t set,uint32_t binding,VkDescriptorType type,const VkDescriptorBufferInfo* buffers,const VkDescriptorImageInfo* images) {
    auto& w=writes[index];w={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=entry->sets[set];w.dstBinding=binding;w.descriptorCount=32;w.descriptorType=type;w.pBufferInfo=buffers;w.pImageInfo=images;
  };
  for(uint32_t dim=0;dim<3;++dim) write(dim,0,dim,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,nullptr,texture_info[dim].data());
  write(3,1,0,VK_DESCRIPTOR_TYPE_SAMPLER,nullptr,sampler_info.data());
  write(4,2,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,vertex_info.data(),nullptr);
  c_.f.vkUpdateDescriptorSets(c_.device,uint32_t(writes.size()),writes.data(),0,nullptr);
  if(complete && cacheable) {cache_[hash].push_back(entry);++cache_entries_;}
  e={};return entry;
}
void DescriptorStore::Evict(uint64_t serial) {
  // Free entries no submission holds (use_count 1) and unused for 8 frames.
  for(auto it=cache_.begin();it!=cache_.end();) {
    auto& list=it->second;
    std::erase_if(list,[&](std::shared_ptr<DescriptorCacheEntry>& entry) {
      if(entry.use_count()!=1 || entry->last_used+8>serial) return false;
      --cache_entries_;++cache_stats_.evicted;return true;  // destructor frees the sets
    });
    it=list.empty()?cache_.erase(it):std::next(it);
  }
}
std::shared_ptr<DescriptorDraw> DescriptorStore::Prepare(const DrawBindings& bindings,
    const std::array<std::array<uint32_t,6>,32>& fetch,ResourceStore& store,uint64_t serial,Error& e) {
  if(!layouts_[0] || serial<=completed_ || store.CurrentSerial()!=serial) {
    e={"Draw descriptors",VK_ERROR_INITIALIZATION_FAILED,"Missing layouts or recording submission"};return {};
  }
  for(uint32_t slot=0;slot<32;++slot) if((bindings.texture_indices[slot]&0x7fffu)>=32 || bindings.sampler_indices[slot]!=slot) {
    e={"Draw descriptors",VK_ERROR_INITIALIZATION_FAILED,"Descriptor index was not remapped into the draw arrays"};return {};
  }
  auto draw=std::make_shared<DescriptorDraw>();
  draw->shared=Shared(bindings,fetch,store,serial,draw->resources,e);if(!draw->shared) return {};
  std::array<VkDescriptorBufferInfo,3> constant_info{};
  std::array<std::span<const std::byte>,3> bytes{std::as_bytes(std::span(bindings.constants.vs)),std::as_bytes(std::span(bindings.constants.ps)),std::as_bytes(std::span(bindings.constants.shared))};
  for(uint32_t i=0;i<3;++i) {
    // Suballocated from the store's mapped per-submission arena.
    ++constant_serial_;auto buffer=store.UploadTransient(0,bytes[i],4096,e);
    if(!buffer) return {};
    constant_info[i]={buffer->handle,buffer->offset,4096};draw->resources.push_back(buffer);
  }
  // Only the per-draw constant set comes from the per-submission pages.
  constexpr uint32_t sets_per_page=512;
  auto& pages=pages_[serial];
  if((pages.empty() || pages.back()->draws>=sets_per_page) && !free_pages_.empty()) {
    pages.push_back(std::move(free_pages_.back()));free_pages_.pop_back();
  }
  if(pages.empty() || pages.back()->draws>=sets_per_page) {
    auto page=std::make_shared<DescriptorPage>();page->context=&c_;
    VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3*sets_per_page}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=sets_per_page;pool.poolSizeCount=1;pool.pPoolSizes=sizes;
    if(!Check(c_.f.vkCreateDescriptorPool(c_.device,&pool,nullptr,&page->pool),"Create draw descriptor page",e)) return {};
    pages.push_back(page);
  }
  draw->page=pages.back();
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=draw->page->pool;allocate.descriptorSetCount=1;allocate.pSetLayouts=layouts_.data();
  if(!Check(c_.f.vkAllocateDescriptorSets(c_.device,&allocate,draw->sets.data()),"Allocate draw descriptor sets",e)) return {};
  ++draw->page->draws;
  for(uint32_t i=0;i<3;++i) draw->sets[1+i]=draw->shared->sets[i];
  std::array<VkWriteDescriptorSet,3> writes{};
  for(uint32_t i=0;i<3;++i) {auto& w=writes[i];w={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=draw->sets[0];w.dstBinding=i;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=&constant_info[i];}
  c_.f.vkUpdateDescriptorSets(c_.device,uint32_t(writes.size()),writes.data(),0,nullptr);
  pending_.Keep(serial,draw);e={};return draw;
}
void DescriptorStore::Retire(uint64_t serial) {
  completed_=std::max(completed_,serial);pending_.Retire(completed_);
  // Reset retired pools for reuse instead of destroying and recreating them.
  for(auto it=pages_.begin();it!=pages_.upper_bound(completed_);it=pages_.erase(it))
    for(auto& page:it->second) if(page.use_count()==1 && free_pages_.size()<64 && c_.f.vkResetDescriptorPool(c_.device,page->pool,0)==VK_SUCCESS) {page->draws=0;free_pages_.push_back(std::move(page));}
}
} // namespace superman_returns::graphics::vulkan
