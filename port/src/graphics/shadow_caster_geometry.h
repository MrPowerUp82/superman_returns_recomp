#pragma once
#include "shadow_projection.h"
#include "post_vertex_geometry.h"
namespace superman_returns::graphics {
inline bool IsOriginalShadowCaster(uint64_t shader) {
  return shader==0x57A00D4EB3BF4C31ull || shader==0xC7C0E381A3C9B79Eull;
}
// Heuristic: 57A applies a skin palette and C7C0 an object transform. Skinning
// does not prove a character (see docs/rt-dlss-continuation-2026-10-26.md).
inline bool IsCharacterShadowCaster(uint64_t shader) {
  return shader==0x57A00D4EB3BF4C31ull;
}
struct ShadowCasterContract {
  ShadowProjection projection;
  std::array<std::array<float,4>,3> atlas{};
  float mode=0,depth_bias=0;
  std::array<float,4> screen{1,1,0,0};
  std::array<float,2> half_pixel{};
};
// Both audited casters export clip=(2U-1,1-2V,.5,1) and TEXCOORD0.x=32*(Z+bias),
// after 0..3 affine atlas transforms. Undo those, not the constant clip Z.
// The result retains the original caster's deformation/thickness displacement;
// it is not an unbiased reflection mesh or a logical character identity.
inline RecoveredPosition RecoverShadowCasterWorld(const ShadowCasterContract& c,
    std::array<float,4> clip,float exported_depth) {
  for(float value:clip) if(!std::isfinite(value)) return {};
  for(float value:c.screen) if(!std::isfinite(value)) return {};
  for(float value:c.half_pixel) if(!std::isfinite(value)) return {};
  if(!std::isfinite(exported_depth) || !std::isfinite(c.depth_bias) || !std::isfinite(c.mode) ||
     std::abs(clip[3]-1)>1e-6f || std::abs(c.screen[0])<1e-12f || std::abs(c.screen[1])<1e-12f) return {};
  for(int i=0;i<2;++i) clip[i]=(clip[i]-c.screen[i+2]-c.half_pixel[i])/c.screen[i];
  std::array<float,3> zvu{exported_depth/32-c.depth_bias,(1-clip[1])/2,(clip[0]+1)/2};
  for(int a=2;a>=0;--a) if(c.mode>a) {
    const auto& scale=c.atlas[a];
    for(float value:scale) if(!std::isfinite(value)) return {};
    if(std::abs(scale[3])<1e-12f) return {};
    for(int i=0;i<3;++i) zvu[i]=(zvu[i]-scale[2-i])/scale[3];
  }
  Matrix4 matrix{},inverse{};
  for(int i=0;i<4;++i) {matrix[i]=c.projection.u[i]; matrix[4+i]=c.projection.v[i]; matrix[8+i]=c.projection.z[i];}
  matrix[15]=1;
  if(!InvertProjection(matrix,inverse)) return {};
  return RecoverWorldPosition(inverse,{zvu[2],zvu[1],zvu[0],1},{1,1,0,0},{0,0});
}
}
