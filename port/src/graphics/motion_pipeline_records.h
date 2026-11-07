#pragma once
#include <bit>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace superman_returns::graphics {
using std::size_t;
// Metadata only: numeric raster state, never shader bytecode or pointers.
struct MotionPipelineRecord {
  uint32_t cull=1, front=0, bias=0, slope=0, clamp=0, depth=40;
  bool operator==(const MotionPipelineRecord&) const = default;
};
// Keep the six-word journal ABI: old depth values are EQUAL, bit 16 selects
// conservative ALWAYS invalidation. The low 16 bits remain the DXGI format.
inline constexpr uint32_t kMotionPipelineInvalidator = 0x10000u;
inline constexpr uint32_t MotionPipelineDepthTest(uint32_t packed) {
  if(!(packed&kMotionPipelineInvalidator)) return 3; // EQUAL legacy
  const auto encoded=(packed>>17)&15u;return encoded ? encoded : 8; // ALWAYS legacy
}
inline constexpr uint32_t PackMotionPipelineDepth(uint32_t format,uint32_t comparison) {
  return comparison==3 ? format : format|kMotionPipelineInvalidator|(comparison<<17);
}
// Source-audited observed metadata from sustained title, object-motion and visible-DXR routes.
// No shader data; exact integer/bias float bits keep cold installs deterministic.
inline constexpr std::array<MotionPipelineRecord,4> kObservedMotionPipelineSeed{{
    {3,1,5873,1024416809,0,45},
    {3,1,5873,1024416809,0,45|kMotionPipelineInvalidator},
    {3,1,5873,1024416809,0,45|kMotionPipelineInvalidator|(7u<<17)},
    {3,1,0,1045220557,0,45|kMotionPipelineInvalidator|(5u<<17)}}};
inline bool ValidMotionPipelineRecord(const MotionPipelineRecord& r) {
  const uint32_t format=r.depth&0xffffu;
  return r.cull>=1 && r.cull<=3 && r.front<=1 &&
      !(r.depth&~(0xffffu|kMotionPipelineInvalidator|0x1e0000u)) &&
      ((r.depth&kMotionPipelineInvalidator) || !(r.depth&0x1e0000u)) &&
      MotionPipelineDepthTest(r.depth)>=1 && MotionPipelineDepthTest(r.depth)<=8 && (format==40 || format==45) &&
      std::isfinite(std::bit_cast<float>(r.slope)) && std::isfinite(std::bit_cast<float>(r.clamp));
}
inline std::vector<uint8_t> SerializeMotionPipelineRecords(const std::vector<MotionPipelineRecord>& records) {
  if(records.size()>128) return {};
  std::vector<uint8_t> out{'S','R','T','M','P','S','O',0};
  auto put=[&](uint32_t v) {for(unsigned i=0;i<4;++i)out.push_back(uint8_t(v>>(8*i)));};
  put(1);put(uint32_t(records.size()));
  for(const auto& r:records) {
    if(!ValidMotionPipelineRecord(r)) return {};
    for(uint32_t word:{r.cull,r.front,r.bias,r.slope,r.clamp,r.depth})put(word);
  }
  return out;
}
inline bool ParseMotionPipelineRecords(std::span<const uint8_t> bytes,
                                      std::vector<MotionPipelineRecord>& out) {
  out.clear();
  constexpr uint8_t magic[]{'S','R','T','M','P','S','O',0};
  if(bytes.size()<16)return false;
  for(size_t i=0;i<8;++i)if(bytes[i]!=magic[i])return false;
  auto get=[&](size_t at) {return uint32_t(bytes[at])|(uint32_t(bytes[at+1])<<8)|
      (uint32_t(bytes[at+2])<<16)|(uint32_t(bytes[at+3])<<24);};
  const auto count=get(12);
  if(get(8)!=1 || count>128 || bytes.size()!=16+size_t(count)*24)return false;
  std::vector<MotionPipelineRecord> parsed;
  for(uint32_t i=0;i<count;++i) {
    const size_t at=16+size_t(i)*24;
    MotionPipelineRecord r{get(at),get(at+4),get(at+8),get(at+12),get(at+16),get(at+20)};
    if(!ValidMotionPipelineRecord(r))return false;
    parsed.push_back(r);
  }
  out=std::move(parsed);return true;
}
}
