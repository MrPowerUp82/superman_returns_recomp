#pragma once

// Input preparation for local co-op in one game instance. This does not yet
// create a second player or camera, or render split-screen views.
namespace rex { class Runtime; }

namespace superman_returns::coop {
// Call from OnPostSetup before the guest starts polling. Opt-in via
// --sr_coop_input=true; normal and title-benchmark input are left unchanged.
void Install(rex::Runtime& runtime);
}
