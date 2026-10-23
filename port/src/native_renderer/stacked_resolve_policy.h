#pragma once
#include <cstdint>
#include <optional>
#include <span>

namespace superman_returns::native {
inline bool IsStacked2DFetch(std::span<const uint32_t,6> fetch) {
  return (fetch[0]&3u)==2u && ((fetch[5]>>9)&3u)==1u && (fetch[1]&(1u<<10));
}
struct StackedResolvePlan {
  uint32_t width,height,layers,destination_z;
};
inline std::optional<StackedResolvePlan> PlanStackedResolve(
    std::span<const uint32_t,6> fetch,uint32_t level,uint32_t slice) {
  // Initial experiment: one mip, compatible with the observed Texture3D shader.
  if(!IsStacked2DFetch(fetch) || level || ((fetch[4]>>2)&255u)) return std::nullopt;
  StackedResolvePlan plan{(fetch[2]&8191u)+1,((fetch[2]>>13)&8191u)+1,
                          (fetch[2]>>26)+1,slice};
  // D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION. Reject rather than truncate.
  if(plan.width>2048 || plan.height>2048 || slice>=plan.layers) return std::nullopt;
  return plan;
}
inline uint32_t ResolvedFetchDimension(std::span<const uint32_t,6> fetch,bool resolved_volume) {
  return resolved_volume && IsStacked2DFetch(fetch)?2u:(fetch[5]>>9)&3u;
}
}
