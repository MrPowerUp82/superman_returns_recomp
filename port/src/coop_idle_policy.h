#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>
namespace superman_returns::coop {
struct IdleBinding {uint32_t primary=0,secondary=0,generation=0;};
enum class IdleRoute {Native,Primary,Secondary};
inline IdleRoute SelectIdleRoute(const IdleBinding& b,uint32_t actor,uint32_t port) {
  if(!b.primary || b.primary==255 || !b.secondary || b.secondary==255 || b.primary==b.secondary ||
     (b.generation&0xC0000000u)!=0xC0000000u || !(b.generation&0x3FFFFFFFu))return IdleRoute::Native;
  if(actor==b.primary && port==0)return IdleRoute::Primary;
  if(actor==b.secondary && port==1)return IdleRoute::Secondary;
  return IdleRoute::Native;
}
inline double CombinedIdle(double primary,double secondary) {
  // Native last-input timestamps are floats, but now is calculated as a
  // double. A just-written timestamp can round a few microseconds into the
  // future; treat at most one millisecond of that residue as active input.
  return std::isfinite(primary)&&primary>=0 && std::isfinite(secondary)&&secondary>=-0.001 ?
      std::min(primary,std::max(0.0,secondary)):primary;
}
}
