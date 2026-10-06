#include "descriptors.h"
#include "test_main.h"
#include <cstring>
using namespace superman_returns::graphics::vulkan;
namespace guest=superman_returns::graphics::guest;
namespace {
// The 12 KiB constant block of one draw (vs, ps, shared), as MapTransient hands it out.
struct Block {
  alignas(16) std::array<std::byte,sizeof(guest::ConstantSnapshot)> bytes{};
  std::byte* data() {return bytes.data();}
  const std::byte* shared() const {return bytes.data()+kSharedConstantsOffset;}
};
}
SR_TEST(descriptor_arrays_accept_32_slots_and_reject_33) {
  std::vector<TextureBindingRequest> requests;
  for(uint32_t i=0;i<32;++i) requests.push_back({i,1500+i,TextureDimension::k2D,0});
  DrawBindings bindings;Error e;
  SR_CHECK(RemapTextureBindings(requests,bindings,e));
  SR_CHECK_EQ(bindings.textures[0][31],1531u);
  requests.push_back({32,1600,TextureDimension::k2D,0});
  SR_CHECK(!RemapTextureBindings(requests,bindings,e));
}
SR_TEST(descriptors_remap_global_ids_keep_flags_and_dimension_dummies) {
  DrawBindings b;Error e;
  std::vector<TextureBindingRequest> requests{{7,1500,TextureDimension::k3D,0x80000000u},
                                             {9,1501,TextureDimension::kCube,0}};
  SR_CHECK(RemapTextureBindings(requests,b,e));
  SR_CHECK_EQ(b.texture_indices[7],0x80000000u);
  SR_CHECK_EQ(b.texture_indices[9],0u);
  SR_CHECK_EQ(b.textures[1][0],1500u);
  SR_CHECK_EQ(b.textures[2][0],1501u);
  SR_CHECK_EQ(b.textures[0][0],DummyTexture(TextureDimension::k2D));
  SR_CHECK_EQ(b.textures[1][31],DummyTexture(TextureDimension::k3D));
  requests.push_back(requests[0]);
  SR_CHECK(!RemapTextureBindings(requests,b,e));
}
SR_TEST(remap_failure_leaves_the_bindings_untouched) {
  DrawBindings b;Error e;
  std::vector<TextureBindingRequest> good{{3,1500,TextureDimension::k2D,0}};
  SR_CHECK(RemapTextureBindings(good,b,e));
  const DrawBindings before=b;
  std::vector<TextureBindingRequest> bad{{4,1600,TextureDimension::k2D,0},{4,1601,TextureDimension::k2D,0}};  // duplicate slot
  SR_CHECK(!RemapTextureBindings(bad,b,e));
  SR_CHECK(std::memcmp(&before,&b,sizeof(DrawBindings))==0);
  std::vector<TextureBindingRequest> zero{{5,0,TextureDimension::k2D,0}};  // no resource
  SR_CHECK(!RemapTextureBindings(zero,b,e));
  SR_CHECK(std::memcmp(&before,&b,sizeof(DrawBindings))==0);
}
SR_TEST(vertex_descriptor_remap_preserves_ushort2_and_stream_offset) {
  guest::DrawPacket p;
  p.vertex_fetch[48]={7,14,4,0x2C2259};
  guest::VertexStream stream{};stream.stream=7;stream.offset=12;stream.size=16;stream.stride=4;stream.update.plan.key=1500;
  p.streams.push_back(stream);
  Block block;DrawBindings b;Error e;
  SR_CHECK(BuildBindings(p,block.data(),b,e));
  SR_CHECK_EQ(b.vertex_buffers[0],1500u);
  guest::VertexFetchMeta meta{};std::memcpy(&meta,block.shared()+512+48*16,16);
  SR_CHECK_EQ(meta.buffer,0u);SR_CHECK_EQ(meta.offset,14u);SR_CHECK_EQ(meta.type,0x2C2259u);
  SR_CHECK_EQ(b.vertex_buffers[31],DummyBuffer);
}
SR_TEST(build_bindings_writes_vs_ps_and_patches_shared_in_the_block) {
  guest::DrawPacket p;
  p.constants.vs[3]=0x11111111;p.constants.ps[5]=0x22222222;p.constants.shared[300]=0x7e;  // 300 is outside the patched ranges
  p.texture_fetch[7][0]=2;p.texture_fetch[7][5]=1<<9;p.texture_fetch[7][1]=0x1006;
  Block block;DrawBindings b;Error e;
  SR_CHECK(BuildBindings(p,block.data(),b,e));
  uint32_t word=0;
  std::memcpy(&word,block.data()+3*4,4);SR_CHECK_EQ(word,0x11111111u);
  std::memcpy(&word,block.data()+4096+5*4,4);SR_CHECK_EQ(word,0x22222222u);
  SR_CHECK_EQ(uint32_t(block.shared()[300]),0x7eu);
  std::memcpy(&word,block.shared()+7*4,4);SR_CHECK_EQ(word,b.texture_indices[7]);   // texture index of slot 7
  std::memcpy(&word,block.shared()+128+7*4,4);SR_CHECK_EQ(word,7u);                // sampler index of slot 7
  SR_CHECK_EQ(b.sampler_indices[7],7u);
}
namespace {
// The implementation before Task 9: copies the whole 12 KiB snapshot, then overwrites shared[0,256) and shared[512,4096).
bool ReferenceBuildBindings(const guest::DrawPacket& draw,std::byte* block,DrawBindings& out,Error& error) {
  out=DrawBindings{};
  out.vertex_buffers.fill(DummyBuffer);
  std::array<TextureBindingRequest,32> requests;size_t request_count=0;
  for(uint32_t slot=0;slot<32;++slot) {
    const auto& fetch=draw.texture_fetch[slot];
    if((fetch[0]&3)!=2) continue;
    uint32_t dim=(fetch[5]>>9)&3;
    if(dim==0) dim=1;
    uint32_t gamma=0;
    if(((fetch[0]>>2)&3)==3 && ((fetch[0]>>4)&3)==3 && ((fetch[0]>>6)&3)==3) gamma=0x80000000u;
    requests[request_count++]={slot,TextureResourceId(fetch),TextureDimension(dim-1),gamma};
  }
  if(!RemapTextureBindings(std::span<const TextureBindingRequest>(requests.data(),request_count),out,error)) return false;
  std::array<uint8_t,32> stream_index;stream_index.fill(0xff);uint32_t streams=0;
  if(draw.inline_vertices) {
    out.vertex_buffers[0]=InlineBufferBase|draw.command_serial;
  } else {
    for(const auto& stream:draw.streams) {
      if(stream.stream>=32 || !stream.update.plan.key || stream_index[stream.stream]!=0xff || streams>=32) {
        error={"Vertex remap",VK_ERROR_INITIALIZATION_FAILED,"Invalid or duplicate vertex stream"};return false;
      }
      stream_index[stream.stream]=uint8_t(streams);out.vertex_buffers[streams++]=stream.update.plan.key;
    }
  }
  std::memcpy(block,&draw.constants,sizeof(guest::ConstantSnapshot));
  std::byte* shared=block+kSharedConstantsOffset;
  for(uint32_t i=0;i<draw.vertex_fetch.size();++i) {
    auto meta=draw.vertex_fetch[i];
    if(meta.type) {
      if(draw.inline_vertices) meta.buffer=0;
      else {
        if(meta.buffer>=32 || stream_index[meta.buffer]==0xff) {error={"Vertex remap",VK_ERROR_INITIALIZATION_FAILED,"Vertex metadata references missing stream"};return false;}
        meta.buffer=stream_index[meta.buffer];
      }
    } else {meta={31,0,0,0};}
    std::memcpy(shared+512+i*16,&meta,16);
  }
  for(uint32_t slot=0;slot<32;++slot) {
    out.sampler_indices[slot]=slot;
    std::memcpy(shared+slot*4,&out.texture_indices[slot],4);
    std::memcpy(shared+128+slot*4,&out.sampler_indices[slot],4);
  }
  error={};return true;
}
// Fills every constant of the packet with distinct, non-zero bytes so a missed copy cannot hide.
void FillConstants(guest::DrawPacket& p,uint32_t seed) {
  for(uint32_t i=0;i<1024;++i) {p.constants.vs[i]=seed*2654435761u+i*7u+1u;p.constants.ps[i]=seed*40503u+i*13u+3u;}
  for(uint32_t i=0;i<4096;++i) p.constants.shared[i]=uint8_t(seed*31u+i*5u+1u);
}
void ExpectSameBlockAsReference(const guest::DrawPacket& p) {
  Block fresh,old;fresh.bytes.fill(std::byte{0xCD});old.bytes.fill(std::byte{0x5A});  // the arena is neither zeroed nor initialized
  DrawBindings nb,ob;Error ne,oe;
  SR_CHECK(ReferenceBuildBindings(p,old.data(),ob,oe));
  SR_CHECK(BuildBindings(p,fresh.data(),nb,ne));
  SR_CHECK(std::memcmp(fresh.data(),old.data(),sizeof(guest::ConstantSnapshot))==0);
  SR_CHECK(std::memcmp(&nb,&ob,sizeof(DrawBindings))==0);
  // The two blocks start from different patterns, so equality proves every byte was written by BuildBindings.
}
}
SR_TEST(build_bindings_block_matches_the_reference_implementation_byte_for_byte) {
  {  // no textures, no streams
    guest::DrawPacket p;FillConstants(p,1);ExpectSameBlockAsReference(p);
  }
  {  // textures (all 32 slots, three dimensions and a gamma one) and several streams
    guest::DrawPacket p;FillConstants(p,2);
    for(uint32_t slot=0;slot<32;++slot) {
      p.texture_fetch[slot][0]=2|(slot==5?((3<<2)|(3<<4)|(3<<6)):0);p.texture_fetch[slot][1]=0x1000+slot;
      p.texture_fetch[slot][5]=(slot%3)<<9;
    }
    for(uint32_t s=0;s<3;++s) {
      guest::VertexStream stream{};stream.stream=s*2+1;stream.size=16;stream.stride=4;stream.update.plan.key=1500+s;p.streams.push_back(stream);
    }
    p.vertex_fetch[0]={1,0,4,0x2C2259};p.vertex_fetch[48]={5,14,4,0x2C2259};p.vertex_fetch[223]={3,2,8,0x1A2086};
    ExpectSameBlockAsReference(p);
  }
  {  // inline vertices
    guest::DrawPacket p;FillConstants(p,3);p.inline_vertices=true;p.inline_stride=32;p.command_serial=77;
    p.texture_fetch[0][0]=2;p.texture_fetch[0][5]=1<<9;p.texture_fetch[31][0]=2;p.texture_fetch[31][5]=2<<9;
    p.vertex_fetch[0]={9,0,4,0x2C2259};p.vertex_fetch[10]={20,8,4,0x2C23A5};p.vertex_fetch[100]={0,16,4,0x1A2086};
    ExpectSameBlockAsReference(p);
  }
  {  // the packets composition and immediate build (zero constants)
    guest::DrawPacket p;p.inline_vertices=true;p.inline_stride=24;ExpectSameBlockAsReference(p);
    guest::DrawPacket q;ExpectSameBlockAsReference(q);
  }
}
SR_TEST(build_bindings_rejects_duplicate_and_missing_streams) {
  Block block;DrawBindings b;Error e;
  guest::VertexStream stream{};stream.stream=2;stream.size=16;stream.stride=4;stream.update.plan.key=1500;
  guest::DrawPacket duplicate;duplicate.streams={stream,stream};
  SR_CHECK(!BuildBindings(duplicate,block.data(),b,e));
  guest::DrawPacket no_key;stream.update.plan.key=0;no_key.streams={stream};
  SR_CHECK(!BuildBindings(no_key,block.data(),b,e));
  guest::DrawPacket missing;stream.update.plan.key=1500;missing.streams={stream};
  missing.vertex_fetch[0]={9,0,4,0x2C2259};   // references stream 9, which the draw does not have
  SR_CHECK(!BuildBindings(missing,block.data(),b,e));
  guest::DrawPacket out_of_range;out_of_range.vertex_fetch[0]={40,0,4,0x2C2259};
  SR_CHECK(!BuildBindings(out_of_range,block.data(),b,e));
}
SR_TEST(sampler_plan_preserves_fetch_filters_and_address_modes) {
  std::array<uint32_t,6> fetch{};
  fetch[0]=(0u<<10)|(1u<<13)|(2u<<16);
  fetch[3]=(1u<<19)|(1u<<23);
  VkPhysicalDeviceFeatures features{};VkPhysicalDeviceLimits limits{};
  VkSamplerCreateInfo info{};Error e;
  SR_CHECK(PlanSampler(fetch,features,limits,false,info,e));
  SR_CHECK(info.addressModeU==VK_SAMPLER_ADDRESS_MODE_REPEAT);
  SR_CHECK(info.addressModeV==VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT);
  SR_CHECK(info.addressModeW==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
  SR_CHECK(info.magFilter==VK_FILTER_LINEAR);SR_CHECK(info.minFilter==VK_FILTER_NEAREST);
  SR_CHECK(info.mipmapMode==VK_SAMPLER_MIPMAP_MODE_LINEAR);
  fetch[0]=3u<<10;
  SR_CHECK(!PlanSampler(fetch,features,limits,false,info,e));
  SR_CHECK(PlanSampler(fetch,features,limits,true,info,e));
  SR_CHECK(info.addressModeU==VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE);
  fetch[0]=0;fetch[3]=3u<<25;
  SR_CHECK(!PlanSampler(fetch,features,limits,false,info,e));
  features.samplerAnisotropy=true;limits.maxSamplerAnisotropy=16;
  SR_CHECK(PlanSampler(fetch,features,limits,false,info,e));
  SR_CHECK(info.anisotropyEnable);SR_CHECK_EQ(info.maxAnisotropy,4u);
}
SR_TEST(draw_bindings_gamma_matches_reference_rgb_sign_rule) {
  guest::DrawPacket packet;packet.texture_fetch[0][0]=2|(3<<2);packet.texture_fetch[0][5]=1<<9;
  Block block;DrawBindings bindings;Error e;SR_CHECK(BuildBindings(packet,block.data(),bindings,e));
  SR_CHECK_EQ(bindings.texture_indices[0]&0x80000000u,0u);
  packet.texture_fetch[0][0]|=(3<<4)|(3<<6);
  SR_CHECK(BuildBindings(packet,block.data(),bindings,e));SR_CHECK_EQ(bindings.texture_indices[0]&0x80000000u,0x80000000u);
}
SR_TEST(resource_id_bases_do_not_collide) {
  SR_CHECK(InlineBufferBase != ExpandedIndexBufferBase);
  SR_CHECK(InlineBufferBase != DescriptorConstantBufferBase);
  SR_CHECK(ExpandedIndexBufferBase != DescriptorConstantBufferBase);
  std::array<uint32_t,6> fetch{2,0,0,0,0,1<<9};
  SR_CHECK_EQ(TextureResourceId(fetch) & InlineBufferBase, 0ull);
  for(uint64_t serial : {1ull, 39ull, 1000ull, 1000000ull}) {
    auto inline_id = InlineBufferBase | serial;
    auto expanded_id = ExpandedIndexBufferBase | serial;
    auto constant_id = DescriptorConstantBufferBase | serial;
    SR_CHECK(inline_id != expanded_id);
    SR_CHECK(inline_id != constant_id);
    SR_CHECK(expanded_id != constant_id);
  }
}

