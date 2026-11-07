#pragma once
#include "graphics_options.h"
#include <string>
#include <string_view>

namespace superman_returns::graphics {
// Legacy companion codec retained for migration; contains intent only.
bool ParseGraphicsPreferences(std::string_view text,GraphicsValues& values);
std::string SerializeGraphicsPreferences(const GraphicsValues& values);
// UI thread: merge individual cvar changes with menu edits since the previous
// synchronization. A newer external edit wins only its field; unavailable On
// requests are rejected. Initialize is used once before renderer/device creation.
std::string_view LauncherGraphicsKey(GraphicsOption option);
// Partial canonical INI read. Present marks valid keys only; invalid fields do not reset others.
std::array<bool,kGraphicsOptionCount> ReadLauncherGraphicsPreferences(std::string_view text,GraphicsValues& values);
std::string MergeLauncherGraphicsPreferences(std::string_view text,const GraphicsValues& previous,
                                             const GraphicsValues& requested,bool fill_missing=false);
class GraphicsPreferencesBridge {
 public:
  bool Initialize(GraphicsOptions& options,const GraphicsValues& values);
  GraphicsValues Synchronize(GraphicsOptions& options,const GraphicsValues& external);
 private:
  GraphicsValues previous_{0,1,0,0,0,0,1,0,0,0,2};
};
}
