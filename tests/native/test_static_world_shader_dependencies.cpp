#include "test_main.h"
#include "../../port/src/graphics/post_vertex_geometry.h"
#include "../../port/src/graphics/static_world_shader_dependencies.h"

#include <array>
#include <bit>
#include <cstdint>

using namespace superman_returns::graphics;

namespace {
struct RigidFamily {
  uint64_t shader;
  uint32_t world_first_row;
  uint32_t clip_first_row;
  uint32_t world_texcoord;
};
// Independent expectations from each artifact's aliases and output chain.
constexpr std::array<RigidFamily, 5> kRigidFamilies{{
    {0x4F17209D364D5B51ull, 0, 8, 1},
    {0x18B4F69400B7F0AFull, 0, 8, 1},
    {0xC5E8551EFDE01162ull, 1, 5, 2},
    {0xC63F6F22679609F0ull, 0, 8, 1},
    {0x7D1AA67BBDC418D1ull, 0, 4, 4},
}};
constexpr std::array<uint64_t, 2> kInstancedShaders{
    0x29C1ACD9F4373197ull, 0xC79CC997C897773Eull};

struct RigidConstants {
  std::array<uint32_t, 1024> floats{};
  std::array<uint32_t, 4> booleans{};
  std::array<uint32_t, 32> loops{};

  RigidConstants() {
    for (uint32_t i = 0; i < floats.size(); ++i)
      floats[i] = std::bit_cast<uint32_t>(float(i) + 0.25f);
  }

  bool Capture(uint64_t shader, StaticWorldShaderDependencies& result) const {
    return CaptureStaticWorldShaderDependencies(shader, floats, booleans, loops, result);
  }

  bool Capture(uint64_t shader, uint32_t first, uint32_t end,
               StaticWorldShaderDependencies& result) const {
    return CaptureStaticWorldShaderDependencies(shader, floats, booleans, loops, first, end, result);
  }
};
}  // namespace

SR_TEST(static_world_family_captures_rigid_world_and_separate_object_to_clip_rows) {
  const RigidConstants constants;
  for (const auto& [shader, world_first, clip_first, texcoord] : kRigidFamilies) {
    StaticWorldShaderDependencies result;
    const bool captured = constants.Capture(shader, result);
    SR_CHECK(captured);
    if (!captured) continue;
    for (uint32_t word = 0; word < 16; ++word) {
      SR_CHECK_EQ(result.position_words[word], constants.floats[world_first * 4 + word]);
      SR_CHECK_EQ(result.clip_rows[word], constants.floats[clip_first * 4 + word]);
    }
    SR_CHECK_EQ(result.position_words[16], uint32_t(shader));
    SR_CHECK_EQ(result.position_words[17], uint32_t(shader >> 32));
    SR_CHECK_EQ(result.position_words[18], 2u);  // Expanded signature schema.
    SR_CHECK_EQ(result.position_words[19], texcoord);
    // The external capture allowlist has not integrated the new source domains.
    if (shader == 0x4F17209D364D5B51ull || shader == 0x18B4F69400B7F0AFull)
      SR_CHECK_EQ(result.position_words[19], WorldPositionTexcoord(shader));
    SR_CHECK_EQ(result.clip_first_row, clip_first);
    SR_CHECK(result.clip_matrix_kind == StaticWorldClipMatrixKind::kObjectToClip);
    for (uint32_t word = 20; word < result.position_words.size(); ++word)
      SR_CHECK_EQ(result.position_words[word], 0u);
  }
}

SR_TEST(static_world_family_every_world_matrix_word_invalidates_unchanged_world) {
  for (const auto& family : kRigidFamilies) {
    const uint64_t shader = family.shader;
    const RigidConstants original;
    StaticWorldShaderDependencies before;
    const bool captured = original.Capture(shader, before);
    SR_CHECK(captured);
    if (!captured) continue;
    for (uint32_t word = 0; word < 16; ++word) {
      auto changed = original;
      changed.floats[family.world_first_row * 4 + word] =
          std::bit_cast<uint32_t>(-100.f - float(word));
      StaticWorldShaderDependencies after;
      SR_CHECK(changed.Capture(shader, after));
      SR_CHECK(before.position_words != after.position_words);
      SR_CHECK(before.clip_rows == after.clip_rows);
    }
  }
}

SR_TEST(static_world_family_clip_bank_changes_are_returned_separately_including_clip_w) {
  for (const auto& family : kRigidFamilies) {
    const uint64_t shader = family.shader;
    const RigidConstants original;
    StaticWorldShaderDependencies before;
    const bool captured = original.Capture(shader, before);
    SR_CHECK(captured);
    if (!captured) continue;
    for (uint32_t word = 0; word < 16; ++word) {
      auto changed = original;
      changed.floats[family.clip_first_row * 4 + word] =
          std::bit_cast<uint32_t>(-200.f - float(word));
      StaticWorldShaderDependencies after;
      SR_CHECK(changed.Capture(shader, after));
      SR_CHECK(before.position_words == after.position_words);
      SR_CHECK(before.clip_rows != after.clip_rows);
      SR_CHECK_EQ(after.clip_rows[word], changed.floats[family.clip_first_row * 4 + word]);
      SR_CHECK(before != after);
    }
  }
}

