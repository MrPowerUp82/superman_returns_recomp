#pragma once

namespace rex { class Runtime; }

namespace superman_returns::coop {
// Development-only, opt-in script mailbox. Executes on the game's Lua thread,
// retaining both the guest register state and the Lua stack of the caller.
void InstallLuaBridge(rex::Runtime& runtime);
}
