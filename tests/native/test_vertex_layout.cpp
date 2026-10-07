#include "test_main.h"
#include "../../port/src/graphics/guest/vertex_layout.h"
#include <cstring>
#include <stdexcept>
using namespace superman_returns::graphics::guest;
SR_TEST(vertex_bytes_swap_float_and_ushort2_without_normalizing) {
  std::vector<uint8_t> data={0x3f,0x80,0,0,0,1,0x01,0x23};
  std::vector<VertexAttribute> attributes={{0,0,0x2c83a4,0,0},{0,4,0x2c2259,5,0}};
  SwapVertexElements(data,8,0,0,attributes);
  float f;uint16_t u;
  std::memcpy(&f,data.data(),4);std::memcpy(&u,data.data()+6,2);
  SR_CHECK(f==1.0f);SR_CHECK_EQ(u,0x123u);
  SR_CHECK_EQ(GetVertexEncoding(0x2c2259).swap,2u);
}
SR_TEST(vertex_phase_preserves_prefix_and_partial_tail) {
  std::vector<uint8_t> data={9,8,0,1,0,2,7};
  std::vector<VertexAttribute> attributes={{1,0,0x2c2259,0,0},{0,0,0x2c83a4,0,0}};
  SwapVertexElements(data,4,2,1,attributes);
  SR_CHECK(data==std::vector<uint8_t>({9,8,1,0,2,0,7}));
}
SR_TEST(declaration_cache_reuses_equal_contents_and_rejects_address_reuse) {
  VertexDeclarationCache cache;
  std::vector<uint8_t> bytes={0,1,0,4,0,0x2c,0x22,0x59,0,5,0,0};
  const auto& first=cache.Decode(bytes);
  SR_CHECK_EQ(first.attributes.size(),1u);
  SR_CHECK_EQ(first.attributes[0].stream,1u);
  SR_CHECK_EQ(first.attributes[0].offset,4u);
  SR_CHECK_EQ(first.attributes[0].type,0x2c2259u);
  SR_CHECK_EQ(first.streams_used,2u);
  auto another=bytes;
  SR_CHECK(&cache.Decode(another)==&first);
  bytes[3]=8;
  SR_CHECK_EQ(cache.Decode(bytes).attributes[0].offset,8u);
  SR_CHECK_EQ(cache.Decode(another).attributes[0].offset,4u);
}
SR_TEST(declaration_cache_honors_terminator_and_owns_source_bytes) {
  VertexDeclarationCache cache;
  std::vector<uint8_t> bytes(24,0);
  bytes[1]=0xff;bytes[13]=1;
  SR_CHECK(cache.Decode(bytes).attributes.empty());
  bytes[1]=0;
  SR_CHECK_EQ(cache.Decode(bytes).attributes.size(),2u);
  bytes.clear();
  SR_CHECK(cache.Decode(bytes).attributes.empty());
  bytes.resize(13);
  bool rejected=false;
  try {cache.Decode(bytes);} catch(const std::invalid_argument&) {rejected=true;}
  SR_CHECK(rejected);
}