SR_TEST(static_world_family_ignores_unread_shading_banks_and_unused_boolean_loop_words) {
  for (const auto& family : kRigidFamilies) {
    const uint64_t shader = family.shader;
    RigidConstants constants;
    StaticWorldShaderDependencies before, after;
    const bool captured = constants.Capture(shader, before);
    SR_CHECK(captured);
    if (!captured) continue;
    for (uint32_t word = 0; word < constants.floats.size(); ++word) {
      if (word >= family.world_first_row * 4 && word < family.world_first_row * 4 + 16)
        continue;
      if (word >= family.clip_first_row * 4 && word < family.clip_first_row * 4 + 16)
        continue;
      constants.floats[word] = 0x7fc00000u;  // Unread NaNs do not poison the proof.
    }
    constants.booleans.fill(UINT32_MAX);
    constants.loops.fill(UINT32_MAX);
    SR_CHECK(constants.Capture(shader, after));
    SR_CHECK(before == after);
  }
}

SR_TEST(static_world_family_nonfinite_position_or_clip_dependency_fails_atomically) {
  constexpr std::array<uint32_t, 3> nonfinite{0x7f800000u, 0xff800000u, 0x7fc00001u};
  for (const auto& family : kRigidFamilies) {
    const uint64_t shader = family.shader;
    const RigidConstants original;
    StaticWorldShaderDependencies sentinel;
    sentinel.position_words.fill(0xdeadbeefu);
    sentinel.clip_rows.fill(0xcafebabeu);
    sentinel.clip_first_row = 240;
    bool rejected_all = true;
    bool preserved_all = true;
    for (uint32_t first : {family.world_first_row * 4, family.clip_first_row * 4}) {
      for (uint32_t word = first; word < first + 16; ++word) {
        for (uint32_t invalid : nonfinite) {
          auto changed = original;
          changed.floats[word] = invalid;
          auto result = sentinel;
          rejected_all &= !changed.Capture(shader, result);
          preserved_all &= result == sentinel;
        }
      }
    }
    SR_CHECK(rejected_all);
    SR_CHECK(preserved_all);
  }
}

SR_TEST(static_world_family_unknown_instanced_and_skinned_shaders_remain_unverified) {
  constexpr std::array<uint64_t, 13> unsupported{
      0, 0xffffffffffffffffull, 0x2C45BFFE76D77398ull,
      0x9A62E7C65051447Aull, 0x008A68F78E2E3502ull,
      0x32EAA2870BB32915ull, 0x406390546D9691FAull,
      0x770469DDF5AB09DCull, 0x9B2F97272E1F510Bull,
      0xC687EB65D635C5D7ull, 0xC20921D0DBBD2F1Full,
      0xE57FB2C0DC954BAAull, 0xEC4D2CD998957026ull};
  RigidConstants constants;
  constants.booleans.fill(UINT32_MAX);
  constants.loops.fill(UINT32_MAX);
  StaticWorldShaderDependencies sentinel;
  SR_CHECK(constants.Capture(kRigidFamilies[0].shader, sentinel));
  for (uint64_t shader : unsupported) {
    auto result = sentinel;
    SR_CHECK(!constants.Capture(shader, result));
    SR_CHECK(result == sentinel);
  }
}

SR_TEST(static_world_family_signatures_keep_shader_source_domains_distinct) {
  RigidConstants constants;
  constants.floats.fill(std::bit_cast<uint32_t>(1.f));
  for (uint32_t i = 0; i < kRigidFamilies.size(); ++i) {
    for (uint32_t j = i + 1; j < kRigidFamilies.size(); ++j) {
      StaticWorldShaderDependencies first, second;
      SR_CHECK(constants.Capture(kRigidFamilies[i].shader, first));
      SR_CHECK(constants.Capture(kRigidFamilies[j].shader, second));
      SR_CHECK(first.position_words != second.position_words);
      SR_CHECK(first.clip_rows == second.clip_rows);
      for (uint32_t word = 0; word < 16; ++word)
        SR_CHECK_EQ(first.position_words[word], second.position_words[word]);
    }
  }
}

