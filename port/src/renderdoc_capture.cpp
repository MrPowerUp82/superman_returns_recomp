// RenderDoc capture of whole guest frames, for GPU profiling.
//
// RenderDoc's F12 hotkey captures one host present, but the window presents
// at display rate while the GPU plugin submits guest work from its own thread,
// so a hotkey capture usually holds no guest rendering. Instead, when the game
// runs under RenderDoc and SR_RDC_TRIGGER names a file, creating that file
// captures everything between two guest presents kCapturedFrames apart.
// tools/capture_frame.ps1 drives this.

#include "renderdoc_capture.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>

#include <rex/logging.h>

#include "../third_party/renderdoc/renderdoc_app.h"

namespace {

constexpr uint64_t kCapturedFrames = 3;
constexpr uint64_t kTriggerPollInterval = 15;

RENDERDOC_API_1_1_2* Api() {
  static RENDERDOC_API_1_1_2* api = [] () -> RENDERDOC_API_1_1_2* {
    HMODULE module = GetModuleHandleA("renderdoc.dll");
    if (!module) return nullptr;
    auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
    RENDERDOC_API_1_1_2* result = nullptr;
    if (!get_api || !get_api(eRENDERDOC_API_Version_1_1_2, reinterpret_cast<void**>(&result))) {
      return nullptr;
    }
    return result;
  }();
  return api;
}

}  // namespace

void OnGuestFrameForCapture(uint64_t frame) {
  static const char* trigger = std::getenv("SR_RDC_TRIGGER");
  static uint64_t end_frame = 0;
  if (!trigger || !*trigger) return;

  if (end_frame) {
    if (frame >= end_frame) {
      Api()->EndFrameCapture(nullptr, nullptr);
      REXLOG_INFO("renderdoc: captured guest frames up to {}", frame);
      end_frame = 0;
    }
    return;
  }
  if (frame % kTriggerPollInterval != 0 || !Api()) return;
  if (std::remove(trigger) != 0) return;  // Trigger file absent.
  Api()->StartFrameCapture(nullptr, nullptr);
  end_frame = frame + kCapturedFrames;
  REXLOG_INFO("renderdoc: capture started at guest frame {}", frame);
}
