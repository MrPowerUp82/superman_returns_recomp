#include "global_draw_bindings.h"
#include <algorithm>
#include <cstring>
#include <chrono>
#include <cstdlib>
namespace superman_returns::graphics::vulkan {
namespace {
bool Fail(Error& e,const char* why) {e={"Global draw bindings",VK_ERROR_INITIALIZATION_FAILED,why};return false;}
constexpr uint32_t ConstantBytes=8192+512,StreamBytes=33*16,PageDraws=128;
bool FlushPart(ResourceStore& store,const TransientSlice& slice,uint32_t offset,uint32_t size,Error& e) {
  return store.FlushTransient({slice.chunk,slice.offset+offset,size,slice.data+offset},e);
}
}
bool GlobalDrawBindings::Initialize(Error& e) {
  // Diagnostic A/B switch, read once, outside the draw hot path.
  const auto* value=std::getenv("SR_VULKAN_TEXTURE_BINDING_CACHE");
  texture_reuse_=!(value && value[0]=='0');
  return heap_.Initialize(c_.descriptor_indexing_features,{},e);
}
bool PlanGlobalVertexLayout(const guest::DrawPacket& draw,GlobalVertexLayout& out,Error& e) {
  GlobalVertexLayout next;
  for(auto& a:next.attributes)a={32,0,0,0};
  auto attribute=[&](uint32_t semantic,uint32_t stream,uint32_t offset,uint32_t type) {
    if(!type)return true;
    if(stream>=32)return Fail(e,"Vertex attribute stream exceeds 31");
    next.attributes[semantic]={stream,offset,0,type};next.streams_used|=1u<<stream;return true;
  };
  if(!draw.attributes.empty()) {
    for(const auto& a:draw.attributes)if(a.usage<14 && a.index<16)
      if(!attribute(a.usage*16+a.index,draw.inline_vertices?0:a.stream,a.offset,a.type))return false;
  } else {
    // Many draws expose the full 224-entry legacy fetch table. Index the
    // stream bases once instead of linearly searching draw.streams per
    // attribute. On duplicates, the first stream wins (the old find_if rule).
    std::array<uint32_t,32> stream_bases{};
    std::array<bool,32> stream_present{};
    if(!draw.inline_vertices)for(const auto& s:draw.streams)if(s.stream<32 && !stream_present[s.stream]) {
      stream_bases[s.stream]=s.offset;stream_present[s.stream]=true;
    }
    for(uint32_t i=0;i<224;++i) {
      const auto& meta=draw.vertex_fetch[i];if(!meta.type)continue;
      uint32_t stream=draw.inline_vertices?0:meta.buffer,base=0;
      if(!draw.inline_vertices) {
        if(stream<32) {
          if(!stream_present[stream] || meta.offset<stream_bases[stream])return Fail(e,"Vertex metadata references an invalid stream offset");
          base=stream_bases[stream];
        } else {
          // Preserve legacy diagnostics for out-of-range stream IDs.
          auto found=std::find_if(draw.streams.begin(),draw.streams.end(),[&](const auto& s){return s.stream==stream;});
          if(found==draw.streams.end() || meta.offset<found->offset)return Fail(e,"Vertex metadata references an invalid stream offset");
          base=found->offset;
        }
      }
      if(!attribute(i,stream,meta.offset-base,meta.type))return false;
    }
  }
  out=next;e={};return true;
}
bool GlobalVertexLayoutKey(const guest::DrawPacket& draw,std::vector<uint32_t>& key,Error& e) {
  key.clear();key.push_back(draw.inline_vertices);key.push_back(!draw.attributes.empty());
  if(!draw.attributes.empty())for(const auto& a:draw.attributes) {
    if(a.usage>=14 || a.index>=16)continue;
    if(!draw.inline_vertices && a.stream>=32)return Fail(e,"Vertex attribute stream exceeds 31");
    key.insert(key.end(),{draw.inline_vertices?0:a.stream,a.offset,a.type,a.usage,a.index});
  } else {
    GlobalVertexLayout layout;if(!PlanGlobalVertexLayout(draw,layout,e))return false;
    for(const auto& a:layout.attributes)key.insert(key.end(),a.begin(),a.end());
  }
  e={};return true;
}
std::shared_ptr<SamplerResource> GlobalDrawBindings::Sampler(std::span<const uint32_t,6> fetch,Error& e) {
  std::array<uint32_t,2> key{fetch[0]&0x7fc00u,fetch[3]&0xff80000u};
  if(auto found=samplers_.find(key);found!=samplers_.end())return found->second;
  VkSamplerCreateInfo info{};
  if(!PlanSampler(fetch,c_.enabled_features,c_.properties.limits,c_.mirror_clamp_enabled,info,e))return {};
  if(samplers_.size()>=c_.properties.limits.maxSamplerAllocationCount) {Fail(e,"Sampler allocation limit exhausted");return {};}
  auto sampler=std::make_shared<SamplerResource>();sampler->context=&c_;
  if(!Check(c_.f.vkCreateSampler(c_.device,&info,nullptr,&sampler->handle),"Create global game sampler",e))return {};
  samplers_.emplace(key,sampler);return sampler;
}
bool GlobalDrawBindings::BufferSlot(const std::shared_ptr<BufferResource>& resource,uint64_t serial,uint32_t& slot,Error& e) {
  if(auto it=buffer_slots_.find(resource.get());it!=buffer_slots_.end()) {slot=it->second;e={};return true;}
  ++stats_.resource_acquires;if(!heap_.Buffer(resource,serial,slot,e))return false;
  buffer_slots_.emplace(resource.get(),slot);return true;
}
bool GlobalDrawBindings::ImageSlot(const std::shared_ptr<TextureResource>& resource,uint64_t serial,uint32_t& slot,Error& e) {
  if(auto it=image_slots_.find(resource.get());it!=image_slots_.end()) {slot=it->second;e={};return true;}
  ++stats_.resource_acquires;if(!heap_.Image(resource,serial,slot,e))return false;
  image_slots_.emplace(resource.get(),slot);return true;
}
bool GlobalDrawBindings::SamplerSlot(const std::shared_ptr<SamplerResource>& resource,uint64_t serial,uint32_t& slot,Error& e) {
  if(auto it=sampler_slots_.find(resource.get());it!=sampler_slots_.end()) {slot=it->second;e={};return true;}
  ++stats_.resource_acquires;if(!heap_.Sampler(resource,serial,slot,e))return false;
  sampler_slots_.emplace(resource.get(),slot);return true;
}
void GlobalDrawBindings::Retire(uint64_t serial) {
  heap_.Retire(serial);completed_=std::max(completed_,serial);
  if(completed_>=serial_) {buffer_slots_.clear();image_slots_.clear();sampler_slots_.clear();}
}
bool GlobalDrawBindings::BeginSubmission(ResourceStore& store,uint64_t serial,Error& e) {
  if(serial<=serial_ || serial<=completed_ || store.CurrentSerial()!=serial)return Fail(e,"Invalid global draw submission");
  layouts_.clear();pages_.clear();serial_=serial;
  texture_bindings_={};
  buffer_slots_.clear();image_slots_.clear();sampler_slots_.clear();
  // Bind all three dimension dummies before real images. Refreshing these
  // entries each serial protects their shared slot zero from capacity eviction.
  for(uint32_t dim=0;dim<3;++dim) {
    auto texture=store.Texture(DummyTexture(TextureDimension(dim)),e);uint32_t slot;
    if(!texture || !ImageSlot(texture,serial,slot,e))return false;
    if(slot!=0)return Fail(e,"Global dummy image must occupy slot zero in every dimension");
  }
  auto buffer=store.Buffer(DummyBuffer,e);if(!buffer || !BufferSlot(buffer,serial,dummy_buffer_,e))return false;
  static constexpr std::array<uint32_t,6> no_texture{};
  auto sampler=Sampler(no_texture,e);uint32_t slot;
  if(!sampler || !SamplerSlot(sampler,serial,slot,e))return false;
  if(slot!=0)return Fail(e,"Global dummy sampler must occupy slot zero");
  e={};return true;
}
bool GlobalDrawBindings::Prepare(const guest::DrawPacket& draw,ResourceStore& store,uint64_t serial,GlobalDrawBinding& out,Error& e) {
  using Clock=std::chrono::steady_clock;
  auto phase_start=profiling_?Clock::now():Clock::time_point{};
  auto phase=[&](GlobalBindingStats::Phase index) {
    if(!profiling_)return;
    auto now=Clock::now();
    stats_.ns[index]+=std::chrono::duration_cast<std::chrono::nanoseconds>(now-phase_start).count();
    phase_start=now;
  };
  if(serial!=serial_ || serial<=completed_ || store.CurrentSerial()!=serial)return Fail(e,"Draw differs from global recording submission");
  if(!GlobalVertexLayoutKey(draw,layout_key_,e))return false;
  auto found=layouts_.find(layout_key_);
  if(found==layouts_.end()) {
    LayoutEntry layout;if(!PlanGlobalVertexLayout(draw,layout.plan,e) || !store.MapTransient(sizeof(layout.plan.attributes),layout.slice,e))return false;
    ++stats_.transient_allocations;
    std::memcpy(layout.slice.data,layout.plan.attributes.data(),sizeof(layout.plan.attributes));
    if(!store.FlushTransient(layout.slice,e))return false;
    found=layouts_.emplace(layout_key_,std::move(layout)).first;
  }
  const auto& layout=found->second;
  phase(GlobalBindingStats::kLayout);
  std::array<std::array<uint32_t,4>,33> streams{};for(auto& s:streams)s[0]=dummy_buffer_;
  uint32_t available=0;
  if(draw.inline_vertices) {
    auto b=store.Buffer(InlineBufferBase|draw.command_serial,e);uint32_t slot;
    if(!b || !BufferSlot(b,serial,slot,e))return false;
    streams[0]={slot,0,draw.inline_stride,0};available=1;
  } else for(const auto& stream:draw.streams) {
    if(stream.stream>=32 || (available&(1u<<stream.stream)))return Fail(e,"Invalid or duplicate global vertex stream");
    auto b=store.Buffer(stream.update.plan.key,e);uint32_t slot;
    if(!b)return false;
    if(stream.offset>b->size)return Fail(e,"Global vertex stream offset exceeds buffer");
    if(!BufferSlot(b,serial,slot,e))return false;
    streams[stream.stream]={slot,stream.offset,stream.stride,0};available|=1u<<stream.stream;
  }
  if(layout.plan.streams_used&~available)return Fail(e,"Global vertex layout references a missing stream");
  phase(GlobalBindingStats::kStreams);
  // One allocation and table binding for a block of draws, rather than two
  // arena allocations and three chunk-slot acquisitions for every draw.
  auto& pages=pages_[layout.slice.chunk.get()];
  if(pages.empty() || pages.back().count==PageDraws) {
    const VkDeviceSize alignment=std::max<VkDeviceSize>(16,c_.properties.limits.minStorageBufferOffsetAlignment);
    const VkDeviceSize table_offset=(VkDeviceSize(ConstantBytes+StreamBytes)*PageDraws+alignment-1)/alignment*alignment;
    TransientSlice block;if(!store.MapTransient(table_offset+16+PageDraws*16,block,e))return false;
    ++stats_.transient_allocations;
    Page page;
    page.constants={block.chunk,block.offset,ConstantBytes*PageDraws,block.data};
    page.streams={block.chunk,block.offset+ConstantBytes*PageDraws,StreamBytes*PageDraws,block.data+ConstantBytes*PageDraws};
    page.table={block.chunk,block.offset+table_offset,16+PageDraws*16,block.data+table_offset};
    uint32_t data_slot,layout_slot;
    if(!BufferSlot(block.chunk,serial,data_slot,e) || !BufferSlot(layout.slice.chunk,serial,layout_slot,e))return false;
    const uint32_t header[]{data_slot,layout_slot,data_slot,0};std::memcpy(page.table.data,header,16);
    ++stats_.resource_acquires;
    if(!FlushPart(store,page.table,0,16,e) || !heap_.BufferRange(page.table.chunk,page.table.offset,page.table.size,serial,page.slot,e))return false;
    pages.push_back(std::move(page));++stats_.pages;
  }
  auto& page=pages.back();
  TransientSlice constants{page.constants.chunk,page.constants.offset+page.count*ConstantBytes,ConstantBytes,page.constants.data+page.count*ConstantBytes};
  TransientSlice stream_data{page.streams.chunk,page.streams.offset+page.count*StreamBytes,StreamBytes,page.streams.data+page.count*StreamBytes};
  phase(GlobalBindingStats::kPage);
  std::memcpy(constants.data,draw.constants.vs.data(),4096);
  std::memcpy(constants.data+4096,draw.constants.ps.data(),4096);
  std::memcpy(constants.data+8192+256,draw.constants.shared.data()+256,256);
  phase(GlobalBindingStats::kConstants);
  std::array<uint32_t,32> textures{},samplers{};
  for(uint32_t slot=0;slot<32;++slot)if((draw.texture_fetch[slot][0]&3)==2) {
    const auto& fetch=draw.texture_fetch[slot];auto& recent=texture_bindings_[slot];
    int cached=-1;
    if(texture_reuse_)for(int i=0;i<2;++i)if(recent[i].resource && recent[i].fetch==fetch){cached=i;break;}
    const auto id=cached>=0?recent[cached].id:TextureResourceId(fetch);
    const auto* resource=store.FindTexture(id);
    if(!resource)return Fail(e,"Texture has no upload or resolve");
    const auto& texture=*resource;
    if(cached>=0 && recent[cached].resource==texture.get()) {
      textures[slot]=recent[cached].image;samplers[slot]=recent[cached].sampler;
      if(cached)std::swap(recent[0],recent[1]);
      ++stats_.texture_reuses;continue;
    }
    ++stats_.texture_preparations;uint32_t image_index,sampler_index;
    // ImageSlot retains the version until this serial retires, so both the
    // raw cache identity and the GPU descriptor remain valid for every hit.
    if(!ImageSlot(texture,serial,image_index,e))return false;
    uint32_t dimension=(fetch[5]>>9)&3;
    VkImageViewType expected=dimension<=1?VK_IMAGE_VIEW_TYPE_2D:(dimension==2?VK_IMAGE_VIEW_TYPE_3D:VK_IMAGE_VIEW_TYPE_CUBE);
    if(texture->view_type!=expected)return Fail(e,"Captured texture dimension differs from its view");
    auto sampler=Sampler(fetch,e);if(!sampler || !SamplerSlot(sampler,serial,sampler_index,e))return false;
    if(((fetch[3]>>19)&3)==1 || ((fetch[3]>>21)&3)==1 || ((fetch[3]>>25)&7)>1) {
      auto format=filtering_.find(texture->format);
      if(format==filtering_.end()) {VkFormatProperties properties{};c_.f.vkGetPhysicalDeviceFormatProperties(c_.physical,texture->format,&properties);format=filtering_.emplace(texture->format,properties.optimalTilingFeatures).first;}
      if(!(format->second&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))return Fail(e,"Global texture format cannot be filtered linearly");
    }
    uint32_t gamma=(((fetch[0]>>2)&3)==3 && ((fetch[0]>>4)&3)==3 && ((fetch[0]>>6)&3)==3)?0x80000000u:0;
    textures[slot]=gamma|image_index;samplers[slot]=sampler_index;
    recent[1]=recent[0];recent[0]={fetch,id,texture.get(),textures[slot],samplers[slot]};
  }
  phase(GlobalBindingStats::kTextures);
  std::memcpy(constants.data+8192,textures.data(),128);std::memcpy(constants.data+8192+128,samplers.data(),128);
  std::memcpy(stream_data.data,streams.data(),sizeof(streams));
  if(!store.FlushTransient(constants,e) || !store.FlushTransient(stream_data,e))return false;
  const uint32_t record[]{uint32_t(layout.slice.offset),uint32_t(stream_data.offset),uint32_t(constants.offset),0};
  std::memcpy(page.table.data+16+page.count*16,record,16);
  if(!FlushPart(store,page.table,16+page.count*16,16,e))return false;
  out={page.count++,page.slot};++stats_.draws;e={};phase(GlobalBindingStats::kFinish);return true;
}
}
