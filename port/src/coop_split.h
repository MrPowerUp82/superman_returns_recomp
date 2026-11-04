#pragma once

// Install the opt-in input, join, camera and HUD parts for local co-op in one
// game instance. The launcher enables their individual options together.
namespace rex { class Runtime; }

namespace superman_returns::coop {
// Call from OnPostSetup before the guest starts polling. Opt-in via
// --sr_coop_input=true; normal and title-benchmark input are left unchanged.
void Install(rex::Runtime& runtime);
}
