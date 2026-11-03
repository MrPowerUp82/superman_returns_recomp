#pragma once
#include <cstdint>
#include <array>
#include <optional>
namespace superman_returns::coop {
struct EffectPhase {
  uint32_t owner=0,clock=0,cadence=0;
  bool operator==(const EffectPhase&) const=default;
};
struct ReplayPhase {
  std::array<EffectPhase,3> effects{};
  uint64_t frame_time=0;
  bool operator==(const ReplayPhase&) const=default;
};
template<class Read,class Write,class Draw>
bool ReplayAtOriginalPhase(const ReplayPhase& before,Read read,Write write,Draw draw) {
  const auto after=read();
  if(!after)return false;
  for(size_t i=0;i<3;++i)if(before.effects[i].owner!=after->effects[i].owner)return false;
  if(!write(before)){write(*after);return false;}
  struct Restore {Write& write;const ReplayPhase& after;bool done=false;
    ~Restore(){if(!done)write(after);}} restore{write,*after};
  draw();
  const bool restored=write(*after);
  restore.done=restored;
  return restored && read()==after;
}
enum class SplitSource {None,Bound,Resolved};
inline SplitSource ChooseSplitSource(bool bound_valid,bool cleared,bool resolved_valid) {
  return bound_valid && !cleared ? SplitSource::Bound :
      resolved_valid ? SplitSource::Resolved : SplitSource::None;
}
// Native worker markers: begin/end P1, begin/end P2. A swap consumes the pair.
struct SplitFrame {
  uint64_t frame=0;
  uint32_t generation=0,next=0;
  void Reset() {*this={};}
  bool Ready() const {return next==4;}
  bool DeferSwap() const {return next==1 || next==3;}
  bool Marker(uint32_t action,uint64_t id,uint32_t binding) {
    if(action==0) {
      Reset();
      if(!id || (binding&0xC0000000u)!=0xC0000000u || !(binding&0x3FFFFFFFu)) return false;
      frame=id;generation=binding;next=1;return true;
    }
    if(action>=4 || action!=next || id!=frame || binding!=generation) {Reset();return false;}
    ++next;return true;
  }
};
}
