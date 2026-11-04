#pragma once
#include <cstdint>
#include "coop_status_packet.h"
struct PPCContext;
namespace superman_returns::coop {
// Simulation thread only, after updating the owned camera. Publishes values,
// never guest pointers; preserves the complete caller PPC context.
void UpdateCoopStatus(PPCContext&,uint8_t*);
std::optional<status::Stamina> StaminaForHud(uint32_t generation,uint32_t view);
}
