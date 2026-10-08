#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <compare>
#include "../graphics/guest/texture_layout.h"

namespace superman_returns::native {
struct FrameHandoffDescriptor {
  uint32_t kind=0;
  std::array<uint32_t,3> destinations{}, strides{};
};
inline bool ReadFrameHandoffDescriptor(std::span<const uint8_t> bytes,
                                       FrameHandoffDescriptor& out) {
  if (bytes.size()<48) return false;
  auto word=[&](size_t offset) {
    return (uint32_t(bytes[offset])<<24)|(uint32_t(bytes[offset+1])<<16)|
           (uint32_t(bytes[offset+2])<<8)|bytes[offset+3];
  };
  FrameHandoffDescriptor result;
  result.kind=word(0);
  for(size_t i=0;i<3;++i) {
    result.destinations[i]=word(12+4*i);
    result.strides[i]=word(36+4*i);
  }
  out=result;
  return true;
}
// Address agreement is evidence of a plane's page, not resource identity or
// proof of write coverage. The locked pointer may include an offset in a page.
inline bool HandoffPlaneMatchesPage(uint32_t destination, uint32_t base_page) {
  if (!destination || !base_page) return false;
  auto physical=[](uint32_t address) {
    return uint64_t(address&0x1fffffffu)+(address>=0xe0000000u ? 0x1000u : 0u);
  };
  const auto a=physical(destination), b=physical(base_page);
  return a<0x20000000u && b<0x20000000u && (a&~uint64_t(0xfff))==b;
}
// Consume each plane once. Wrong sites, wrong pages and duplicate unlocks
// cannot advance the mask and accidentally complete a handoff.
inline bool ConsumeHandoffPlane(const FrameHandoffDescriptor& descriptor,
    uint32_t plane, uint32_t base_page, uint32_t& mask) {
  if(descriptor.kind!=1 || plane>=3 || (mask&(1u<<plane)) ||
     !HandoffPlaneMatchesPage(descriptor.destinations[plane],base_page)) return false;
  mask|=1u<<plane;
  return true;
}
// Object headers contain CPU physical aliases, whereas bound GPU fetches
// contain physical pages. Match the shared helper's low-29-bit + E/F-view
// 4 KiB bias before describing source coverage. Never wrap the arena end.
inline bool NormalizeUnlockTextureFetch(std::array<uint32_t,6>& fetch) {
  auto result=fetch;
  for (size_t word : {size_t(1),size_t(5)}) {
    const uint32_t page=fetch[word]&0xfffff000u;
    const uint32_t physical=(page&0x1fffffffu)+(page>=0xe0000000u ? 0x1000u : 0u);
    if (physical>=0x20000000u) return false;
    result[word]=physical|(fetch[word]&0xfffu);
  }
  fetch=result;
  return true;
}
struct UnlockRangeKey {
  uint32_t count=0;
  std::array<uint64_t,32> ranges{};
  auto operator<=>(const UnlockRangeKey&) const = default;
};
inline bool MakeUnlockRangeKey(std::span<const graphics::guest::TextureRange> ranges,
                              UnlockRangeKey& key) {
  if (ranges.empty() || ranges.size()>key.ranges.size()) return false;
  UnlockRangeKey result;
  result.count=uint32_t(ranges.size());
  for (size_t i=0;i<ranges.size();++i)
    result.ranges[i]=(uint64_t(ranges[i].address)<<32)|ranges[i].length;
  key=result;
  return true;
}
// Structural evidence only: the two texture wrappers read fetch words 1/5
// at object offsets 32/48 and pass their page addresses to the common unlock.
inline bool ReadUnlockTextureFetch(std::span<const uint8_t> header,
    uint32_t base_page, uint32_t mip_page, std::array<uint32_t, 6>& fetch) {
  if (header.size() < 52) return false;
  std::array<uint32_t, 6> decoded{};
  for (size_t i = 0; i < decoded.size(); ++i) {
    const auto* p = header.data() + 28 + 4 * i;
    decoded[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                 (uint32_t(p[2]) << 8) | p[3];
  }
  if ((decoded[0] & 3) != 2 || (decoded[1] & 0xfffff000u) != base_page ||
      (decoded[5] & 0xfffff000u) != mip_page) return false;
  fetch = decoded;
  return true;
}
}  // namespace superman_returns::native
