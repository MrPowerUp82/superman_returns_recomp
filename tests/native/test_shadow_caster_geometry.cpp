#include "test_main.h"
#include "../../port/src/graphics/shadow_caster_geometry.h"
#include <limits>
using namespace superman_returns::graphics;
SR_TEST(shadow_caster_recovers_deformed_world_after_atlas_depth_bias_and_pixel_fix) {
  ShadowCasterContract c;
  c.projection={{.02f,0,.01f,.2f},{0,.03f,0,.1f},{.001f,-.002f,.004f,.3f}};
  c.atlas={std::array<float,4>{.1f,.2f,.3f,.5f},std::array<float,4>{.4f,.3f,.2f,.25f},std::array<float,4>{.05f,.1f,.15f,2.f}};
  c.depth_bias=.0125f; c.half_pixel={1.f/640,-1.f/640};
  c.screen={.8f,-.9f,.1f,-.2f};
  const std::array<float,4> world{5,-7,11,1};
  for(int mode=0;mode<=3;++mode) {
    c.mode=float(mode);
    std::array<float,3> zvu{};
    const std::array<std::array<float,4>,3> rows{c.projection.z,c.projection.v,c.projection.u};
    for(int r=0;r<3;++r) for(int i=0;i<4;++i) zvu[r]+=rows[r][i]*world[i];
    for(int a=0;a<mode;++a) for(int i=0;i<3;++i) zvu[i]=zvu[i]*c.atlas[a][3]+c.atlas[a][2-i];
    std::array<float,4> clip{2*zvu[2]-1,1-2*zvu[1],.5f,1};
    for(int i=0;i<2;++i) clip[i]=clip[i]*c.screen[i]+c.screen[i+2]+c.half_pixel[i];
    const auto recovered=RecoverShadowCasterWorld(c,clip,32*(zvu[0]+c.depth_bias));
    SR_CHECK(recovered.valid);
    for(int i=0;i<3;++i) SR_CHECK(std::abs(recovered.xyz[i]-world[i])<.001f);
  }
}
SR_TEST(shadow_caster_rejects_non_affine_clip_singular_atlas_and_nonfinite_contract) {
  ShadowCasterContract c;
  c.projection={{1,0,0,0},{0,1,0,0},{0,0,1,0}};
  SR_CHECK(!RecoverShadowCasterWorld(c,{0,0,.5f,2},1).valid);
  c.mode=1;
  SR_CHECK(!RecoverShadowCasterWorld(c,{0,0,.5f,1},1).valid);
  c.mode=0; c.projection.z=c.projection.u;
  SR_CHECK(!RecoverShadowCasterWorld(c,{0,0,.5f,1},1).valid);
  c.projection.z={0,0,1,std::numeric_limits<float>::infinity()};
  SR_CHECK(!RecoverShadowCasterWorld(c,{0,0,.5f,1},1).valid);
}
