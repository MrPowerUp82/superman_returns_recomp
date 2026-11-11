#pragma once
#include <cstdint>
#include <span>
#include <algorithm>

namespace superman_returns::native {
// Only the source-identified variable-step render scheduler is admitted.
// All simulation/timestep fields must remain untouched.
inline bool ApplyGuestRenderSchedulerPeriod(std::span<uint8_t> object, int32_t fps) {
  if(object.size()<176 || fps<0) return false;
  const auto read=[&](size_t offset,size_t size) {
    uint64_t value=0;for(size_t i=0;i<size;++i) value=(value<<8)|object[offset+i];return value;
  };
  // 82166178's derived vtable, variable-step mode, original rate and period.
  // Refuse another class/build, a fixed-step scheduler or an already changed rate.
  if(read(0,4)!=0x82005B98 || read(52,4)!=1 || read(60,4)!=0x41F00000 ||
      read(72,8)!=33333334) return false;
  // Unlimited mode uses the same nominal 1 ms floor as native unlocked vblank
  // and the maximum supported 1000 FPS target. Mode 1 computes its timestep
  // from elapsed time in 82720740; do not modify +120 or +132.
  const uint64_t period=fps==0?1000000:(1000000000ull+std::clamp(fps,10,1000)-1)/std::clamp(fps,10,1000);
  if(period==read(72,8)) return false;
  for(size_t i=0;i<8;++i) object[72+i]=uint8_t(period>>((7-i)*8));
  return true;
}

// Experimental (sr_native_guest_sim_scheduler, off by default): the fixed-step
// SIM peer (821780C0, vtable 82005BC4, mode 0, 30 Hz). Mode 0 feeds +72 into
// the callback timestep in 82720740, so a shorter period gives more, shorter
// simulation steps. Systems that count ticks instead of time would run faster,
// and a SIM step slower than the period slows game time: measure both.
// Only explicit 31..240 targets are admitted; unlimited keeps the original 30 Hz.
inline bool ApplyGuestSimSchedulerPeriod(std::span<uint8_t> object, int32_t fps) {
  if(object.size()<176 || fps<=30 || fps>240) return false;
  const auto read=[&](size_t offset,size_t size) {
    uint64_t value=0;for(size_t i=0;i<size;++i) value=(value<<8)|object[offset+i];return value;
  };
  if(read(0,4)!=0x82005BC4 || read(52,4)!=0 || read(60,4)!=0x41F00000 ||
      read(72,8)!=33333334) return false;
  const uint64_t period=(1000000000ull+fps-1)/fps;
  for(size_t i=0;i<8;++i) object[72+i]=uint8_t(period>>((7-i)*8));
  return true;
}
}
