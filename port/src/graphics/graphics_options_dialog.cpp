#include "graphics_options_dialog.h"
#include "graphics_options_runtime.h"
#include "graphics_settings.h"
#include <imgui.h>
#include <algorithm>
#include <utility>
#include <cstdlib>
#include <rex/logging.h>

namespace superman_returns::graphics {
namespace {
ImFont* menu_font=nullptr;
bool IsolatedPadTest() {
  static const bool enabled=[] {
    const char* value=std::getenv("SR_BENCH_GAMEPAD_ONLY");
    const char* source=std::getenv("SR_BENCH_INPUT");
    return source && *source && value && std::string_view(value)=="1";
  }();
  return enabled;
}
struct FontScope {
  FontScope() { if(menu_font) ImGui::PushFont(menu_font); }
  ~FontScope() { if(menu_font) ImGui::PopFont(); }
};
MenuInputSample KeyboardSample() {
  MenuInputSample sample;
  if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) sample.down |= kMenuUp;
  if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) sample.down |= kMenuDown;
  if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) sample.down |= kMenuLeft;
  if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) sample.down |= kMenuRight;
  if (ImGui::IsKeyDown(ImGuiKey_Escape)) sample.down |= kMenuBack;
  for (int key=ImGuiKey_NamedKey_BEGIN; key<ImGuiKey_GamepadStart; ++key)
    sample.any_guest_input_down |= ImGui::IsKeyDown(static_cast<ImGuiKey>(key));
  for (int button=0; button<5; ++button)
    sample.any_guest_input_down |= ImGui::IsMouseDown(button);
  return sample;
}
}
void SetGraphicsOptionsFont(ImFont* font) { menu_font=font; }

GraphicsOptionsDialog::GraphicsOptionsDialog(rex::ui::ImGuiDrawer* drawer,
                                             std::shared_ptr<GraphicsOptionsMenu> menu)
    : ImGuiDialog(drawer), menu_(std::move(menu)) {}

GraphicsOptionsDialog* GraphicsOptionsDialog::Show(rex::ui::ImGuiDrawer* drawer,
                                                   std::shared_ptr<GraphicsOptionsMenu> menu) {
  if (!drawer || !menu || !menu->Snapshot().open) return nullptr;
  return new GraphicsOptionsDialog(drawer,std::move(menu));
}

GraphicsOptionsDialog* GraphicsOptionsDialog::Install(rex::ui::ImGuiDrawer* drawer) {
  if(!drawer) return nullptr;
  auto* dialog=new GraphicsOptionsDialog(drawer,SharedGraphicsOptionsMenu());
  dialog->persistent_=true;
  return dialog;
}

