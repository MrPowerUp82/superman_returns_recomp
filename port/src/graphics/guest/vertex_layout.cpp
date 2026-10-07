#include "vertex_layout.h"
#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace superman_returns::graphics::guest {
const VertexDeclaration& VertexDeclarationCache::Decode(std::span<const uint8_t> bytes) {
  if(bytes.size()%12 || bytes.size()>64*12) throw std::invalid_argument("Invalid vertex declaration size");
  static const VertexDeclaration empty;
  if(bytes.empty()) return empty;
  auto same=[&](const Entry& entry) {return std::equal(bytes.begin(),bytes.end(),entry.bytes.begin(),entry.bytes.end());};
  if(last_ && same(*last_)) return last_->declaration;
  auto hash=std::hash<std::string_view>{}({reinterpret_cast<const char*>(bytes.data()),bytes.size()});
  if(auto found=entries_.find(hash);found!=entries_.end())
    for(const auto& entry:found->second) if(same(entry)) {last_=&entry;return entry.declaration;}
  if(count_>=128) {entries_.clear();last_=nullptr;count_=0;}
  Entry entry;entry.bytes.assign(bytes.begin(),bytes.end());
  auto u16=[&](size_t at) {return uint32_t(bytes[at])<<8|bytes[at+1];};
  auto u32=[&](size_t at) {return u16(at)<<16|u16(at+2);};
  for(size_t at=0;at<bytes.size();at+=12) {
    uint32_t stream=u16(at);if(stream==0xff) break;
    entry.declaration.attributes.push_back({stream,u16(at+2),u32(at+4),bytes[at+9],bytes[at+10]});
    if(stream<32) entry.declaration.streams_used|=1u<<stream;
  }
  auto& added=entries_[hash].emplace_back(std::move(entry));
  ++count_;last_=&added;return added.declaration;
}
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
