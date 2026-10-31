#include "test_main.h"
#include "../../port/src/graphics/shadow_projection.h"
#include "../../port/src/graphics/fixed16_color.h"
#include <algorithm>
#include <limits>
using namespace superman_returns::graphics;
SR_TEST(shadow_fixed16_atlas_preserves_depth_order_across_projection_changes) {
  // The caster exports 32*Z into RG16. Exercise normalized stored samples
  // after the host RTV conversion, including the far clear value. Clamping
  // the unscaled caster first collapses every positive depth >= 1/32 to 1.
  const float scale = ColorOutputScale(4);
  float previous = -2;
  for (float z : {-0.75f, -0.25f, 0.0f, 0.03125f, 0.25f, 0.5f, 0.75f, 1.0f}) {
    const float stored = std::clamp((32.0f * z) * scale, -1.0f, 1.0f);
    SR_CHECK(std::abs(stored - z) < 1e-6f);
    SR_CHECK(stored > previous);
    previous = stored;
  }
  // Translating the light projection as the camera moves must preserve the
  // same receiver/blocker depth difference, rather than move a clamp boundary.
  for (float shift : {-0.2f, 0.0f, 0.2f, 0.4f}) {
    const float blocker = 0.3f + shift, receiver = blocker + 0.05f;
    const float stored = std::clamp(32.0f * blocker * scale, -1.0f, 1.0f);
    SR_CHECK(std::abs(receiver - stored - 0.05f) < 1e-6f);
  }
  const float clear[4] = {32, -16, 0, 1};
  const auto normalized = ColorClearValue(4, clear);
  SR_CHECK(normalized == (std::array<float, 4>{1, -0.5f, 0, 1.0f / 32}));
  for (uint32_t format : {0u, 3u, 5u, 6u, 7u, 12u, 14u, 15u}) {
    SR_CHECK(ColorOutputScale(format) == 1.0f);
    const auto unchanged = ColorClearValue(format, clear);
    SR_CHECK(unchanged == (std::array<float, 4>{32, -16, 0, 1}));
  }
}
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
