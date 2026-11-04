#pragma once
#include "coop_apt_ancestry.h"
#include "coop_camera_packet.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <optional>
namespace superman_returns::coop::apt {
inline std::optional<float> Position(Role role,int32_t scaled,float width,float start,float notch) {
  if(scaled<0 || scaled>10000 || !std::isfinite(width) || width<=16 || width>4096 ||
     !std::isfinite(start) || !std::isfinite(notch))return {};
  const float percent=std::clamp((float(scaled)-500.f)/9500.f,0.f,1.f);
  const float delta=-width*(1-percent);
  switch(role) {
    case Role::FillLeft:return start+delta;
    case Role::FillBody:return start+8+delta;
    case Role::FillRight:return start+width-8+delta;
    case Role::Notch:return notch+width*percent;
    default:return {};
  }
}
// Only the supported 8-unit cap geometry is admitted by the runtime caller.
// Preserve every original packet/color/effect byte except the private matrix
// pointer and its translation. No P1 percentage or APT tween state is needed.
inline std::optional<std::array<uint8_t,96>> PrivatePacket(
    std::span<const uint8_t> original,std::span<const uint8_t> transform,
    uint32_t scratch,float desired,float current,const std::array<float,16>& meter) {
  if(original.size()!=16 || transform.size()!=80 || !scratch ||
     uint64_t(scratch)+96>0x100000000ull || !packet::ReadWord(original,4) ||
     !std::isfinite(desired) || !std::isfinite(current) || std::abs(desired-current)>8192)return {};
  for(float value:meter)if(!std::isfinite(value))return {};
  for(size_t i=0;i<16;++i)
    if(!std::isfinite(std::bit_cast<float>(packet::ReadWord(transform,i*4))))return {};
  std::array<uint8_t,96> result{};
  std::copy(original.begin(),original.end(),result.begin());
  std::copy(transform.begin(),transform.end(),result.begin()+16);
  packet::WriteWord(result,4,scratch+16);
  for(size_t i=0;i<4;++i) {
    const float value=std::bit_cast<float>(packet::ReadWord(transform,48+i*4))+(desired-current)*meter[i];
    if(!std::isfinite(value))return {};
    packet::WriteWord(result,16+48+i*4,std::bit_cast<uint32_t>(value));
  }
  return result;
}
}
