#pragma once
#include <cstdint>
namespace superman_returns::graphics {
// A positive original_pixel_owner is allowed only after the audited original
// PSO actually bound its private MRT. The replay must retain owner-ID filtering,
// final-depth EQUAL, original raster geometry and unknown invalidators.
inline bool CanReplayMotionRasterCoverage(uint32_t samples,bool depth_enabled,
    bool depth_write,bool stencil_enabled,bool debug_no_depth,
    uint32_t blend_control,uint32_t color_control,uint32_t original_pixel_owner) {
  if(samples!=1 || !depth_enabled || !depth_write || debug_no_depth) return false;
  // The original MRT write already passed its original stencil/alpha tests.
  // Later stencil contents cannot reconstruct that historical coverage.
  if(original_pixel_owner) return true;
  return !stencil_enabled && (blend_control&0x1fff)==1 &&
      (!(color_control&8) || (color_control&7)==7);
}
}
