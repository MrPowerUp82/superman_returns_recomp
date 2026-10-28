#include "title_benchmark_dialog.h"

#include <imgui.h>
#include <rex/logging.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "title_benchmark.h"
#include "graphics/graphics_options_dialog.h"

namespace superman_returns::benchmark {
namespace {
// Esc/Enter never reach the ImGui drawer here (a listener above it consumes the
// keys), so read them from Windows while the game window is in front.
bool CloseKeyPressed() {
#if defined(_WIN32)
  static bool was_down = true;  // a key already held when the screen opens does not count
  DWORD process = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &process);
  const bool down = process == GetCurrentProcessId() &&
                    ((GetAsyncKeyState(VK_ESCAPE) | GetAsyncKeyState(VK_RETURN)) & 0x8000);
  const bool pressed = down && !was_down;
  was_down = down;
  return pressed;
#else
  return ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter);
#endif
}
}  // namespace

ResultsDialog::ResultsDialog(rex::ui::ImGuiDrawer* drawer, std::function<void()> quit)
    : ImGuiDialog(drawer), quit_(std::move(quit)) {}

ResultsDialog* ResultsDialog::Install(rex::ui::ImGuiDrawer* drawer, std::function<void()> quit) {
  if (!drawer) return nullptr;
  return new ResultsDialog(drawer, std::move(quit));
}

void ResultsDialog::OnDraw(ImGuiIO& io) {
  const Result r = Snapshot();
  if (r.phase != Phase::Done || quitting_) return;
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowBgAlpha(0.85f);
  // Portuguese text needs accented glyphs.
  ImFont* font = graphics::GraphicsOptionsFont();
  if (font) ImGui::PushFont(font);
  ImGui::Begin("Benchmark - Superman Returns", nullptr,
               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
  if (r.valid) ImGui::TextUnformatted("Resultado válido");
  else ImGui::Text("Resultado inválido: %s", r.reason.c_str());
  ImGui::Separator();
  const auto& s = r.stats;
  ImGui::Text("FPS médio: %.1f", s.avg_fps);
  ImGui::Text("FPS mínimo: %.1f", s.min_fps);
  ImGui::Text("1%% low: %.1f   0,1%% low: %.1f", s.low1_fps, s.low01_fps);
  ImGui::Text("Tempo de quadro (ms): p50 %.1f   p95 %.1f   p99 %.1f   máximo %.1f", s.p50_ms, s.p95_ms,
              s.p99_ms, s.max_ms);
  ImGui::Text("Quadros: %u   Duração: %.1f s", s.frames, s.seconds);
  ImGui::Separator();
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
  ImGui::TextUnformatted(r.settings_text.c_str());
  if (r.saved) ImGui::Text("Salvo em: %s", r.json_path.c_str());
  else ImGui::TextUnformatted("Não foi possível salvar o arquivo");
  ImGui::PopTextWrapPos();
  const bool close = ImGui::Button("Fechar") || CloseKeyPressed();
  ImGui::End();
  if (font) ImGui::PopFont();
  if (close && quit_) {
    REXLOG_INFO("benchmark: results closed, quitting");
    quitting_ = true;
    quit_();
  }
}

}  // namespace superman_returns::benchmark
