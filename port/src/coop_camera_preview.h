#pragma once
#include <cstdint>
#include <algorithm>
#include <span>
struct PPCContext;

namespace superman_returns::coop {
struct CameraSelection {
  uint32_t world=0,hud=0;
  bool operator==(const CameraSelection&) const=default;
};
inline bool PreviewMatrixMatches(CameraSelection expected,CameraSelection current,
    std::span<const uint8_t> source,std::span<const uint8_t> actual) {
  return expected.world && expected.hud && expected.world!=expected.hud && expected==current &&
      source.size()==64 && actual.size()==64 && std::equal(source.begin(),source.end(),actual.begin());
}
// Called after the original view-0 application, before native world preparation.
void ApplyP2CameraPreview(PPCContext& ctx,uint8_t* base,uint32_t graphics,
                          uint32_t view,uint32_t caller);
void ObserveP2CameraDraw(uint8_t* base,uint32_t graphics);
}
