#include "graphics_options_dialog.h"
#include "graphics_options_runtime.h"
#include "graphics_settings.h"
#include "../title_benchmark.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <string>
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
std::string UpperValue(GraphicsOption option,int value) {
  std::string label(GraphicsValueLabel(option,value));
  for(auto& c:label) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return label;
}
}
void SetGraphicsOptionsFont(ImFont* font) { menu_font=font; }
ImFont* GraphicsOptionsFont() { return menu_font; }

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
  // Benchmark aid: the title scene has no guest pause, so show the panel with
  // this row selected. Screenshot only; nothing else opens the panel unpaused.
  if(const char* row=std::getenv("SR_BENCH_GRAPHICS_PANEL"); row && *row && benchmark::Active()) {
    if(!menu_->Snapshot().open) menu_->Open({1,true,true,true});
    menu_->Select(static_cast<size_t>(std::atoi(row)));
  }
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
  // Proportions measured from the reference: panel 979x742 over a 1577x890 view.
  const float height = std::min(io.DisplaySize.y*.83f,io.DisplaySize.x*.92f*742/979);
  const float width = height*979/742;
  const float scale = height/742;
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*.5f,io.DisplaySize.y*.5f),
                          ImGuiCond_Always,ImVec2(.5f,.5f));
  ImGui::SetNextWindowSize(ImVec2(width,height),ImGuiCond_Always);
  ImGui::SetNextWindowFocus();
  constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
  if (ImGui::Begin("GRAPHICS OPTIONS##sr_graphics_options",nullptr,flags)) {
    auto* draw=ImGui::GetWindowDrawList();
    ImFont* font=ImGui::GetFont();
    const ImVec2 origin=ImGui::GetWindowPos();
    const auto at=[&](float x,float y) { return ImVec2(origin.x+x*width,origin.y+y*height); };
    const auto text=[&](float size,ImVec2 pos,ImU32 color,const char* value) {
      draw->AddText(font,size*scale,ImVec2(pos.x+1.5f*scale,pos.y+1.5f*scale),IM_COL32(0,8,24,220),value);
      draw->AddText(font,size*scale,pos,color,value);
    };
    const auto centered=[&](float size,float y,ImU32 color,const char* value) {
      const auto extent=font->CalcTextSizeA(size*scale,FLT_MAX,0,value);
      text(size,ImVec2(origin.x+(width-extent.x)*.5f,at(0,y).y-extent.y*.5f),color,value);
    };
    const auto clicked=[&](ImVec2 min,ImVec2 max) {
      return !IsolatedPadTest() && ImGui::IsMouseHoveringRect(min,max) &&
             ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    };

    // Body and the four wings of the frame.
    const ImU32 edge=IM_COL32(70,135,205,255), wing=IM_COL32(16,70,140,255), wing_light=IM_COL32(26,86,158,255);
    draw->AddRectFilledMultiColor(at(0,.077f),at(1,.926f),IM_COL32(9,44,96,246),IM_COL32(9,44,96,246),
                                  IM_COL32(6,30,72,246),IM_COL32(6,30,72,246));
    for(int side=0;side<2;++side) for(int end=0;end<2;++end) {
      const auto point=[&](float x,float y) { return at(side ? 1-x : x,end ? 1-y : y); };
      const ImVec2 outer[]{point(0,.077f),point(.098f,0),point(.5f,.05f),point(.5f,.077f)};
      const ImVec2 inner[]{point(.14f,.077f),point(.21f,.012f),point(.5f,.05f),point(.5f,.077f)};
      draw->AddConvexPolyFilled(outer,4,wing);
      draw->AddConvexPolyFilled(inner,4,wing_light);
      draw->AddPolyline(outer,4,edge,ImDrawFlags_None,1.5f*scale);
    }
    draw->AddLine(at(0,.077f),at(1,.077f),edge,1.5f*scale);
    draw->AddLine(at(0,.926f),at(1,.926f),edge,1.5f*scale);
    draw->AddLine(at(0,.077f),at(0,.926f),edge,1.5f*scale);
    draw->AddLine(at(1,.077f),at(1,.926f),edge,1.5f*scale);

    // Title band fades toward both sides.
    const ImU32 band=IM_COL32(26,84,150,235), band_clear=IM_COL32(26,84,150,0);
    draw->AddRectFilledMultiColor(at(.06f,.131f),at(.5f,.185f),band_clear,band,band,band_clear);
    draw->AddRectFilledMultiColor(at(.5f,.131f),at(.94f,.185f),band,band_clear,band_clear,band);
    centered(32,.158f,IM_COL32(236,242,250,255),"GRAPHICS OPTIONS");

    static constexpr const char* labels[]{"RT SHADOWS","SHADOW SOFTNESS","CHARACTER SHADOWS","RT REFLECTIONS","NVIDIA DLSS"};
    const ImU32 bright=IM_COL32(226,233,243,255), dim=IM_COL32(120,138,165,255);
    for (size_t row=0;row<kGraphicsOptionCount;++row) {
      const auto option = static_cast<GraphicsOption>(row);
      const auto& support = options.features[row];
      const bool available = support.implemented && support.capable;
      const bool softness = option==GraphicsOption::kShadowSoftness;
      const bool inactive = (softness || option==GraphicsOption::kCharacterShadows) && !options.applied[0];
      const bool selected = row==menu.selected;
      const float y=.36f+row*.0553f;
      const ImVec2 min=at(.02f,y-.0245f), max=at(.98f,y+.0245f);
      if(selected) {
        const ImU32 solid=IM_COL32(58,102,158,235), clear=IM_COL32(58,102,158,0), line=IM_COL32(150,185,225,220);
        const ImVec2 mid=at(.5f,y);
        draw->AddRectFilledMultiColor(min,ImVec2(mid.x,max.y),clear,solid,solid,clear);
        draw->AddRectFilledMultiColor(ImVec2(mid.x,min.y),max,solid,clear,clear,solid);
        draw->AddLine(min,ImVec2(max.x,min.y),line,scale);
        draw->AddLine(ImVec2(min.x,max.y),max,line,scale);
      }
      const float row_top=at(0,y).y-font->CalcTextSizeA(27*scale,FLT_MAX,0,"A").y*.5f;
      text(27,ImVec2(at(.103f,0).x,row_top),available ? bright : dim,labels[row]);
      // Only the renderer's report is printed as the value, never the request.
      const int shown=inactive ? options.requested[row] : options.applied[row];
      const auto value=UpperValue(option,shown);
      text(27,ImVec2(at(.545f,0).x,row_top),available && !inactive ? bright : dim,value.c_str());
      if(clicked(min,max) && !selected) menu_->Select(row);
      if(selected) {
        const int low=softness ? 1 : 0, high=softness ? 2 : 1;
        const bool can_left=options.requested[row]>low && !inactive;
        const bool can_right=available && options.requested[row]<high && !inactive;
        const float half=7*scale;
        const ImVec2 left=at(.476f,y), right=at(.766f,y);
        draw->AddTriangleFilled(ImVec2(left.x-half,left.y),ImVec2(left.x+half,left.y-half),
                                ImVec2(left.x+half,left.y+half),can_left ? bright : dim);
        draw->AddTriangleFilled(ImVec2(right.x+half,right.y),ImVec2(right.x-half,right.y+half),
                                ImVec2(right.x-half,right.y-half),can_right ? bright : dim);
        const ImVec2 pad(16*scale,16*scale);
        if(can_left && clicked(ImVec2(left.x-pad.x,left.y-pad.y),ImVec2(left.x+pad.x,left.y+pad.y))) menu_->Adjust(-1);
        if(can_right && clicked(ImVec2(right.x-pad.x,right.y-pad.y),ImVec2(right.x+pad.x,right.y+pad.y))) menu_->Adjust(1);
      }
    }

    // Why the selected row cannot change; the reference has no such row because
    // every feature there is available.
    const auto& selected_support=options.features[menu.selected];
    std::string status;
    if(!selected_support.implemented || !selected_support.capable) status="Unavailable: "+selected_support.reason;
    else if((menu.selected==size_t(GraphicsOption::kShadowSoftness) || menu.selected==size_t(GraphicsOption::kCharacterShadows)) && !options.applied[0])
      status="Turn on RT Shadows to change this option.";
    if(!options.failure.empty()) status+=(status.empty() ? "" : "\n")+options.failure;
    if(!status.empty())
      draw->AddText(font,18*scale,at(.103f,.665f),IM_COL32(190,205,225,255),status.c_str(),nullptr,width*.8f);

    const auto back=font->CalcTextSizeA(26*scale,FLT_MAX,0,"BACK");
    const ImVec2 back_center=at(.969f,.883f);
    text(26,ImVec2(back_center.x-24*scale-back.x,back_center.y-back.y*.5f),bright,"BACK");
    draw->AddCircleFilled(back_center,15*scale,IM_COL32(186,28,36,255));
    draw->AddCircle(back_center,15*scale,IM_COL32(245,205,205,255),0,1.5f*scale);
    const auto b_size=font->CalcTextSizeA(20*scale,FLT_MAX,0,"B");
    draw->AddText(font,20*scale,ImVec2(back_center.x-b_size.x*.5f,back_center.y-b_size.y*.5f),IM_COL32_WHITE,"B");
    if(clicked(ImVec2(back_center.x-24*scale-back.x,back_center.y-18*scale),
               ImVec2(back_center.x+18*scale,back_center.y+18*scale))) menu_->Close();
    centered(25,.975f,IM_COL32(206,214,226,255),"PAUSED");
  }
  ImGui::End();
}
} // namespace superman_returns::graphics
