#pragma once
// Results screen of the title benchmark (sr_benchmark). Persistent ImGui dialog:
// draws nothing until the benchmark is done, then shows the numbers over the
// scene. Closing it quits the game.
#include <functional>
#include <rex/ui/imgui_dialog.h>

namespace superman_returns::benchmark {
class ResultsDialog final : public rex::ui::ImGuiDialog {
 public:
  // The app deletes it before the drawer is torn down. UI thread only.
  static ResultsDialog* Install(rex::ui::ImGuiDrawer* drawer, std::function<void()> quit);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  ResultsDialog(rex::ui::ImGuiDrawer* drawer, std::function<void()> quit);
  std::function<void()> quit_;
  bool quitting_ = false;
};
}  // namespace superman_returns::benchmark
