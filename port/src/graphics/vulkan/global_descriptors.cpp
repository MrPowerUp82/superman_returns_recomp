#include "global_descriptors.h"
#include <algorithm>
namespace superman_returns::graphics::vulkan {
namespace {
bool Fail(Error& e,const char* reason,VkResult result=VK_ERROR_INITIALIZATION_FAILED) {
  e={"Global descriptors",result,reason};return false;
}
}
void GlobalSlotTable::Erase(uint32_t slot) {
  auto& entry=entries_[slot];cache_.erase(entry.key);entry={};free_.push_back(slot);
}
bool GlobalSlotTable::Acquire(const std::shared_ptr<void>& owner,uint64_t offset,uint64_t size,
                             uint64_t serial,uint32_t& slot,bool& fresh,Error& e) {
  if(!owner || !size || serial<=completed_ || serial<latest_)
    return Fail(e,"Invalid owner, range or recording serial");
  const Key key{reinterpret_cast<uintptr_t>(owner.get()),offset,size};
  if(auto found=cache_.find(key);found!=cache_.end()) {
    auto& entry=entries_[found->second];
    if(!entry.owner.expired()) {
      if(entry.owner.owner_before(owner) || owner.owner_before(entry.owner))
        return Fail(e,"Resource address has conflicting owners");
      if(entry.serial!=serial) pending_[serial].push_back(owner);
      entry.serial=serial;latest_=serial;slot=found->second;fresh=false;e={};return true;
    }
    Erase(found->second);
  }
  if(free_.empty() && entries_.size()==capacity_) {
    // Capacity pressure can evict a live cached resource only after every
    // command referring to its descriptor has completed.
    // Continue past the slots replaced by earlier draws. Restarting at zero
    // rescans their now-pending prefix and makes a full turnover quadratic.
    for(uint32_t examined=0;examined<entries_.size();++examined) {
      const uint32_t i=eviction_cursor_++;
      if(eviction_cursor_==entries_.size())eviction_cursor_=0;
      ++eviction_candidates_examined_;
      if(entries_[i].used && entries_[i].serial<=completed_) {Erase(i);break;}
    }
  }
  if(free_.empty() && entries_.size()>=capacity_)
    return Fail(e,"Global descriptor capacity exhausted by pending resources",VK_ERROR_OUT_OF_POOL_MEMORY);
  uint32_t next;
  if(free_.empty()) {next=uint32_t(entries_.size());entries_.push_back({});}
  else {next=free_.back();free_.pop_back();}
  entries_[next]={key,owner,serial,true};cache_[key]=next;pending_[serial].push_back(owner);
  latest_=serial;slot=next;fresh=true;e={};return true;
}
void GlobalSlotTable::Retire(uint64_t serial) {
  completed_=std::max(completed_,serial);
  for(auto it=pending_.begin();it!=pending_.end() && it->first<=completed_;) it=pending_.erase(it);
  for(uint32_t i=0;i<entries_.size();++i)
    if(entries_[i].used && entries_[i].serial<=completed_ && entries_[i].owner.expired()) Erase(i);
}
GlobalDescriptorHeap::~GlobalDescriptorHeap() {
  if(!c_.device)return;
  c_.f.vkDeviceWaitIdle(c_.device);Retire(UINT64_MAX);
  if(pool_)c_.f.vkDestroyDescriptorPool(c_.device,pool_,nullptr);
  if(layout_)c_.f.vkDestroyDescriptorSetLayout(c_.device,layout_,nullptr);
}
bool ValidateGlobalHeapCapacities(const GlobalHeapCapacities& capacity,const VkPhysicalDeviceDescriptorIndexingProperties& properties,Error& e) {
  if(!capacity.images_2d || !capacity.buffers || !capacity.samplers || !capacity.images_3d || !capacity.images_cube ||
     capacity.images_2d>32768 || capacity.images_3d>32768 || capacity.images_cube>32768)
    return Fail(e,"Invalid global heap capacities or packed texture index overflow");
  const uint64_t images=uint64_t(capacity.images_2d)+capacity.images_3d+capacity.images_cube;
  const uint64_t resources=images+capacity.buffers,total=resources+capacity.samplers;
  if(images>properties.maxDescriptorSetUpdateAfterBindSampledImages || images>properties.maxPerStageDescriptorUpdateAfterBindSampledImages ||
     capacity.buffers>properties.maxDescriptorSetUpdateAfterBindStorageBuffers || capacity.buffers>properties.maxPerStageDescriptorUpdateAfterBindStorageBuffers ||
     capacity.samplers>properties.maxDescriptorSetUpdateAfterBindSamplers || capacity.samplers>properties.maxPerStageDescriptorUpdateAfterBindSamplers ||
     resources>properties.maxPerStageUpdateAfterBindResources || total>properties.maxUpdateAfterBindDescriptorsInAllPools)
    return Fail(e,"Global descriptor heap exceeds device limits",VK_ERROR_FEATURE_NOT_PRESENT);
  e={};return true;
}
bool GlobalDescriptorHeap::Initialize(const VkPhysicalDeviceDescriptorIndexingFeatures& enabled,
                                      const GlobalHeapCapacities& capacity,Error& e) {
  if(layout_ || !c_.device)return Fail(e,"Missing device or heap already initialized");
  if(!c_.enabled_features.shaderStorageBufferArrayDynamicIndexing || !enabled.runtimeDescriptorArray ||
     !enabled.shaderSampledImageArrayNonUniformIndexing || !enabled.shaderStorageBufferArrayNonUniformIndexing ||
     !enabled.descriptorBindingPartiallyBound || !enabled.descriptorBindingSampledImageUpdateAfterBind ||
     !enabled.descriptorBindingStorageBufferUpdateAfterBind)
    return Fail(e,"Required global descriptor features were not enabled",VK_ERROR_FEATURE_NOT_PRESENT);
  std::array<uint32_t,5> counts{capacity.images_2d,capacity.buffers,capacity.samplers,capacity.images_3d,capacity.images_cube};
  if(std::any_of(counts.begin(),counts.end(),[](uint32_t n){return !n;}) ||
     capacity.images_2d>32768 || capacity.images_3d>32768 || capacity.images_cube>32768)
    return Fail(e,"Invalid global heap capacities or packed texture index overflow");
  VkPhysicalDeviceDescriptorIndexingProperties properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
  VkPhysicalDeviceProperties2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};query.pNext=&properties;
  c_.f.vkGetPhysicalDeviceProperties2(c_.physical,&query);
  const uint64_t images=uint64_t(capacity.images_2d)+capacity.images_3d+capacity.images_cube;
  if(!ValidateGlobalHeapCapacities(capacity,properties,e))return false;
  constexpr auto stages=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
  constexpr std::array<VkDescriptorType,5> types{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    VK_DESCRIPTOR_TYPE_SAMPLER,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE};
  std::array<VkDescriptorSetLayoutBinding,5> bindings;
  std::array<VkDescriptorBindingFlags,5> flags;
  for(uint32_t i=0;i<5;++i) {
    bindings[i]={i,types[i],counts[i],stages,nullptr};
    flags[i]=VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT|VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
  }
  VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
  binding_flags.bindingCount=5;binding_flags.pBindingFlags=flags.data();
  VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};info.pNext=&binding_flags;
  info.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;info.bindingCount=5;info.pBindings=bindings.data();
  auto support=reinterpret_cast<PFN_vkGetDescriptorSetLayoutSupport>(c_.f.vkGetDeviceProcAddr(c_.device,"vkGetDescriptorSetLayoutSupport"));
  if(!support)return Fail(e,"Descriptor layout support query unavailable");
  VkDescriptorSetLayoutSupport supported{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT};support(c_.device,&info,&supported);
  if(!supported.supported)return Fail(e,"Global descriptor layout unsupported",VK_ERROR_FEATURE_NOT_PRESENT);
  VkDescriptorSetLayout layout=VK_NULL_HANDLE;
  if(!Check(c_.f.vkCreateDescriptorSetLayout(c_.device,&info,nullptr,&layout),"Create global descriptor layout",e))return false;
  VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,uint32_t(images)},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,capacity.buffers},{VK_DESCRIPTOR_TYPE_SAMPLER,capacity.samplers}};
  VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool_info.flags=VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  pool_info.maxSets=1;pool_info.poolSizeCount=3;pool_info.pPoolSizes=sizes;
  VkDescriptorPool pool=VK_NULL_HANDLE;
  if(!Check(c_.f.vkCreateDescriptorPool(c_.device,&pool_info,nullptr,&pool),"Create global descriptor pool",e)) {
    c_.f.vkDestroyDescriptorSetLayout(c_.device,layout,nullptr);return false;
  }
  VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool=pool;
  allocation.descriptorSetCount=1;allocation.pSetLayouts=&layout;
  VkDescriptorSet set=VK_NULL_HANDLE;
  if(!Check(c_.f.vkAllocateDescriptorSets(c_.device,&allocation,&set),"Allocate global descriptor set",e)) {
    c_.f.vkDestroyDescriptorPool(c_.device,pool,nullptr);c_.f.vkDestroyDescriptorSetLayout(c_.device,layout,nullptr);return false;
  }
  for(uint32_t i=0;i<5;++i)slots_[i]=std::make_unique<GlobalSlotTable>(counts[i]);
  layout_=layout;pool_=pool;set_=set;e={};return true;
}
bool GlobalDescriptorHeap::Buffer(const std::shared_ptr<BufferResource>& resource,uint64_t serial,uint32_t& slot,Error& e) {
  return BufferRange(resource,0,resource?resource->size:0,serial,slot,e);
}
bool GlobalDescriptorHeap::BufferRange(const std::shared_ptr<BufferResource>& resource,VkDeviceSize offset,VkDeviceSize size,
                                      uint64_t serial,uint32_t& slot,Error& e) {
  if(!set_ || !resource || resource->context!=&c_ || !resource->handle || !size ||
     offset>resource->size || size>resource->size-offset || size>c_.properties.limits.maxStorageBufferRange ||
     offset>UINT64_MAX-resource->offset ||
     (resource->offset+offset)%std::max(VkDeviceSize(1),c_.properties.limits.minStorageBufferOffsetAlignment))
    return Fail(e,"Invalid global buffer range or alignment");
  bool fresh=false;uint32_t selected;
  if(!slots_[1]->Acquire(resource,offset,size,serial,selected,fresh,e))return false;
  if(fresh) {
    VkDescriptorBufferInfo buffer{resource->handle,resource->offset+offset,size};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set_;write.dstBinding=1;
    write.dstArrayElement=selected;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&buffer;
    c_.f.vkUpdateDescriptorSets(c_.device,1,&write,0,nullptr);++writes_;
  }
  slot=selected;e={};return true;
}
bool GlobalDescriptorHeap::Image(const std::shared_ptr<TextureResource>& resource,uint64_t serial,uint32_t& slot,Error& e) {
  if(!set_ || !resource || resource->context!=&c_ || !resource->view)return Fail(e,"Missing global image view");
  uint32_t binding;
  switch(resource->view_type) {
    case VK_IMAGE_VIEW_TYPE_2D:binding=0;break;case VK_IMAGE_VIEW_TYPE_3D:binding=3;break;case VK_IMAGE_VIEW_TYPE_CUBE:binding=4;break;
    default:return Fail(e,"Unsupported global image dimension");
  }
  bool fresh=false;uint32_t selected;
  if(!slots_[binding]->Acquire(resource,0,1,serial,selected,fresh,e))return false;
  if(fresh) {
    VkDescriptorImageInfo image{VK_NULL_HANDLE,resource->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set_;write.dstBinding=binding;
    write.dstArrayElement=selected;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.pImageInfo=&image;
    c_.f.vkUpdateDescriptorSets(c_.device,1,&write,0,nullptr);++writes_;
  }
  slot=selected;e={};return true;
}
bool GlobalDescriptorHeap::Sampler(const std::shared_ptr<SamplerResource>& resource,uint64_t serial,uint32_t& slot,Error& e) {
  if(!set_ || !resource || resource->context!=&c_ || !resource->handle)return Fail(e,"Missing global sampler");
  bool fresh=false;uint32_t selected;
  if(!slots_[2]->Acquire(resource,0,1,serial,selected,fresh,e))return false;
  if(fresh) {
    VkDescriptorImageInfo image{resource->handle,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set_;write.dstBinding=2;
    write.dstArrayElement=selected;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;write.pImageInfo=&image;
    c_.f.vkUpdateDescriptorSets(c_.device,1,&write,0,nullptr);++writes_;
  }
  slot=selected;e={};return true;
}
void GlobalDescriptorHeap::Retire(uint64_t serial) {for(auto& slots:slots_)if(slots)slots->Retire(serial);}
}
