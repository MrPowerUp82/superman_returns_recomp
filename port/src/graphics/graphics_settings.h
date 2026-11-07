#pragma once
#include <filesystem>
namespace superman_returns::graphics {
// Called after SDK config/CLI parsing and before creating the graphics device.
// path is the old companion file for read-only migration. New writes use
// SR_LAUNCHER_SETTINGS or the standard LocalAppData launcher installation INI.
void InitializeGraphicsSettings(const std::filesystem::path& path);
// Serialized across guest callbacks and shutdown; persists requested preferences.
void SynchronizeGraphicsSettings(bool flush=false);
}
