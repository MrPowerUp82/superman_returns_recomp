#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
namespace superman_returns::coop::status {
struct Stamina {
  uint32_t target=0;
  int32_t current=0,maximum=0,hud_maximum=0,scaled=0;
  bool operator==(const Stamina&) const=default;
};
struct Pair {
  uint32_t generation=0;
  uint64_t sequence=0;
  std::array<Stamina,2> players;
};
inline std::optional<Stamina> Normalize(uint32_t target,int32_t current,int32_t maximum) {
  if(!target || target==255 || maximum<=0 || current<0 || current>maximum)return {};
  const int32_t capacity=std::max(maximum,500);
  const auto scaled=int32_t((float(current)/float(capacity))*10000.f);
  return Stamina{target,current,maximum,capacity,scaled};
}
inline std::optional<Pair> MakePair(uint32_t generation,uint64_t sequence,const Stamina& a,const Stamina& b) {
  if((generation&0xC0000000u)!=0xC0000000u || !(generation&0x3FFFFFFFu) || !sequence || a.target==b.target ||
     Normalize(a.target,a.current,a.maximum)!=a || Normalize(b.target,b.current,b.maximum)!=b)return {};
  return Pair{generation,sequence,{a,b}};
}
inline std::optional<Stamina> Select(const std::optional<Pair>& pair,uint32_t generation,uint32_t view) {
  if(!pair || view>1 || generation!=pair->generation ||
     !MakePair(pair->generation,pair->sequence,pair->players[0],pair->players[1]))return {};
  return pair->players[view];
}
}
