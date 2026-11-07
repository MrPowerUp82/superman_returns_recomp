#pragma once
#include <cstdint>
#include "motion_raster_coverage.h"
namespace superman_returns::graphics {
struct RayGeometryOpacity {
  uint64_t vs=0,ps=0;
  uint32_t blend=0,color_control=0,color_mask=0,spec=0,owner=0;
  float output_scale=0;
  bool depth_write=false,original_shader=false,coverage_qualified=false;
};
inline bool IsOpaqueRayGeometry(const RayGeometryOpacity& draw) {
  if(!draw.depth_write) return false;
  if((draw.blend&0x1fffu)==1 && !(draw.color_control&8u)) return true;
  // 3EF6 exports literal alpha1. Only normal decoding (bits0/6) may precede
  // that export; all known alpha modifiers and unknown spec bits reject it.
  return draw.vs==0xC687EB65D635C5D7ull && draw.ps==0x3EF6A4A3C740ADB1ull &&
      draw.original_shader && draw.coverage_qualified && (draw.spec&~65u)==0 &&
      draw.output_scale==1.f && (draw.blend&0x1fffu)==0x706u &&
      MotionBlendHasSingleOwner(draw.blend,draw.owner,draw.color_mask);
}
}
