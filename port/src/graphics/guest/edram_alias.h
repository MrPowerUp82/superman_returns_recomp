#pragma once
#include "render_state.h"
#include <span>
namespace superman_returns::graphics::guest {
struct AliasSurface {uint64_t id;bool depth;SurfaceGeometry geometry;uint64_t last_write;float scale=1;};
enum class AliasAction {kNone,kClear,kReinterpret};
struct AliasPlan {AliasAction action=AliasAction::kNone;uint64_t source=0,newest_same=0,newest_other=0;};
bool EdramOverlaps(uint32_t base,uint32_t tiles,uint32_t other_base,uint32_t other_tiles);
AliasPlan PlanEdramAlias(const AliasSurface&,std::span<const AliasSurface>,std::span<const uint64_t> bind_set);
}
