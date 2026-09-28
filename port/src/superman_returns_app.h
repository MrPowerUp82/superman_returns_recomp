// superman_returns - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <cstdlib>
#include <fstream>

#include <rex/rex_app.h>
#include <rex/runtime.h>

#include "frame_stats.h"

class SupermanReturnsApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<SupermanReturnsApp>(new SupermanReturnsApp(ctx, "superman_returns",
        PPCImageConfig));
  }

  // Load the Xenos GPU emulation plugin staged next to the executable.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (!config.graphics && config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
  }

  // Debug aid: set SR_DUMP_IMAGE=<path> to write the loaded guest image
  // (0x82000000-0x829E0000) for offline analysis of missed functions.
  void OnPostLoadXexImage() override {
    const char* path = std::getenv("SR_DUMP_IMAGE");
    if (!path || !*path) return;
    constexpr uint32_t kImageBase = 0x82000000;
    constexpr uint32_t kImageEnd = 0x829E0000;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(runtime()->virtual_membase() + kImageBase),
              kImageEnd - kImageBase);
  }

  // Feed the F3 debug overlay with the guest frame rate.
  void OnPostSetup() override { SetGuestFrameStats(SampleGuestFrameStats); }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
