#include "stacked_resolve_policy.h"
#include "test_main.h"
#include <array>

using namespace superman_returns::native;
namespace {
constexpr std::array<uint32_t,6> kShadowFetch={0x85000002,0x1E820458,0x0C4FE27F,0x01281400,3,0x200};
}

SR_TEST(stacked_resolve_preserves_all_four_observed_shadow_slices) {
  for(uint32_t slice=0;slice<4;++slice) {
    auto plan=PlanStackedResolve(kShadowFetch,0,slice);
    SR_CHECK(plan.has_value());
    if(!plan) continue;
    SR_CHECK_EQ(plan->width,640u);SR_CHECK_EQ(plan->height,640u);
    SR_CHECK_EQ(plan->layers,4u);SR_CHECK_EQ(plan->destination_z,slice);
  }
}
SR_TEST(stacked_resolve_rejects_unsupported_mip_layer_and_volume_extents) {
  SR_CHECK(!PlanStackedResolve(kShadowFetch,1,0));
  SR_CHECK(!PlanStackedResolve(kShadowFetch,0,4));
  SR_CHECK(!PlanStackedResolve(kShadowFetch,0,0xFFFFFFFF));
  auto mip_chain=kShadowFetch;mip_chain[4]|=1u<<6;
  SR_CHECK(!PlanStackedResolve(mip_chain,0,0));
  auto too_wide=kShadowFetch;too_wide[2]=(too_wide[2]&~8191u)|8191u;
  SR_CHECK(!PlanStackedResolve(too_wide,0,0));
}
SR_TEST(stacked_resolve_does_not_convert_ordinary_cube_or_unbound_fetches) {
  auto fetch=kShadowFetch;fetch[1]&=~(1u<<10);
  SR_CHECK(!PlanStackedResolve(fetch,0,0));
  fetch=kShadowFetch;fetch[5]=3u<<9;
  SR_CHECK(!PlanStackedResolve(fetch,0,0));
  fetch=kShadowFetch;fetch[0]&=~3u;
  SR_CHECK(!PlanStackedResolve(fetch,0,0));
}
SR_TEST(stacked_resolve_binding_matches_actual_view_and_keeps_legacy_fallback) {
  SR_CHECK_EQ(ResolvedFetchDimension(kShadowFetch,false),1u);
  SR_CHECK_EQ(ResolvedFetchDimension(kShadowFetch,true),2u);
  auto ordinary=kShadowFetch;ordinary[1]&=~(1u<<10);
  SR_CHECK_EQ(ResolvedFetchDimension(ordinary,false),1u);
  SR_CHECK_EQ(ResolvedFetchDimension(ordinary,true),1u);
}
