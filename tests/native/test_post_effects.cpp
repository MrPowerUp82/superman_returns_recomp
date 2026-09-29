// post_effects.h: the sr_post_effects=false filter on synthetic draw/copy
// sequences shaped like the gameplay trace.

#include <cstdint>

#include "post_effects.h"
#include "test_main.h"

namespace pe = superman_returns::post_effects;

namespace {

constexpr uint32_t kSceneSurface = 335545600;  // 1280, the scene
constexpr uint32_t kTriangleList = 4;
constexpr uint32_t kRectangleList = 8;
constexpr uint64_t kResolveVs = 0x72CBCAA6A7984111ull;
constexpr uint64_t kCompositeVs = 0x871A3860C63CB28Dull;
constexpr uint64_t kCompositePs = 0x2679C172F83DE85Dull;  // kept 1280 pass

// Mirrors SrCommandProcessor: a resolve reaches the filter as a mode-6 draw
// and then as a copy, with the previous pixel shader still bound.
bool Resolve(pe::Filter& f, uint64_t ps, uint32_t surface) {
  SR_CHECK(!f.SkipDraw(kRectangleList, pe::kModeCopy, kResolveVs, ps, surface));
  return f.SkipCopy(ps, surface);
}

}  // namespace

SR_TEST(post_effects_table_is_consistent) {
  SR_CHECK(pe::kPassCount > 0);
  for (size_t i = 0; i < pe::kPassCount; ++i) {
    const pe::Pass& p = pe::kPasses[i];
    SR_CHECK(p.vs != 0 && p.ps != 0);
    // Only surfaces narrower than the 1280 scene (pitch = low 14 bits).
    SR_CHECK((p.surface & 0x3FFF) < 1280);
    SR_CHECK(p.surface != kSceneSurface);
    for (size_t j = i + 1; j < pe::kPassCount; ++j) {
      const pe::Pass& q = pe::kPasses[j];
      SR_CHECK(!(p.vs == q.vs && p.ps == q.ps && p.surface == q.surface));
    }
  }
}

SR_TEST(post_effects_skip_listed_quad_and_its_resolve) {
  pe::Filter f;
  const pe::Pass& p = pe::kPasses[0];
  SR_CHECK(f.SkipDraw(pe::kQuadList, pe::kModeColorDepth, p.vs, p.ps, p.surface));
  SR_CHECK(Resolve(f, p.ps, p.surface));
  SR_CHECK_EQ(f.skipped_draws(), uint64_t(1));
  SR_CHECK_EQ(f.skipped_copies(), uint64_t(1));
}

SR_TEST(post_effects_whole_chain_is_skipped) {
  pe::Filter f;
  for (const pe::Pass& p : pe::kPasses) {
    SR_CHECK(f.SkipDraw(pe::kQuadList, pe::kModeColorDepth, p.vs, p.ps, p.surface));
    SR_CHECK(Resolve(f, p.ps, p.surface));
  }
  SR_CHECK_EQ(f.skipped_draws(), uint64_t(pe::kPassCount));
  SR_CHECK_EQ(f.skipped_copies(), uint64_t(pe::kPassCount));
}

SR_TEST(post_effects_keep_other_primitives_modes_and_surfaces) {
  pe::Filter f;
  const pe::Pass& p = pe::kPasses[0];
  SR_CHECK(!f.SkipDraw(kTriangleList, pe::kModeColorDepth, p.vs, p.ps, p.surface));
  SR_CHECK(!f.SkipDraw(pe::kQuadList, 5, p.vs, p.ps, p.surface));
  SR_CHECK(!f.SkipDraw(pe::kQuadList, pe::kModeColorDepth, p.vs, p.ps, kSceneSurface));
  SR_CHECK(!f.SkipDraw(pe::kQuadList, pe::kModeColorDepth, p.vs + 1, p.ps, p.surface));
  SR_CHECK(!f.SkipDraw(pe::kQuadList, pe::kModeColorDepth, kCompositeVs, kCompositePs,
                       kSceneSurface));
  SR_CHECK_EQ(f.skipped_draws(), uint64_t(0));
}

SR_TEST(post_effects_keep_resolves_not_after_a_skipped_pass) {
  pe::Filter f;
  const pe::Pass& p = pe::kPasses[0];
  // No skipped pass before: a resolve with a listed shader bound stays.
  SR_CHECK(!Resolve(f, p.ps, p.surface));
  // A kept draw in between closes the window.
  SR_CHECK(f.SkipDraw(pe::kQuadList, pe::kModeColorDepth, p.vs, p.ps, p.surface));
  SR_CHECK(!f.SkipDraw(pe::kQuadList, pe::kModeColorDepth, kCompositeVs, kCompositePs,
                       kSceneSurface));
  SR_CHECK(!Resolve(f, p.ps, p.surface));
  // A resolve of another surface (e.g. the scene) right after a skipped pass stays.
  SR_CHECK(f.SkipDraw(pe::kQuadList, pe::kModeColorDepth, p.vs, p.ps, p.surface));
  SR_CHECK(!Resolve(f, p.ps, kSceneSurface));
  SR_CHECK_EQ(f.skipped_copies(), uint64_t(0));
}

SR_TEST(post_effects_repeated_resolves_of_a_skipped_pass) {
  pe::Filter f;
  const pe::Pass& p = pe::kPasses[1];
  SR_CHECK(f.SkipDraw(pe::kQuadList, pe::kModeColorDepth, p.vs, p.ps, p.surface));
  SR_CHECK(Resolve(f, p.ps, p.surface));
  SR_CHECK(Resolve(f, p.ps, p.surface));
  SR_CHECK_EQ(f.skipped_copies(), uint64_t(2));
}
