#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace superman_returns::graphics {

// Three HLSL constant registers, bound at b3, space4 by the D3D12 draw path.
// The specialization also requires t0, space6 (TLAS) and u0, space6 (a zeroed
// execution counter); its caller gates DXR 1.1, SM 6.5 and WaveOps support.
struct RtShadowDrawConstants {
  float light[3];
  uint32_t enabled;
  float bias, max_distance, cone;
  uint32_t samples;
  uint32_t mask;
  uint32_t padding[3];
};
static_assert(sizeof(RtShadowDrawConstants) == 48);

// Only PS 8A6D is audited, with canonical world position from paired VS 2C45.
bool IsRtShadowShader(uint64_t ps_hash);

// On rejection output is cleared and error explains the failed audit.
bool SpecializeRtShadowShader(uint64_t hash, std::string_view source,
                              std::string& output, std::string& error);

// Version + actual patch text: the translator fingerprints this separately
// from its existing tool fingerprint. Bump the version for audit-rule changes.
std::string_view RtShadowShaderCacheIdentity();

}  // namespace superman_returns::graphics