void GraphicsOptionsDialog::OnDraw(ImGuiIO& io) {
  SynchronizeGraphicsSettings();
  FontScope font_scope;
  menu_->SetInput(MenuInputSource::kKeyboard,IsolatedPadTest() ? MenuInputSample{} : KeyboardSample());
  menu_->TickInput();
  if(persistent_ && !menu_->Snapshot().open && CurrentGraphicsGuestPause().guest_paused &&
     !menu_->CaptureGuestInput()) {
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*.5f,io.DisplaySize.y*.88f),
                            ImGuiCond_Always,ImVec2(.5f,.5f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(.025f,.09f,.20f,.92f));
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.05f,.24f,.43f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.12f,.40f,.67f,1));
    ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.18f,.48f,.75f,1));
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.94f,.97f,1,1));
    const auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|
                     ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNav;
    if(ImGui::Begin("##sr_pause_graphics_entry",nullptr,flags)) {
      const bool activated=ImGui::Button("GRAPHICS OPTIONS  [G / Y]");
      const bool click=!IsolatedPadTest() && ImGui::IsItemClicked(ImGuiMouseButton_Left);
      const bool key=!IsolatedPadTest() && ImGui::IsKeyPressed(ImGuiKey_G,false);
      if(activated || click || key) {
        REXLOG_INFO("native graphics entry: source={}",key ? "G" : click ? "mouse" : "navigation_ignored");
        // Enter/Space belong to the original pause menu. ImGui may retain the
        // entry's navigation focus after this panel closes, even with NoNav.
        if(click || key) OpenGraphicsOptionsFromPause();
      }
    }
    ImGui::End(); ImGui::PopStyleColor(5);
  }
  const auto menu = menu_->Snapshot();
  if(menu.open!=was_open_) {
    REXLOG_INFO("native graphics panel: open={} pause_session={} input_captured={}",
                menu.open,menu.pause_session,menu.capture_guest_input);
    was_open_=menu.open;
  }
  if (!menu.open) {
    // Keep sampling releases without drawing the panel. Otherwise closing on
    // Esc would leave its last down sample latched after this object dies.
    if (!persistent_ && !menu.capture_guest_input) Close();
    return;
  }
  const auto options = menu_->options()->Snapshot();
  const float width = std::min(720.f,io.DisplaySize.x*0.92f);
  const float height = std::min(540.f,io.DisplaySize.y*0.92f);
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*.5f,io.DisplaySize.y*.5f),
                          ImGuiCond_Always,ImVec2(.5f,.5f));
  ImGui::SetNextWindowSize(ImVec2(width,height),ImGuiCond_Always);
  ImGui::SetNextWindowFocus();
  ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(.025f,.09f,.20f,.92f));
  ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(.1f,.4f,.7f,.75f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered,ImVec4(.15f,.5f,.8f,.9f));
  ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.94f,.97f,1,1));
  ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.18f,.48f,.75f,1));
  ImGui::PushStyleColor(ImGuiCol_Separator,ImVec4(.18f,.48f,.75f,.85f));
  ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.05f,.24f,.43f,1));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.12f,.40f,.67f,1));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(.15f,.50f,.80f,1));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(22,18));
  constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoNav;
  if (ImGui::Begin("GRAPHICS OPTIONS##sr_graphics_options",nullptr,flags)) {
    ImGui::SetCursorPosX((width-ImGui::CalcTextSize("GRAPHICS OPTIONS").x)*.5f);
    ImGui::TextUnformatted("GRAPHICS OPTIONS");
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::BeginTable("options",4,ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Option",ImGuiTableColumnFlags_WidthStretch,3);
      ImGui::TableSetupColumn("Effective",ImGuiTableColumnFlags_WidthStretch,1);
      ImGui::TableSetupColumn("Decrease",ImGuiTableColumnFlags_WidthFixed,28);
      ImGui::TableSetupColumn("Increase",ImGuiTableColumnFlags_WidthFixed,28);
      for (size_t row=0;row<kGraphicsOptionCount;++row) {
        const auto option = static_cast<GraphicsOption>(row);
        const auto& support = options.features[row];
        const bool available = support.implemented && support.capable;
        ImGui::PushID(static_cast<int>(row));
        static constexpr const char* labels[]{"RT SHADOWS","SHADOW SOFTNESS","CHARACTER SHADOWS","RT REFLECTIONS","NVIDIA DLSS"};
        ImGui::TableNextRow(0,48); ImGui::TableSetColumnIndex(0);
        if (ImGui::Selectable(labels[row],row == menu.selected) && !IsolatedPadTest()) menu_->Select(row);
        ImGui::TableSetColumnIndex(1);
        // This is the only primary value: never print requested as effective.
        if(option==GraphicsOption::kShadowSoftness && !options.applied[0])
          ImGui::Text("%s (inactive)",GraphicsValueLabel(option,options.requested[row]).data());
        else ImGui::TextUnformatted(GraphicsValueLabel(option,options.applied[row]).data());
        ImGui::TableSetColumnIndex(2);
        ImGui::BeginDisabled(options.requested[row] <= (option==GraphicsOption::kShadowSoftness ? 1 : 0));
        if (ImGui::SmallButton("<") && !IsolatedPadTest()) { menu_->Select(row); menu_->Adjust(-1); }
        ImGui::EndDisabled();
        ImGui::TableSetColumnIndex(3);
        const int max_value = option == GraphicsOption::kShadowSoftness ? 2 : 1;
        ImGui::BeginDisabled(!available || options.requested[row] >= max_value);
        if (ImGui::SmallButton(">") && !IsolatedPadTest()) { menu_->Select(row); menu_->Adjust(1); }
        ImGui::EndDisabled();
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    const auto& selected_support=options.features[menu.selected];
    if(!selected_support.implemented || !selected_support.capable)
      ImGui::TextWrapped("Unavailable: %s",selected_support.reason.c_str());
    if (!options.failure.empty()) ImGui::TextWrapped("%s",options.failure.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("Arrows / D-pad / left stick: select and change. B / Esc: back to pause.");
    if (ImGui::Button("Back to pause") && !IsolatedPadTest()) menu_->Close();
  }
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(9);
}
} // namespace superman_returns::graphics