SR_TEST(static_world_family_preserves_finite_bits_and_never_inverts_projection) {
  constexpr std::array<uint32_t, 5> finite{
      0u, 0x80000000u, 1u, 0x7f7fffffu, 0xff7fffffu};
  for (const auto& family : kRigidFamilies) {
    const uint64_t shader = family.shader;
    for (uint32_t value : finite) {
      RigidConstants constants;
      constants.floats.fill(value);  // Includes singular matrices and signed zero.
      const auto original_floats = constants.floats;
      const auto original_booleans = constants.booleans;
      const auto original_loops = constants.loops;
      StaticWorldShaderDependencies result;
      const bool captured = constants.Capture(shader, result);
      SR_CHECK(captured);
      if (!captured) continue;
      for (uint32_t word = 0; word < 16; ++word) {
        SR_CHECK_EQ(result.position_words[word], value);
        SR_CHECK_EQ(result.clip_rows[word], value);
      }
      SR_CHECK(constants.floats == original_floats);
      SR_CHECK(constants.booleans == original_booleans);
      SR_CHECK(constants.loops == original_loops);
    }
  }
}

SR_TEST(static_world_instanced_requires_a_verified_nonempty_normalized_id_envelope) {
  RigidConstants constants;
  constants.floats[16] = std::bit_cast<uint32_t>(192.f);
  constants.floats[17] = 0;
  StaticWorldShaderDependencies sentinel;
  sentinel.position_words.fill(0x12345678u);
  sentinel.clip_rows.fill(0xabcdef01u);
  for (uint64_t shader : kInstancedShaders) {
    auto result = sentinel;
    SR_CHECK(!constants.Capture(shader, result));
    SR_CHECK(result == sentinel);
    for (const auto& range : std::array<std::array<uint32_t, 2>, 5>{
             {{0, 0}, {4, 4}, {8, 4}, {0, 0x1000001}, {0x1000000, 0x1000001}}}) {
      result = sentinel;
      SR_CHECK(!constants.Capture(shader, range[0], range[1], result));
      SR_CHECK(result == sentinel);
    }
  }
}

SR_TEST(static_world_instanced_delegates_audited_range_dependencies_and_returns_world_to_clip) {
  RigidConstants constants;
  constants.floats[16] = std::bit_cast<uint32_t>(100.5f);
  constants.floats[17] = 0;
  for (uint64_t shader : kInstancedShaders) {
    for (const auto& range : std::array<std::array<uint32_t, 2>, 4>{
             {{0, 50}, {100, 101}, {7700, 7900}, {0xffffff, 0x1000000}}}) {
      StaticWorldShaderDependencies result;
      const bool captured = constants.Capture(shader, range[0], range[1], result);
      SR_CHECK(captured);
      if (!captured) continue;
      std::array<uint32_t, 636> expected;
      SR_CHECK(CaptureStaticWorldDependenciesInVertexRange(
          0x2C45BFFE76D77398ull, constants.floats, range[0], range[1] - 1, expected));
      for (uint32_t word = 0; word < expected.size(); ++word)
        SR_CHECK_EQ(result.position_words[20 + word], expected[word]);
      for (uint32_t word = 0; word < 16; ++word) {
        SR_CHECK_EQ(result.position_words[word], 0u);
        SR_CHECK_EQ(result.clip_rows[word], constants.floats[word]);
      }
      SR_CHECK_EQ(result.position_words[16], uint32_t(shader));
      SR_CHECK_EQ(result.position_words[17], uint32_t(shader >> 32));
      SR_CHECK_EQ(result.position_words[18], 2u);
      SR_CHECK_EQ(result.position_words[19], UINT32_MAX);  // No exported world varying.
      SR_CHECK_EQ(result.position_words[19], WorldPositionTexcoord(shader));
      SR_CHECK_EQ(result.clip_first_row, 0u);
      SR_CHECK(result.clip_matrix_kind == StaticWorldClipMatrixKind::kWorldToClip);
    }
  }
}

