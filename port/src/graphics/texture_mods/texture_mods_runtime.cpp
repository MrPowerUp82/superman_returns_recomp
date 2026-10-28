// Game side of texture mods: options and the instance both backends use.
#include "texture_mods.h"
#include "texture_mods_runtime.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_STRING(sr_texture_dump_dir, "", "Superman Returns",
                      "Write each distinct texture once as <key>.png plus index.csv into this folder")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(sr_texture_replace_dir, "textures", "Superman Returns",
                      "Folder of replacement textures named <key>.png (relative to the executable)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(sr_texture_replace, true, "Superman Returns",
                    "Use replacement textures and costume skins")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace superman_returns::graphics::texture_mods {
namespace {
std::filesystem::path ExecutableDir() {
#if defined(_WIN32)
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length && length < MAX_PATH) return std::filesystem::path(path).parent_path();
#endif
  return std::filesystem::current_path();
}
std::filesystem::path Resolve(const std::string& value) {
  if (value.empty()) return {};
  std::filesystem::path path = std::filesystem::u8path(value);
  return path.is_absolute() ? path : ExecutableDir() / path;
}
}  // namespace

TextureMods& GlobalTextureMods() {
  static TextureMods* mods = [] {
    const bool replace = REXCVAR_GET(sr_texture_replace);
    auto* created = new TextureMods(ModConfig{Resolve(REXCVAR_GET(sr_texture_dump_dir)),
                                              replace ? Resolve(REXCVAR_GET(sr_texture_replace_dir)) : std::filesystem::path{},
                                              replace});
    created->SetWarn([](const std::string& message) { REXLOG_WARN("texture mods: {}", message); });
    created->SetNote([](const std::string& message) { REXLOG_INFO("texture mods: {}", message); });
    if (replace) InstallCostumeSkin(*created);
    return created;
  }();
  return *mods;
}

void ApplyTextureMods(guest::LinearTexture& texture) { GlobalTextureMods().Apply(texture); }

}  // namespace superman_returns::graphics::texture_mods
