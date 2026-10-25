#pragma once
#include <cstdint>
namespace superman_returns::graphics {
struct MotionDepthBoundaryState {
  bool active=false,finished=false,has_geometry=false;
  uintptr_t captured_depth=0;
};
// Freeze before the first depth mutation, even if a resolve already cleared
// OM binding caches. Binding bookkeeping is not the depth resource's lifetime.
inline bool ShouldFreezeMotionDepthBeforeWrite(MotionDepthBoundaryState state,
    uintptr_t written_depth,bool writes_depth) {
  return state.active && !state.finished && state.has_geometry &&
      state.captured_depth && written_depth==state.captured_depth && writes_depth;
}
}
