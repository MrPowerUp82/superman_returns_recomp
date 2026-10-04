#include "primitive_expansion.h"
#include <cstring>
namespace superman_returns::graphics::guest {
bool ExpandRectangles(std::span<const std::byte> bytes,uint32_t count,uint32_t stride,
    std::span<const VertexAttribute> attributes,std::vector<uint8_t>& result,std::string& error) {
  uint32_t rectangles=count/3;uint64_t size=uint64_t(rectangles)*6*stride;
  if(!stride || uint64_t(count)*stride>bytes.size() || size>512*1024*1024) {error="Rectangle vertex range invalid";return false;}
  struct FloatElement {uint32_t offset,size;};std::vector<FloatElement> floats;int32_t position=-1;
  for(auto& a:attributes) {auto encoding=GetVertexEncoding(a.type);if(!encoding.floating || uint64_t(a.offset)+encoding.size>stride) continue;floats.push_back({a.offset,encoding.size});if(encoding.size>=8 && (position<0 || a.usage==0)) position=int32_t(a.offset);}
  std::vector<uint8_t> out(size),fourth(stride);auto source=reinterpret_cast<const uint8_t*>(bytes.data());
  for(uint32_t r=0;r<rectangles;++r) {
    const uint8_t* v[3];for(uint32_t i=0;i<3;++i) v[i]=source+(size_t(r)*3+i)*stride;uint32_t corner=0;
    if(position>=0) {float p[3][2];for(uint32_t i=0;i<3;++i) std::memcpy(p[i],v[i]+position,8);auto distance=[&](uint32_t i,uint32_t j){float x=p[i][0]-p[j][0],y=p[i][1]-p[j][1];return x*x+y*y;};float a=distance(1,2),b=distance(2,0),c=distance(0,1);corner=a>b && a>c?0:b>c?1:2;}
    const auto c=v[corner],a=v[(corner+1)%3],b=v[(corner+2)%3];std::memcpy(fourth.data(),c,stride);
    for(auto& f:floats) for(uint32_t k=0;k<f.size;k+=4) {float x,y,z;std::memcpy(&x,a+f.offset+k,4);std::memcpy(&y,b+f.offset+k,4);std::memcpy(&z,c+f.offset+k,4);float value=x+y-z;std::memcpy(fourth.data()+f.offset+k,&value,4);}
    const uint8_t* order[]{c,a,b,b,a,fourth.data()};for(uint32_t i=0;i<6;++i) std::memcpy(out.data()+(size_t(r)*6+i)*stride,order[i],stride);
  }
  result=std::move(out);error.clear();return true;
}
}
