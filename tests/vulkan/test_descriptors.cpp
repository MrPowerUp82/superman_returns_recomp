#include "descriptors.h"
#include "test_main.h"
#include <cstring>
using namespace superman_returns::graphics::vulkan;
namespace guest=superman_returns::graphics::guest;
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
SR_TEST(vertex_descriptor_remap_preserves_ushort2_and_stream_offset) {
  guest::DrawPacket p;
  p.vertex_fetch[48]={7,14,4,0x2C2259};
  guest::VertexStream stream{};stream.stream=7;stream.offset=12;stream.size=16;stream.stride=4;stream.update.plan.key=1500;
  p.streams.push_back(stream);
  Error e;auto b=BuildBindings(p,e);
  SR_CHECK(e.message.empty());
  SR_CHECK_EQ(b.vertex_buffers[0],1500u);
  guest::VertexFetchMeta meta{};std::memcpy(&meta,b.constants.shared.data()+512+48*16,16);
  SR_CHECK_EQ(meta.buffer,0u);SR_CHECK_EQ(meta.offset,14u);SR_CHECK_EQ(meta.type,0x2C2259u);
  SR_CHECK_EQ(b.vertex_buffers[31],DummyBuffer);
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
  Error e;auto bindings=BuildBindings(packet,e);SR_CHECK(e.message.empty());
  SR_CHECK_EQ(bindings.texture_indices[0]&0x80000000u,0u);
  packet.texture_fetch[0][0]|=(3<<4)|(3<<6);
  bindings=BuildBindings(packet,e);SR_CHECK_EQ(bindings.texture_indices[0]&0x80000000u,0x80000000u);
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

