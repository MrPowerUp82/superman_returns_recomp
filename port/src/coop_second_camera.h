#pragma once
#include <cstdint>
#include <rex/ppc/func.h>
namespace rex { class Runtime; }
namespace superman_returns::coop {
void InstallSecondCamera(rex::Runtime& runtime);
// Own controller graph, on the simulation thread. Does not render or replace
// the game's camera singleton; preserves the caller's complete PPC context.
void UpdateSecondCamera(PPCContext& ctx, uint8_t* base, uint32_t primary, double dt);
void DestroySecondCamera(PPCContext& ctx, uint8_t* base);
}
