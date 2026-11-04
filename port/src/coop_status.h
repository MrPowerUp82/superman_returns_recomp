#pragma once
#include <cstdint>
struct PPCContext;
namespace superman_returns::coop {
// Simulation thread only, after updating the owned camera. Publishes values,
// never guest pointers; preserves the complete caller PPC context.
void UpdateCoopStatus(PPCContext&,uint8_t*);
}
