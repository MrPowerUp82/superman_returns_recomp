#pragma once
#include "coop_camera_packet.h"

namespace superman_returns::coop::hud {
// HudThreatState: vtable/magic/type at 0..15, actor position/terrain/heading
// at 16..39, visibility at 40, then 64 shared-world threat records at 48.
constexpr size_t kStateSize=3120, kAnchorOffset=16, kAnchorSize=24;
using Anchor=std::array<uint8_t,kAnchorSize>;
struct Snapshot {
  Anchor anchor{};
  uint32_t generation=0,target=0;
};
inline bool Valid(const Snapshot& value,uint32_t generation) {
  if(!value.target || value.target==255 || value.generation!=generation ||
     (generation&0xC0000000u)!=0xC0000000u || !(generation&0x3FFFFFFFu))return false;
  for(size_t i=0;i<kAnchorSize;i+=4)
    if(!std::isfinite(std::bit_cast<float>(packet::ReadWord(value.anchor,i))))return false;
  return true;
}
inline bool Retarget(std::span<uint8_t> state,const Snapshot& value,uint32_t generation) {
  if(state.size()!=kStateSize || packet::ReadWord(state,0)!=0x820215D0 ||
     packet::ReadWord(state,4)!=0x56414C44 || !Valid(value,generation))return false;
  std::copy(value.anchor.begin(),value.anchor.end(),state.begin()+kAnchorOffset);
  return true;
}
} // namespace superman_returns::coop::hud
