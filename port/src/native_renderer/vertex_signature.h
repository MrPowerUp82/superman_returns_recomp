#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace superman_returns::native {
inline bool VertexShaderHasFloatOutput(const std::vector<uint8_t>& blob,
                                        const char* semantic,uint32_t index,uint32_t mask) {
  auto rd=[&](size_t at) {
    uint32_t v=0;
    if(at<=blob.size() && blob.size()-at>=4) std::memcpy(&v,blob.data()+at,4);
    return v;
  };
  if(!semantic || !mask || (mask&~15u) || blob.size()<36 || std::memcmp(blob.data(),"DXBC",4)) return false;
  const uint32_t chunks=rd(28);
  if(chunks>64 || uint64_t(chunks)*4+32>blob.size()) return false;
  for(uint32_t c=0;c<chunks;++c) {
    const size_t at=rd(32+4*c);
    if(at>blob.size() || blob.size()-at<16 || std::memcmp(blob.data()+at,"OSG1",4)) continue;
    const size_t base=at+8;
    const uint32_t bytes=rd(at+4);
    if(bytes<8 || bytes>blob.size()-base) return false;
    const size_t end=base+bytes;
    const uint32_t count=rd(base), first=rd(base+4);
    if(count>64 || first<8 || uint64_t(first)+uint64_t(count)*32>bytes) return false;
    for(uint32_t i=0;i<count;++i) {
      const size_t e=base+first+size_t(i)*32;
      if(rd(e)!=0 || rd(e+8)!=index || rd(e+12)!=0 || rd(e+16)!=3 ||
         (rd(e+24)&mask)!=mask) continue;
      const uint32_t name_offset=rd(e+4);
      if(name_offset>=bytes) continue;
      const char* name=reinterpret_cast<const char*>(blob.data()+base+name_offset);
      const size_t length=strnlen(name,end-(base+name_offset));
      if(length==end-(base+name_offset)) continue;
      if(std::string_view(name,length)==semantic) return true;
    }
  }
  return false;
}
inline bool VertexShaderHasFloat3Output(const std::vector<uint8_t>& blob,
                                        const char* semantic,uint32_t index) {
  return VertexShaderHasFloatOutput(blob,semantic,index,7);
}
inline std::vector<std::pair<std::string, uint32_t>> VertexShaderInputs(const std::vector<uint8_t>& dxbc, bool float_only = false) {
  std::vector<std::pair<std::string, uint32_t>> out;
  auto rd = [&](size_t at) -> uint32_t {
    uint32_t v = 0;
    if (at + 4 <= dxbc.size()) std::memcpy(&v, dxbc.data() + at, 4);
    return v;
  };
  if (dxbc.size() < 32 || std::memcmp(dxbc.data(), "DXBC", 4) != 0) return out;
  const uint32_t chunks = rd(28);
  for (uint32_t c = 0; c < chunks && c < 64; ++c) {
    const size_t at = rd(32 + 4 * c);
    if (at + 8 > dxbc.size() || std::memcmp(dxbc.data() + at, "ISG1", 4) != 0) continue;
    const size_t base = at + 8;
    const size_t end = std::min(dxbc.size(), base + rd(at + 4));
    const uint32_t count = rd(base);
    for (uint32_t i = 0; i < count && i < 64; ++i) {
      const size_t e = base + rd(base + 4) + 32 * i;
      if (e + 32 > end) break;
      // Generated system inputs have no IA element. Only float inputs can
      // use the XDK's (0, 0, 0, 1) fallback without packed-format decoding.
      if (rd(e + 12) != 0 || (float_only && rd(e + 16) != 3)) continue;
      const size_t name_at = base + rd(e + 4);
      if (name_at >= end) continue;
      const char* name = reinterpret_cast<const char*>(dxbc.data() + name_at);
      out.emplace_back(std::string(name, strnlen(name, end - name_at)), rd(e + 8));
    }
    break;
  }
  return out;
}

}
