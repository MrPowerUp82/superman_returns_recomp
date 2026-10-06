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

