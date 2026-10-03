#include "vertex_layout.h"
#include <algorithm>

namespace superman_returns::graphics::guest {
VertexEncoding GetVertexEncoding(uint32_t type) {
  switch(type) {
    case 0x2c83a4:return {4,4,true};
    case 0x2c23a5:return {8,4,true};
    case 0x2a23b9:return {12,4,true};
    case 0x1a23a6:return {16,4,true};
    case 0x182886:case 0x1a2286:case 0x1a2386:case 0x1a2086:case 0x1a2186:
    case 0x2c82a1:case 0x2a2187:case 0x2a2190:case 0x2a2390:return {4,4,false};
    case 0x2c2359:case 0x2c2259:case 0x2c2159:case 0x2c2059:case 0x2c235f:return {4,2,false};
    case 0x1a235a:case 0x1a215a:case 0x1a205a:case 0x1a2360:return {8,2,false};
    default:return {0,4,false};
  }
}
void SwapVertexElements(std::span<uint8_t> bytes,uint32_t stride,uint32_t phase,
                        uint32_t stream,std::span<const VertexAttribute> attrs) {
  if(!stride || phase>bytes.size()) return;
  for(const auto& a:attrs) {
    if(stream!=UINT32_MAX && a.stream!=stream) continue;
    auto f=GetVertexEncoding(a.type);
    if(!f.size || uint64_t(a.offset)+f.size>stride) continue;
    for(size_t v=phase;bytes.size()-v>=stride;v+=stride)
      for(uint32_t w=0;w<f.size;w+=f.swap)
        std::reverse(bytes.begin()+v+a.offset+w,bytes.begin()+v+a.offset+w+f.swap);
  }
}
}
