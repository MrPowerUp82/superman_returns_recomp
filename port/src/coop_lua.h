#pragma once
#include <cstdint>

namespace rex { class Runtime; }

namespace superman_returns::coop {
// Opt-in F2-driven P2 lifecycle and development script mailbox. Execute on the game's Lua thread,
// retaining both the guest register state and the Lua stack of the caller.
void InstallLuaBridge(rex::Runtime& runtime);
// UI requests only. Guest actor/camera writes remain on the validated Lua thread.
bool CanToggleCoop();
void RequestCoopToggle();
bool RuntimeCoopActive();
// Atomic snapshot: low bit is active, upper bits identify a completed lifecycle.
uint64_t RuntimeCoopState();
}
