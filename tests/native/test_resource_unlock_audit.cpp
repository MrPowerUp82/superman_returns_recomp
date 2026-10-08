#include "resource_unlock_audit.h"
#include "test_main.h"
using namespace superman_returns::native;
SR_TEST(handoff_descriptor_reads_strided_planes_and_rejects_short_inputs) {
  std::array<uint8_t,48> bytes{};
  auto put=[&](size_t offset,uint32_t value) {
    for(size_t i=0;i<4;++i) bytes[offset+i]=uint8_t(value>>(24-8*i));
  };
  put(0,1); put(12,0xe1234000); put(16,0xe2345000); put(20,0xe3456000);
  put(36,1280); put(40,640); put(44,768);
  FrameHandoffDescriptor descriptor;
  SR_CHECK(ReadFrameHandoffDescriptor(bytes,descriptor));
  SR_CHECK_EQ(descriptor.kind,1u);
  SR_CHECK_EQ(descriptor.destinations[2],0xe3456000u);
  SR_CHECK_EQ(descriptor.strides[0],1280u);
  SR_CHECK_EQ(descriptor.strides[2],768u);
  SR_CHECK(!ReadFrameHandoffDescriptor(std::span(bytes).first(47),descriptor));
  SR_CHECK_EQ(descriptor.strides[2],768u);
}
SR_TEST(handoff_plane_pages_handle_aliases_offsets_and_arena_end) {
  SR_CHECK(HandoffPlaneMatchesPage(0xe1234080,0xe1234000));
  SR_CHECK(HandoffPlaneMatchesPage(0xe1234080,0x01235000));
  SR_CHECK(!HandoffPlaneMatchesPage(0xe1234080,0x01234000));
  SR_CHECK(!HandoffPlaneMatchesPage(0xfffff080,0xfffff000));
  SR_CHECK(!HandoffPlaneMatchesPage(0,0x1000));
}
SR_TEST(handoff_plane_consumption_rejects_duplicates_and_wrong_destinations) {
  FrameHandoffDescriptor descriptor{1,{0xe1000000,0xe2000000,0xe3000000},{1280,640,640}};
  uint32_t mask=0;
  SR_CHECK(ConsumeHandoffPlane(descriptor,2,0xe3000000,mask));
  SR_CHECK(!ConsumeHandoffPlane(descriptor,2,0xe3000000,mask));
  SR_CHECK(!ConsumeHandoffPlane(descriptor,0,0xe2000000,mask));
  SR_CHECK(!ConsumeHandoffPlane(descriptor,3,0xe1000000,mask));
  SR_CHECK_EQ(mask,4u);
  SR_CHECK(ConsumeHandoffPlane(descriptor,0,0xe1000000,mask));
  SR_CHECK(ConsumeHandoffPlane(descriptor,1,0xe2000000,mask));
  SR_CHECK_EQ(mask,7u);
  descriptor.kind=255;
  mask=0;
  SR_CHECK(!ConsumeHandoffPlane(descriptor,0,0xe1000000,mask));
  SR_CHECK_EQ(mask,0u);
}
SR_TEST(unlock_texture_fetch_normalizes_cpu_alias_pages_without_losing_format_bits) {
  std::array<uint32_t,6> fetch{2,0xfeeaf054,0,0,0,0xfee9fa00};
  SR_CHECK(NormalizeUnlockTextureFetch(fetch));
  SR_CHECK_EQ(fetch[1],0x1eeb0054u);
  SR_CHECK_EQ(fetch[5],0x1eea0a00u);
  fetch[1]=0xa1234054; fetch[5]=0xc2345a00;
  SR_CHECK(NormalizeUnlockTextureFetch(fetch));
  SR_CHECK_EQ(fetch[1],0x01234054u);
  SR_CHECK_EQ(fetch[5],0x02345a00u);
  fetch[1]=0xfffff054;
  const auto before=fetch;
  SR_CHECK(!NormalizeUnlockTextureFetch(fetch));
  SR_CHECK(fetch==before);
}
SR_TEST(unlock_range_keys_compare_exact_physical_partitions) {
  using superman_returns::graphics::guest::TextureRange;
  const std::array<TextureRange,2> ranges{{{0x1000,0x2000},{0x9000,0x1000}}};
  UnlockRangeKey a,b;
  SR_CHECK(MakeUnlockRangeKey(ranges,a));
  SR_CHECK(MakeUnlockRangeKey(ranges,b));
  SR_CHECK(a==b);
  auto changed=ranges;
  ++changed[1].length;
  SR_CHECK(MakeUnlockRangeKey(changed,b));
  SR_CHECK(a!=b);
  SR_CHECK(MakeUnlockRangeKey(std::span(ranges).first(1),b));
  SR_CHECK(a!=b);
  const auto previous=b;
  SR_CHECK(!MakeUnlockRangeKey({},b));
  const std::array<TextureRange,33> too_many{};
  SR_CHECK(!MakeUnlockRangeKey(too_many,b));
  SR_CHECK(b==previous);
}
SR_TEST(unlock_texture_fetch_decodes_big_endian_header_and_full_descriptor) {
  std::array<uint8_t,52> bytes{};
  const std::array<uint32_t,6> expected{2,0x12345012,0x12345678,0x89abcdef,0xfedcba98,0x345670aa};
  for (size_t i=0;i<6;++i)
    for (size_t j=0;j<4;++j) bytes[28+4*i+j]=uint8_t(expected[i]>>(24-8*j));
  std::array<uint32_t,6> fetch{};
  SR_CHECK(ReadUnlockTextureFetch(bytes,0x12345000,0x34567000,fetch));
  SR_CHECK(fetch==expected);
}
SR_TEST(unlock_texture_fetch_rejects_unrelated_objects_without_changing_output) {
  std::array<uint8_t,52> bytes{};
  std::array<uint32_t,6> fetch{1,2,3,4,5,6};
  const auto original=fetch;
  SR_CHECK(!ReadUnlockTextureFetch(bytes,0,0,fetch));
  bytes[31]=2;
  SR_CHECK(!ReadUnlockTextureFetch(bytes,0x1000,0,fetch));
  SR_CHECK(!ReadUnlockTextureFetch(bytes,0,0x1000,fetch));
  SR_CHECK(!ReadUnlockTextureFetch(std::span(bytes).first(51),0,0,fetch));
  SR_CHECK(fetch==original);
}
