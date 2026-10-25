#pragma once
#include "graphics_options.h"
#include <string>
#include <string_view>

namespace superman_returns::graphics {
// Small versioned companion settings file; contains intent only, never GPU status.
bool ParseGraphicsPreferences(std::string_view text,GraphicsValues& values);
std::string SerializeGraphicsPreferences(const GraphicsValues& values);
// UI thread: merge individual cvar changes with menu edits since the previous
// synchronization. A newer external edit wins only its field; unavailable On
// requests are rejected. Initialize is used once before renderer/device creation.
class GraphicsPreferencesBridge {
 public:
  bool Initialize(GraphicsOptions& options,const GraphicsValues& values);
  GraphicsValues Synchronize(GraphicsOptions& options,const GraphicsValues& external);
 private:
  GraphicsValues previous_{0,1,0,0,0};
};
}
