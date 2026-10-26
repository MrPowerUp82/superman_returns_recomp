#pragma once
#include <cstdint>
namespace superman_returns::graphics {
struct MotionDepthBoundaryState {
  bool active=false,finished=false,has_geometry=false;
  uintptr_t captured_depth=0;
};
struct MotionCapturePair {uintptr_t color=0,depth=0;};
inline bool MatchesRetainedMotionCapturePair(MotionCapturePair retained,MotionCapturePair current) {
  if(!current.color || !current.depth) return false;
  if(!retained.color && !retained.depth) return true;
  return retained.color && retained.depth && retained.color==current.color && retained.depth==current.depth;
}
// Freeze before the first depth mutation, even if a resolve already cleared
// OM binding caches. Binding bookkeeping is not the depth resource's lifetime.
inline bool ShouldFreezeMotionDepthBeforeWrite(MotionDepthBoundaryState state,
    uintptr_t written_depth,bool writes_depth) {
  return state.active && !state.finished && state.has_geometry &&
      state.captured_depth && written_depth==state.captured_depth && writes_depth;
}
}
