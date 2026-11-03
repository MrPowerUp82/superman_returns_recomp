#pragma once
// Pure encoding/ownership rules for the guest packet's second 96-byte view.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace superman_returns::coop::packet {
constexpr size_t kSecondaryOffset=112, kViewSize=96, kPacketSize=208;
using ViewBytes=std::array<uint8_t,kViewSize>;
inline uint32_t ReadWord(std::span<const uint8_t> bytes,size_t offset) {
  return (uint32_t(bytes[offset])<<24)|(uint32_t(bytes[offset+1])<<16)|
      (uint32_t(bytes[offset+2])<<8)|bytes[offset+3];
}
inline void WriteWord(std::span<uint8_t> bytes,size_t offset,uint32_t word) {
  for(size_t i=0;i<4;++i) bytes[offset+i]=uint8_t(word>>(24-i*8));
}
inline std::optional<ViewBytes> EncodeView(const std::array<float,16>& matrix,
    const std::array<float,4>& projection,uint32_t generation) {
  if(!generation) return {};
  for(auto x:matrix) if(!std::isfinite(x)) return {};
  for(auto x:projection) if(!std::isfinite(x)) return {};
  if(!(projection[0]>0 && projection[1]>projection[0] && projection[2]>0 &&
       projection[2]<3.14159265358979323846 && projection[3]>0)) return {};
  ViewBytes bytes{};
  for(size_t i=0;i<16;++i) WriteWord(bytes,i*4,std::bit_cast<uint32_t>(matrix[i]));
  for(size_t i=0;i<4;++i) WriteWord(bytes,64+i*4,std::bit_cast<uint32_t>(projection[i]));
  WriteWord(bytes,84,0x050002D0); // packed uint16 viewport: 0,0,1280,720
  WriteWord(bytes,88,generation); // 0 invalid; a change disables cross-join interpolation
  return bytes;
}
struct DecodedView {
  std::array<float,16> matrix{};
  std::array<float,4> projection{}; // near, far, FOV, aspect
  uint32_t generation=0;
};
inline std::optional<DecodedView> DecodeOwnedSecondary(std::span<const uint8_t> bytes) {
  if(bytes.size()<kPacketSize) return {};
  const auto view=bytes.subspan(kSecondaryOffset,kViewSize);
  DecodedView result;
  result.generation=ReadWord(view,88);
  if((result.generation&0xC0000000u)!=0xC0000000u || !(result.generation&0x3FFFFFFFu) ||
     ReadWord(view,80)!=0 || ReadWord(view,84)!=0x050002D0u) return {};
  for(size_t i=0;i<16;++i) result.matrix[i]=std::bit_cast<float>(ReadWord(view,i*4));
  for(size_t i=0;i<4;++i) result.projection[i]=std::bit_cast<float>(ReadWord(view,64+i*4));
  if(!EncodeView(result.matrix,result.projection,result.generation)) return {};
  return result;
}

enum class Result { Invalid, Inactive, Occupied, Published, Cleared };
class PacketOwner {
 public:
  Result Publish(std::span<uint8_t> bytes,uint32_t address,const std::optional<ViewBytes>& view) {
    if(!address || bytes.size()<kPacketSize) return Result::Invalid;
    const uint32_t current=ReadWord(bytes,kSecondaryOffset+88);
    const bool ours=address==address_ && version_ && current==version_;
    if(current && !ours) return Result::Occupied;
    if(view) {
      std::copy(view->begin(),view->end(),bytes.begin()+kSecondaryOffset);
      address_=address; version_=ReadWord(*view,88);
      return Result::Published;
    }
    if(ours) {
      std::fill_n(bytes.begin()+kSecondaryOffset,kViewSize,uint8_t{0});
      version_=0;
      return Result::Cleared;
    }
    return Result::Inactive;
  }
 private:
  uint32_t address_=0,version_=0;
};
}
