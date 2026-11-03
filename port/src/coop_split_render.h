#pragma once
#include <cstdint>
struct PPCContext;
namespace superman_returns::coop {
bool PrepareSplitCamera(PPCContext&,uint8_t*,uint32_t graphics,uint32_t view,uint32_t caller);
bool BeginSplitWorld(uint8_t*,uint32_t graphics);
void EndSplitWorld(PPCContext&,uint8_t*,uint32_t graphics);
}
