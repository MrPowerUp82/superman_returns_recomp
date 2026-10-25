#pragma once
#include "graphics_options.h"
namespace superman_returns::graphics {
std::shared_ptr<GraphicsOptions> SharedGraphicsOptions();
std::shared_ptr<GraphicsOptionsMenu> SharedGraphicsOptionsMenu();
void ObserveGraphicsGuestPause(GraphicsGuestPauseSample sample);
GuestPauseContract CurrentGraphicsGuestPause();
bool OpenGraphicsOptionsFromPause();
void ObserveGraphicsGamepad(const GraphicsGamepadState& pad);
void ClearGraphicsGuestPause();
}
