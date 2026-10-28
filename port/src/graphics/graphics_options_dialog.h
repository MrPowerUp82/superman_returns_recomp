#pragma once

#include "graphics_options.h"
#include <rex/ui/imgui_dialog.h>
struct ImFont;

namespace superman_returns::graphics {
void SetGraphicsOptionsFont(ImFont* font);
// Arial with Latin-1 accents when the app loaded it; nullptr otherwise.
ImFont* GraphicsOptionsFont();

// Host panel installed by SupermanReturnsApp. graphics_options_hooks.cpp
// authenticates the existing guest pause and filters its polled gamepad path.
// Install creates one persistent UI-owned dialog; the app deletes it before
// drawer teardown. No guest APT row or guessed callback is inserted.
// Runtime evidence and exact guest addresses:
// docs/graphics-pause-integration-2026-10-24.md.
class GraphicsOptionsDialog final : public rex::ui::ImGuiDialog {
 public:
  // Returns nullptr unless an authenticated pause session is already open.
  // Non-owning result: ReXGlue deletes it after closure. UI thread only.
  static GraphicsOptionsDialog* Show(rex::ui::ImGuiDrawer* drawer,
                                     std::shared_ptr<GraphicsOptionsMenu> menu);
  static GraphicsOptionsDialog* Install(rex::ui::ImGuiDrawer* drawer);
 protected:
  void OnDraw(ImGuiIO& io) override;
 private:
  GraphicsOptionsDialog(rex::ui::ImGuiDrawer* drawer,
                        std::shared_ptr<GraphicsOptionsMenu> menu);
  std::shared_ptr<GraphicsOptionsMenu> menu_;
  bool persistent_=false;
  bool was_open_=false;
};

} // namespace superman_returns::graphics
