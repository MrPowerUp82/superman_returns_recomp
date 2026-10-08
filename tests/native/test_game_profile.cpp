// game_profile.h invariants that must hold whatever the owner confirms.
// (tests/tools/test_game_profile.py cross-checks the candidates against
// docs/data/xdk_match.tsv.)

#include <cstdint>
#include <set>
#include <string>

#include "game_profile.h"
#include "test_main.h"

namespace profile = superman_returns::native::profile;

SR_TEST(profile_has_every_kit_hook_role) {
  // The 21 kit roles plus the common unlock and planar handoff observations.
  SR_CHECK_EQ(sizeof(profile::kHookRoles) / sizeof(profile::kHookRoles[0]), size_t(23));
  std::set<std::string> names;
  for (const auto& r : profile::kHookRoles) names.insert(r.role);
  SR_CHECK_EQ(names.size(), size_t(23));
}

SR_TEST(hook_addresses_are_code_addresses) {
  for (const auto& r : profile::kHookRoles) {
    if (r.absent) continue;
    const bool in_image = r.address >= profile::kImageBase && r.address < profile::kImageEnd;
    if (!in_image) std::printf("  role %s address %08X\n", r.role, r.address);
    SR_CHECK(in_image);
    SR_CHECK_EQ(r.address & 3u, 0u);
  }
}

SR_TEST(hook_addresses_are_distinct) {
  std::set<uint32_t> seen;
  for (const auto& r : profile::kHookRoles) {
    if (r.absent) continue;
    const bool fresh = seen.insert(r.address).second;
    if (!fresh) std::printf("  duplicate address %08X (%s)\n", r.address, r.role);
    SR_CHECK(fresh);
  }
}

SR_TEST(absent_roles_are_confirmed) {
  // SR_ABSENT_x only means something once the owner confirmed it.
  for (const auto& r : profile::kHookRoles) {
    if (r.absent) SR_CHECK(r.confirmed);
  }
}

SR_TEST(gate_requires_every_role_and_the_layout) {
  bool all = true;
  for (const auto& r : profile::kHookRoles) all &= r.confirmed;
  const bool expected = all && profile::kDeviceLayoutConfirmed &&
                        (profile::kDevicePtrAddr == 0 || profile::kDevicePtrConfirmed);
  SR_CHECK_EQ(profile::kProfileConfirmed, expected);
}

SR_TEST(swap_role_matches_frame_stats_hook_or_is_separate) {
  // frame_stats.cpp hooks sub_82112050; native_hooks.cpp defines its own Swap
  // hook only for another address. Either way exactly one hook exists.
  const auto& swap = profile::kHookRoles[18];
  SR_CHECK(std::string(swap.role) == "Swap");
  SR_CHECK_EQ(uint32_t(SR_FRAME_STATS_SWAP_HOOK), 0x82112050u);
}

SR_TEST(device_layout_is_ordered_like_the_xdk_struct) {
  const auto& d = profile::kDevice;
  SR_CHECK(d.fetch_constants < d.vs_constants);
  SR_CHECK_EQ(d.ps_constants - d.vs_constants, 256u * 16u);
  SR_CHECK(d.render_targets + 16 == d.depth_stencil);
  SR_CHECK(d.index_buffer < d.render_targets);
  SR_CHECK(d.textures + 26 * 4 <= d.viewport);
  SR_CHECK(d.viewport + 24 <= d.shader_a);
  SR_CHECK(d.shader_b == d.shader_a + 4);
  SR_CHECK(d.fence_current < d.size && d.fence_completed_ptr < d.size);
  for (const auto& r : profile::kRegisterShadow) {
    SR_CHECK(r.offset + 4 * r.count <= d.vertex_decl);
    SR_CHECK(r.offset >= d.register_shadow);
  }
}

SR_TEST(output_size_is_720p) {
  SR_CHECK_EQ(profile::kOutputWidth, 1280u);
  SR_CHECK_EQ(profile::kOutputHeight, 720u);
  SR_CHECK(profile::kSceneWidth > 0);
}
