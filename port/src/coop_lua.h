#pragma once

namespace rex { class Runtime; }

namespace superman_returns::coop {
// Opt-in built-in P2 join and development script mailbox. Execute on the game's Lua thread,
// retaining both the guest register state and the Lua stack of the caller.
void InstallLuaBridge(rex::Runtime& runtime);
}
