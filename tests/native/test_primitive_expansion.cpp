#include "../../port/src/graphics/guest/primitive_expansion.h"
#include "test_main.h"
#include <cstring>
using namespace superman_returns::graphics::guest;
SR_TEST(rectangles_keep_diagonal_winding_float_interpolation_and_packed_corner) {
  struct Vertex {float x,y,u,v;uint32_t color;};
  Vertex vertices[]={{0,0,0,0,0xaabbccdd},{2,0,1,0,7},{0,3,0,1,9}};
  std::vector<VertexAttribute> attributes{{0,0,0x2c23a5,0,0},{0,8,0x2c23a5,5,0},{0,16,0x182886,10,0}};
  std::vector<uint8_t> output;std::string error;
  SR_CHECK(ExpandRectangles(std::as_bytes(std::span(vertices)),3,sizeof(Vertex),attributes,output,error));
  SR_CHECK_EQ(output.size(),6*sizeof(Vertex));
  if(output.size()==6*sizeof(Vertex)) {Vertex last;std::memcpy(&last,output.data()+5*sizeof(Vertex),sizeof(last));SR_CHECK_EQ(last.x,2.0f);SR_CHECK_EQ(last.y,3.0f);SR_CHECK_EQ(last.u,1.0f);SR_CHECK_EQ(last.v,1.0f);SR_CHECK_EQ(last.color,0xaabbccddu);SR_CHECK(!std::memcmp(output.data()+3*sizeof(Vertex),&vertices[2],sizeof(Vertex)));}
  SR_CHECK(!ExpandRectangles(std::as_bytes(std::span(vertices)).first(8),3,sizeof(Vertex),attributes,output,error));
}
