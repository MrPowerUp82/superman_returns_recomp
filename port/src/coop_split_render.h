#pragma once
#include <cstdint>
#include <optional>
struct PPCContext;
namespace superman_returns::coop {
struct SplitHudContext {uint64_t frame;uint32_t generation,view;};
std::optional<SplitHudContext> CurrentSplitHud();
bool PrepareSplitCamera(PPCContext&,uint8_t*,uint32_t graphics,uint32_t view,uint32_t caller);
bool BeginSplitWorld(uint8_t*,uint32_t graphics);
void EndSplitWorld(PPCContext&,uint8_t*,uint32_t graphics);
}
