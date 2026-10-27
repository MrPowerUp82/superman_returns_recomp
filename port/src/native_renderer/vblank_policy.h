#pragma once
#include <algorithm>
#include <cstdint>

namespace superman_returns::native {
struct VblankCadence {
  uint32_t hz;
  bool unlocked;
};
inline VblankCadence SelectNativeVblankCadence(int32_t fps, bool vsync,
                                               int32_t unlocked_hz=1000) {
  // Xenia Canary: no limit + VSync falls back to 60 vblanks/s; disabling
  // VSync dispatches a vblank then sleeps 1 ms. Keep this project's explicit
  // FPS choices in their established two-vblanks-per-frame units.
  if(fps>0) return {uint32_t(2*std::clamp(fps,10,1000)),false};
  if(vsync) return {60,false};
  return {uint32_t(std::clamp(unlocked_hz,1,2000)),true};
}
} // namespace superman_returns::native
