#include "descriptor_sets.h"
#include "device_requirements.h"
#include <algorithm>
namespace superman_returns::graphics::vulkan {
DescriptorPage::~DescriptorPage() {if(pool) context->f.vkDestroyDescriptorPool(context->device,pool,nullptr);}
SamplerResource::~SamplerResource() {if(handle) context->f.vkDestroySampler(context->device,handle,nullptr);}
DescriptorStore::~DescriptorStore() {
  if(c_.device) c_.f.vkDeviceWaitIdle(c_.device);
  pending_.Retire(UINT64_MAX);pages_.clear();samplers_.clear();
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
std::shared_ptr<DescriptorDraw> DescriptorStore::Prepare(const DrawBindings& bindings,
    const std::array<std::array<uint32_t,6>,32>& fetch,ResourceStore& store,uint64_t serial,Error& e) {
  if(!layouts_[0] || serial<=completed_ || store.CurrentSerial()!=serial) {
    e={"Draw descriptors",VK_ERROR_INITIALIZATION_FAILED,"Missing layouts or recording submission"};return {};
  }
  auto draw=std::make_shared<DescriptorDraw>();
  std::array<VkDescriptorBufferInfo,3> constant_info{};
  std::array<VkDescriptorBufferInfo,32> vertex_info{};
  std::array<std::array<VkDescriptorImageInfo,32>,3> texture_info{};
  std::array<VkDescriptorImageInfo,32> sampler_info{};
  std::array<std::span<const std::byte>,3> bytes{std::as_bytes(std::span(bindings.constants.vs)),std::as_bytes(std::span(bindings.constants.ps)),std::as_bytes(std::span(bindings.constants.shared))};
  for(uint32_t i=0;i<3;++i) {
    uint64_t id=0xc000000000000000ull|++constant_serial_;
    if(!store.UploadHostBuffer(id,bytes[i],1,e)) return {};
    auto buffer=store.Buffer(id,e);store.ForgetBuffer(id);
    if(!buffer) return {};
    constant_info[i]={buffer->handle,0,4096};draw->resources.push_back(buffer);
  }
  for(uint32_t slot=0;slot<32;++slot) {
    if((bindings.texture_indices[slot]&0x7fffu)>=32 || bindings.sampler_indices[slot]!=slot) {
      e={"Draw descriptors",VK_ERROR_INITIALIZATION_FAILED,"Descriptor index was not remapped into the draw arrays"};return {};
    }
    auto buffer=store.Buffer(bindings.vertex_buffers[slot],e);if(!buffer) return {};
    vertex_info[slot]={buffer->handle,0,buffer->size};draw->resources.push_back(buffer);
    for(uint32_t dimension=0;dimension<3;++dimension) {
      auto texture=store.Texture(bindings.textures[dimension][slot],e);if(!texture) return {};
      auto expected=dimension==0?VK_IMAGE_VIEW_TYPE_2D:(dimension==1?VK_IMAGE_VIEW_TYPE_3D:VK_IMAGE_VIEW_TYPE_CUBE);
      if(texture->view_type!=expected) {e={"Texture descriptors",VK_ERROR_INITIALIZATION_FAILED,"Texture view dimension mismatches shader array"};return {};}
      texture_info[dimension][slot]={VK_NULL_HANDLE,texture->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};draw->resources.push_back(texture);
    }
    auto sampler=Sampler(fetch[slot],e);if(!sampler) return {};
    sampler_info[slot]={sampler->handle,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};draw->resources.push_back(sampler);
    if((fetch[slot][0]&3)==2 && (((fetch[slot][3]>>19)&3)==1 || ((fetch[slot][3]>>21)&3)==1 || ((fetch[slot][3]>>25)&7)>1)) {
      uint32_t dimension=(fetch[slot][5]>>9)&3;dimension=dimension?dimension-1:0;
      auto texture=store.Texture(bindings.textures[dimension][bindings.texture_indices[slot]&0x7fffu],e);if(!texture) return {};
      VkFormatProperties properties{};c_.f.vkGetPhysicalDeviceFormatProperties(c_.physical,texture->format,&properties);
      if(!(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        e={"Texture filtering",VK_ERROR_FORMAT_NOT_SUPPORTED,"Guest sampler requests linear filtering of an unsupported format"};return {};
      }
    }
  }
  constexpr uint32_t draws_per_page=128;
  auto& pages=pages_[serial];
  if(pages.empty() || pages.back()->draws>=draws_per_page) {
    auto page=std::make_shared<DescriptorPage>();page->context=&c_;
    VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,35*draws_per_page},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,96*draws_per_page},{VK_DESCRIPTOR_TYPE_SAMPLER,32*draws_per_page}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=4*draws_per_page;pool.poolSizeCount=3;pool.pPoolSizes=sizes;
    if(!Check(c_.f.vkCreateDescriptorPool(c_.device,&pool,nullptr,&page->pool),"Create draw descriptor page",e)) return {};
    pages.push_back(page);
  }
  draw->page=pages.back();
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=draw->page->pool;allocate.descriptorSetCount=4;allocate.pSetLayouts=layouts_.data();
  if(!Check(c_.f.vkAllocateDescriptorSets(c_.device,&allocate,draw->sets.data()),"Allocate draw descriptor sets",e)) return {};
  ++draw->page->draws;
  std::vector<VkWriteDescriptorSet> writes;
  auto write=[&](uint32_t set,uint32_t binding,uint32_t count,VkDescriptorType type,const VkDescriptorBufferInfo* buffers,const VkDescriptorImageInfo* images) {
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=draw->sets[set];w.dstBinding=binding;w.descriptorCount=count;w.descriptorType=type;w.pBufferInfo=buffers;w.pImageInfo=images;writes.push_back(w);
  };
  for(uint32_t i=0;i<3;++i) write(0,i,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,&constant_info[i],nullptr);
  for(uint32_t dim=0;dim<3;++dim) write(1,dim,32,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,nullptr,texture_info[dim].data());
  write(2,0,32,VK_DESCRIPTOR_TYPE_SAMPLER,nullptr,sampler_info.data());
  write(3,0,32,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,vertex_info.data(),nullptr);
  c_.f.vkUpdateDescriptorSets(c_.device,uint32_t(writes.size()),writes.data(),0,nullptr);
  pending_.Keep(serial,draw);e={};return draw;
}
void DescriptorStore::Retire(uint64_t serial) {completed_=std::max(completed_,serial);pending_.Retire(completed_);pages_.erase(pages_.begin(),pages_.upper_bound(completed_));}
} // namespace superman_returns::graphics::vulkan
