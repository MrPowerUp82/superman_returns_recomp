#include "test_main.h"
#include "../../port/src/graphics/shadow_projection.h"
#include <limits>
using namespace superman_returns::graphics;
SR_TEST(shadow_projection_ray_preserves_uv_and_decreases_stored_depth) {
  // Affine projection may be scaled and sheared. A light ray holds U/V
  // constant and moves towards decreasing stored depth, not world -Z.
  ShadowProjection projection{{2,0,0,7},{0,0,3,-9},{1,-4,2,0.5f}};
  const auto ray=ShadowRayDirection(projection);
  SR_CHECK(ray.valid);
  SR_CHECK(std::abs(ray.xyz[0])<1e-6f && ray.xyz[1]>0.999f && std::abs(ray.xyz[2])<1e-6f);
  auto scaled=projection;
  for(float& value:scaled.u) value*=-3;
  for(float& value:scaled.v) value*=0.25f;
  const auto same=ShadowRayDirection(scaled);
  SR_CHECK(same.valid);
  for(size_t i=0;i<3;++i) SR_CHECK(std::abs(same.xyz[i]-ray.xyz[i])<1e-6f);
  SR_CHECK(ShadowProjectionsMatch(projection,projection));
  scaled=projection; scaled.z[3]+=1;
  SR_CHECK(!ShadowProjectionsMatch(projection,scaled));
}
SR_TEST(shadow_projection_rejects_rank_deficient_nonfinite_and_unmatched_contracts) {
  ShadowProjection projection{{1,0,0,0},{0,1,0,0},{0,0,1,0}};
  auto invalid=projection; invalid.v=invalid.u;
  SR_CHECK(!ShadowRayDirection(invalid).valid);
  invalid=projection; invalid.z=invalid.u;
  SR_CHECK(!ShadowRayDirection(invalid).valid);
  invalid=projection; invalid.u[3]=std::numeric_limits<float>::quiet_NaN();
  SR_CHECK(!ShadowRayDirection(invalid).valid);
  SR_CHECK(!ShadowProjectionsMatch(invalid,invalid));
}
