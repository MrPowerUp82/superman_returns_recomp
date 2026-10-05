#include "descriptors.h"
#include <cstring>
#include <map>
#include <algorithm>
namespace superman_returns::graphics::vulkan {
guest::ResourceId TextureResourceId(std::span<const uint32_t,6> words) {
  // IDs identify immutable fetch views, not an API's global descriptor index.
  uint64_t hash=14695981039346656037ull;
  for(auto word:words) for(uint32_t b=0;b<4;++b) {hash^=(word>>(b*8))&255;hash*=1099511628211ull;}
  return hash&~InlineBufferBase;
}
bool RemapTextureBindings(std::span<const TextureBindingRequest> requests,DrawBindings& out,Error& error) {
  if(requests.size()>32) {error={"Descriptor remap",VK_ERROR_FEATURE_NOT_PRESENT,"More than 32 texture slots"};return false;}
  DrawBindings next=out;
  for(uint32_t dim=0;dim<3;++dim) next.textures[dim].fill(DummyTexture(TextureDimension(dim)));
  next.texture_indices.fill(31);
  std::array<uint32_t,3> count{};
  std::array<bool,32> used{};
  for(const auto& r:requests) {
    auto dim=uint32_t(r.dimension);
    if(r.slot>=32 || dim>=3 || used[r.slot] || !r.resource || r.resource>=DummyBuffer) {
      error={"Descriptor remap",VK_ERROR_INITIALIZATION_FAILED,"Invalid or duplicate texture slot/resource/dimension"};return false;
    }
    used[r.slot]=true;
    uint32_t index=count[dim]++;
    next.textures[dim][index]=r.resource;
    next.texture_indices[r.slot]=(r.flags&~0x7fffu)|index;
  }
  out=std::move(next);error={};return true;
}
DrawBindings BuildBindings(const guest::DrawPacket& draw,Error& error) {
  DrawBindings out;out.original_constants=&draw.constants;
  std::memcpy(out.shared_constants.data(), draw.constants.shared.data(), 4096);
  out.vertex_buffers.fill(DummyBuffer);
  std::vector<TextureBindingRequest> requests;
  for(uint32_t slot=0;slot<32;++slot) {
    const auto& fetch=draw.texture_fetch[slot];
    if((fetch[0]&3)!=2) continue;
    uint32_t dim=(fetch[5]>>9)&3;
    if(dim==0) dim=1; // SDK prepares 1D as a one-row 2D image.
    uint32_t gamma=0;
    if(((fetch[0]>>2)&3)==3 && ((fetch[0]>>4)&3)==3 && ((fetch[0]>>6)&3)==3) gamma=0x80000000u;
    requests.push_back({slot,TextureResourceId(fetch),TextureDimension(dim-1),gamma});
  }
  if(!RemapTextureBindings(requests,out,error)) return {};
  std::map<uint32_t,uint32_t> streams;
  if(draw.inline_vertices) {
    out.vertex_buffers[0]=InlineBufferBase|draw.command_serial;
  } else {
    for(const auto& stream:draw.streams) {
      if(stream.stream>=32 || !stream.update.plan.key || streams.contains(stream.stream) || streams.size()>=32) {
        error={"Vertex remap",VK_ERROR_INITIALIZATION_FAILED,"Invalid or duplicate vertex stream"};return {};
      }
      uint32_t index=uint32_t(streams.size());streams.emplace(stream.stream,index);
      out.vertex_buffers[index]=stream.update.plan.key;
    }
  }
  for(uint32_t i=0;i<draw.vertex_fetch.size();++i) {
    auto meta=draw.vertex_fetch[i];
    if(meta.type) {
      if(draw.inline_vertices) meta.buffer=0;
      else {
        auto found=streams.find(meta.buffer);
        if(found==streams.end()) {error={"Vertex remap",VK_ERROR_INITIALIZATION_FAILED,"Vertex metadata references missing stream"};return {};}
        meta.buffer=found->second;
      }
    } else {meta={31,0,0,0};}
    std::memcpy(out.shared_constants.data()+512+i*16,&meta,16);
  }
  for(uint32_t slot=0;slot<32;++slot) {
    out.sampler_indices[slot]=slot;
    std::memcpy(out.shared_constants.data()+slot*4,&out.texture_indices[slot],4);
    std::memcpy(out.shared_constants.data()+128+slot*4,&out.sampler_indices[slot],4);
  }
  error={};return out;
}
bool PlanSampler(std::span<const uint32_t,6> fetch,const VkPhysicalDeviceFeatures& features,
                 const VkPhysicalDeviceLimits& limits,bool mirror_clamp,VkSamplerCreateInfo& out,Error& e) {
  VkSamplerCreateInfo next{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  auto address=[&](uint32_t mode,VkSamplerAddressMode& result) {
    switch(mode) {
    case 0:result=VK_SAMPLER_ADDRESS_MODE_REPEAT;break;
    case 1:result=VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;break;
    case 3:case 5:case 7:
      if(!mirror_clamp) {e={"Sampler",VK_ERROR_FEATURE_NOT_PRESENT,"Mirror-clamp sampler extension was not enabled"};return false;}
      result=VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;break;
    case 6:result=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;break;
    default:result=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;break;
    }
    return true;
  };
  if(!address((fetch[0]>>10)&7,next.addressModeU) || !address((fetch[0]>>13)&7,next.addressModeV) || !address((fetch[0]>>16)&7,next.addressModeW)) return false;
  next.magFilter=((fetch[3]>>19)&3)==1?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
  next.minFilter=((fetch[3]>>21)&3)==1?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
  next.mipmapMode=((fetch[3]>>23)&3)==1?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;
  uint32_t aniso=(fetch[3]>>25)&7;
  if(aniso>1) {
    if(!features.samplerAnisotropy || limits.maxSamplerAnisotropy<1) {
      e={"Sampler",VK_ERROR_FEATURE_NOT_PRESENT,"Guest sampler needs enabled anisotropy"};return false;
    }
    next.anisotropyEnable=VK_TRUE;next.maxAnisotropy=std::min(float(std::min(16u,1u<<(aniso-1))),limits.maxSamplerAnisotropy);
    next.magFilter=next.minFilter=VK_FILTER_LINEAR;next.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
  }
  // Match the reference's original-scale sampler. Fetch LOD bias is emitted
  // by the guest shader; adding it here would apply the bias twice.
  next.minLod=0;next.maxLod=VK_LOD_CLAMP_NONE;
  next.compareOp=VK_COMPARE_OP_NEVER;next.borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
  out=next;e={};return true;
}
} // namespace superman_returns::graphics::vulkan
