#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>

#include "temporal_geometry_history.h"

namespace superman_returns::graphics {

enum class StaticWorldClipMatrixKind : uint32_t {
  kUnknown,
  // Precomposed WVP: not a shared world-to-clip camera matrix.
  kObjectToClip,
  kWorldToClip,
};

struct StaticWorldShaderDependencies {
  // Hash only these words for unchanged-world correspondence. [16..17] are
  // source hash, [18] schema 2, [19] world TEXCOORD (UINT32_MAX if not exported).
  // Rigid: [0..15] are all four world rows (c1..4 for C5E8, otherwise c0..3),
  // including the fourth row, normalized to this signature offset.
  // Instanced: [20..655] hold the existing 636-word audited range signature.
  // All unused words are zero; source domains remain distinct.
  std::array<uint32_t, 1200> position_words{};
  // Exact projection inputs, separate from world correspondence. Inspect kind:
  // rigid c5..8 (C5E8) / c4..7 (7D1AA) / c8..11 are OBJECT-to-clip;
  // instanced c0..3 are WORLD-to-clip. These are pre-shared-fixup rows only.
  std::array<uint32_t, 16> clip_rows{};
  uint32_t clip_first_row = 0;
  StaticWorldClipMatrixKind clip_matrix_kind = StaticWorldClipMatrixKind::kUnknown;
  bool operator==(const StaticWorldShaderDependencies&) const = default;
};

// This no-range entry accepts source-audited rigid 4F172/18B4F/C5E8/C63F/7D1AA. It
// deliberately rejects instanced 29C1/C79C without the range overload below.
// This describes unchanged world inputs;
// it never supplies moving-instance identity or verifies GPU buffer contents.
// Caller must additionally prove canonical mesh/fetch/resource versions and
// capture ACTUALLY UPLOADED shared screen-transform/half-pixel state outside
// these VS banks. API need: neither shared float input is supplied here;
// success is a VS-bank dependency proof, not a complete FINAL-clip proof.
// In both new sources only XY receives shared fixups; final Z/W are WVP dots.
// Unknown shaders or nonfinite read dependencies leave result untouched.
inline bool CaptureStaticWorldShaderDependencies(
    uint64_t source_hash, std::span<const uint32_t, 1024> vs_float_words,
    std::span<const uint32_t, 4> /*vs_bool_words*/,
    std::span<const uint32_t, 32> /*vs_loop_words*/,
    StaticWorldShaderDependencies& result) {
  uint32_t world_first_row = 0;
  uint32_t clip_first_row = 8;
  uint32_t world_texcoord = 1;
  switch (source_hash) {
    case 0x4F17209D364D5B51ull:
    case 0x18B4F69400B7F0AFull:
    // C63F: aliases 593..595; local 881..882; world 946..961/export 998;
    // clip XYZW 889..901, shared XY only 1068..1071 in the audited artifact.
    case 0xC63F6F22679609F0ull:
      break;
    // C5E8: aliases 594..595; local 886..887; world 951..971/export 996;
    // clip XYZW 894..906, shared XY only 1056..1059. c0 is shading-only.
    case 0xC5E8551EFDE01162ull:
      world_first_row = 1;
      clip_first_row = 5;
      world_texcoord = 2;
      break;
    // 7D1AA: source aliases596..597, position885..902, world927..942,
    // TEX4 export959, final shared XY1204..1206. Z clamps to LOCAL literal
    // c254.z=asfloat(0x3727C5AC), not a mutable guest bank/deformation input.
    case 0x7D1AA67BBDC418D1ull:
      clip_first_row = 4;
      world_texcoord = 4;
      break;
    default:
      return false;
  }

  StaticWorldShaderDependencies captured;
  for (uint32_t word = 0; word < 16; ++word) {
    const uint32_t world = vs_float_words[world_first_row * 4 + word];
    const uint32_t clip = vs_float_words[clip_first_row * 4 + word];
    if (!std::isfinite(std::bit_cast<float>(world)) ||
        !std::isfinite(std::bit_cast<float>(clip)))
      return false;
    captured.position_words[word] = world;
    captured.clip_rows[word] = clip;
  }
  // No accepted main body reads BOOL_BIT, g_LoopConstant, or a palette.
  captured.position_words[16] = uint32_t(source_hash);
  captured.position_words[17] = uint32_t(source_hash >> 32);
  captured.position_words[18] = 2;  // Expanded signature schema.
  captured.position_words[19] = world_texcoord;
  captured.clip_first_row = clip_first_row;
  captured.clip_matrix_kind = StaticWorldClipMatrixKind::kObjectToClip;
  result = captured;
  return true;
}

// first/end is a verified, normalized SV_VertexID envelope [first,end), after
// index normalization, restart exclusion, and base-vertex addition. Pass the
// frontend's proven u6/u7; neither raw index bounds nor guessed bounds suffice.
// Empty, inverted, and 24-bit-mask-wrapping ranges are rejected for instancing.
inline bool CaptureStaticWorldShaderDependencies(
    uint64_t source_hash, std::span<const uint32_t, 1024> vs_float_words,
    std::span<const uint32_t, 4> vs_bool_words,
    std::span<const uint32_t, 32> vs_loop_words,
    uint32_t normalized_vertex_first, uint32_t normalized_vertex_end,
    StaticWorldShaderDependencies& result) {
  if (source_hash != 0x29C1ACD9F4373197ull && source_hash != 0xC79CC997C897773Eull)
    return CaptureStaticWorldShaderDependencies(
        source_hash, vs_float_words, vs_bool_words, vs_loop_words, result);
  if (normalized_vertex_first >= normalized_vertex_end || normalized_vertex_end > 0x1000000u)
    return false;

  std::array<uint32_t, 636> ranged;
  // The audited 29C1/C79C expressions are 2C45-equivalent for masked ID,
  // trunc((ID+.5)*rcp(c4.x)), c4.xy remap, 2*instance address, and both rows'
  // position XYZ. Their corpus/runtime-cache DXIL uses precise fdiv too.
  // Delegate the existing algorithm through its fixed audited contract; do
  // not maintain another reciprocal envelope or widen its shader allowlist.
  if (!CaptureStaticWorldDependenciesInVertexRange(0x2C45BFFE76D77398ull,
          vs_float_words, normalized_vertex_first, normalized_vertex_end - 1, ranged))
    return false;

  StaticWorldShaderDependencies captured;
  std::copy(ranged.begin(), ranged.end(), captured.position_words.begin() + 20);
  for (uint32_t word = 0; word < 16; ++word) {
    const uint32_t clip = vs_float_words[word];
    if (!std::isfinite(std::bit_cast<float>(clip))) return false;
    captured.clip_rows[word] = clip;
  }
  captured.position_words[16] = uint32_t(source_hash);
  captured.position_words[17] = uint32_t(source_hash >> 32);
  captured.position_words[18] = 2;
  // TEXCOORD1 is lighting for both shaders; world is internal r1.yzx.
  captured.position_words[19] = UINT32_MAX;
  captured.clip_first_row = 0;
  captured.clip_matrix_kind = StaticWorldClipMatrixKind::kWorldToClip;
  result = captured;
  return true;
}

}  // namespace superman_returns::graphics
