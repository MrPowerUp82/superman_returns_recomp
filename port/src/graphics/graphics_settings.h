#pragma once
#include <filesystem>
namespace superman_returns::graphics {
// Called after SDK config/CLI parsing and before creating the graphics device.
void InitializeGraphicsSettings(const std::filesystem::path& path);
// UI thread; writes only changed preference fields, never applied GPU status.
void SynchronizeGraphicsSettings(bool flush=false);
}