SR_TEST(static_world_instanced_separates_camera_and_keeps_reachable_xyz_and_remap) {
  RigidConstants constants;
  constants.floats[16] = std::bit_cast<uint32_t>(192.f);
  constants.floats[17] = 0;
  for (uint64_t shader : kInstancedShaders) {
    StaticWorldShaderDependencies before;
    const bool captured = constants.Capture(shader, 0, 192, before);
    SR_CHECK(captured);
    if (!captured) continue;
    for (uint32_t word = 0; word < 16; ++word) {
      auto changed = constants;
      changed.floats[word] = std::bit_cast<uint32_t>(-300.f - float(word));
      StaticWorldShaderDependencies after;
      SR_CHECK(changed.Capture(shader, 0, 192, after));
      SR_CHECK(before.position_words == after.position_words);
      SR_CHECK(before.clip_rows != after.clip_rows);
    }
    for (uint32_t row : {100u, 101u}) {
      for (uint32_t component = 0; component < 3; ++component) {
        auto changed = constants;
        changed.floats[row * 4 + component] = std::bit_cast<uint32_t>(-17.f);
        StaticWorldShaderDependencies after;
        SR_CHECK(changed.Capture(shader, 0, 192, after));
        SR_CHECK(before.position_words != after.position_words);
        SR_CHECK(before.clip_rows == after.clip_rows);
      }
    }
    for (uint32_t word : {16u, 17u}) {
      auto changed = constants;
      changed.floats[word] = std::bit_cast<uint32_t>(256.f);
      StaticWorldShaderDependencies after;
      SR_CHECK(changed.Capture(shader, 0, 192, after));
      SR_CHECK(before.position_words != after.position_words);
    }
    auto changed = constants;
    for (uint32_t word = 18; word < changed.floats.size(); ++word) {
      if ((word / 4 == 100 || word / 4 == 101) && word % 4 < 3) continue;
      changed.floats[word] = 0x7fc00000u;
    }
    changed.booleans.fill(UINT32_MAX);
    changed.loops.fill(UINT32_MAX);
    StaticWorldShaderDependencies after;
    SR_CHECK(changed.Capture(shader, 0, 192, after));
    SR_CHECK(before == after);  // Includes ignored c100.w/c101.w and light rows 229..231.
  }
}

SR_TEST(static_world_instanced_reciprocal_boundary_keeps_both_instances) {
  RigidConstants constants;
  constants.floats[16] = std::bit_cast<uint32_t>(100.5f);
  constants.floats[17] = 0;
  for (uint64_t shader : kInstancedShaders) {
    StaticWorldShaderDependencies before;
    const bool captured = constants.Capture(shader, 100, 101, before);
    SR_CHECK(captured);
    if (!captured) continue;
    for (uint32_t row = 100; row <= 103; ++row) {
      auto changed = constants;
      changed.floats[row * 4] = std::bit_cast<uint32_t>(-13.f);
      StaticWorldShaderDependencies after;
      SR_CHECK(changed.Capture(shader, 100, 101, after));
      SR_CHECK(before.position_words != after.position_words);
    }
  }
}

SR_TEST(static_world_instanced_rejects_bad_stride_and_nonfinite_reachable_dependencies_atomically) {
  constexpr std::array<uint32_t, 3> nonfinite{0x7f800000u, 0xff800000u, 0x7fc00001u};
  RigidConstants constants;
  constants.floats[16] = std::bit_cast<uint32_t>(192.f);
  constants.floats[17] = 0;
  StaticWorldShaderDependencies sentinel;
  sentinel.position_words.fill(0x76543210u);
  sentinel.clip_rows.fill(0x10203040u);
  for (uint64_t shader : kInstancedShaders) {
    bool rejected_all = true;
    bool preserved_all = true;
    for (uint32_t word = 0; word < constants.floats.size(); ++word) {
      const bool read = word < 18 ||
          ((word / 4 == 100 || word / 4 == 101) && word % 4 < 3);
      if (!read) continue;
      for (uint32_t invalid : nonfinite) {
        auto changed = constants;
        changed.floats[word] = invalid;
        auto result = sentinel;
        rejected_all &= !changed.Capture(shader, 0, 192, result);
        preserved_all &= result == sentinel;
      }
    }
    for (float stride : {-1.f, 0.f, 0.5f, 16777218.f}) {
      auto changed = constants;
      changed.floats[16] = std::bit_cast<uint32_t>(stride);
      auto result = sentinel;
      rejected_all &= !changed.Capture(shader, 0, 192, result);
      preserved_all &= result == sentinel;
    }
    SR_CHECK(rejected_all);
    SR_CHECK(preserved_all);
  }
}

SR_TEST(static_world_family_range_overload_keeps_rigid_support_and_rejects_other_families) {
  const RigidConstants constants;
  for (const auto& family : kRigidFamilies) {
    const uint64_t shader = family.shader;
    StaticWorldShaderDependencies expected;
    SR_CHECK(constants.Capture(shader, expected));
    // These rigid sources never consume SV_VertexID: only instancing needs
    // an envelope. Both overloads must return the same bounded bank proof.
    for (const auto& range : std::array<std::array<uint32_t, 2>, 4>{
             {{0, 1}, {0, 0}, {8, 4}, {0xffffff, 0x1000001}}}) {
      StaticWorldShaderDependencies actual;
      SR_CHECK(constants.Capture(shader, range[0], range[1], actual));
      SR_CHECK(actual == expected);
    }
  }
  for (uint64_t shader : {0xE57FB2C0DC954BAAull, 0x32EAA2870BB32915ull, 0ull}) {
    StaticWorldShaderDependencies sentinel;
    sentinel.position_words.fill(123);
    auto result = sentinel;
    SR_CHECK(!constants.Capture(shader, 0, 1, result));
    SR_CHECK(result == sentinel);
  }
}
