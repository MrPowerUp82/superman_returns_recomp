#include "test_main.h"
#include "../../port/src/graphics/guest/vertex_layout.h"
#include <cstring>
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
