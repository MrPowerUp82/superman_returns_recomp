#pragma once
#include <cstdint>
namespace superman_returns::graphics {
inline constexpr uint32_t kMotionOwnerIdMask=0x3fffffffu;
inline constexpr uint32_t kMotionOwnerMixedAlpha=0x40000000u;
inline constexpr uint32_t kMotionOwnerRequireOpaqueAlpha=0x80000000u;
inline bool MotionBlendHasSingleOwner(uint32_t blend,uint32_t owner,uint32_t color_mask) {
  const uint32_t rgb=blend&0x1fffu;
  return rgb==1 || (rgb==0x706u && owner && owner<=4096 && (color_mask&7u)==7u);
}
inline uint32_t MotionPixelOwnerConstant(uint32_t owner,uint32_t blend,uint32_t color_mask) {
  return (blend&0x1fffu)==0x706u && MotionBlendHasSingleOwner(blend,owner,color_mask) ?
      owner|kMotionOwnerRequireOpaqueAlpha : owner;
}
// Numeric D3D comparison domain (1 NEVER .. 8 ALWAYS); independent of backend.
inline uint32_t MotionInvalidatorDepthComparison(bool enabled,bool writes,bool debug_disabled,uint32_t guest_compare) {
  if(!enabled || debug_disabled) return 8;
  return writes ? 3 : (guest_compare&7u)+1;
}
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
