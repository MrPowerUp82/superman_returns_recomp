#pragma once
#include <cstdint>
#include <rex/ppc/func.h>
#include "coop_camera_packet.h"
namespace rex { class Runtime; }
namespace superman_returns::coop {
void InstallSecondCamera(rex::Runtime& runtime);
// Own controller graph, on the simulation thread. Does not render or replace
// the game's camera singleton; preserves the caller's complete PPC context.
void UpdateSecondCamera(PPCContext& ctx, uint8_t* base, uint32_t primary, double dt);
void DestroySecondCamera(PPCContext& ctx, uint8_t* base);
struct SecondCameraSnapshot {
  std::optional<packet::ViewBytes> view;
  uint32_t target=0, primary_owner=0;
  uint64_t sequence=0;
  uint64_t session_generation=0;
};
// Immutable copy under a short host lock; no guest pointers escape to readers.
SecondCameraSnapshot SampleSecondCamera();
}
