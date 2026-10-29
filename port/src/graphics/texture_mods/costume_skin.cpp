// Costume skins over game textures: --sr_costume_skin picks one.
#include "kingdom_come.h"
#include "texture_mods.h"
#include "texture_mods_runtime.h"

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(sr_costume_skin, "none", "Superman Returns",
                      "Costume skin painted over the game's textures: none, or kingdom_come (Kingdom Come "
                      "Superman over the Superman Returns suit)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace superman_returns::graphics::texture_mods {
namespace {
// Texture decodes before the film suit is expected (the title alone decodes
// ~1000 textures, and Superman flies in it wearing that suit).
constexpr uint32_t kTexturesBeforeWarning = 3000;
}  // namespace

void InstallCostumeSkin(TextureMods& mods) {
  const auto& skin = REXCVAR_GET(sr_costume_skin);
  if (skin == "kingdom_come") {
    InstallKingdomCome(mods, [](const std::string& message) { REXLOG_WARN("texture mods: {}", message); },
                       kTexturesBeforeWarning);
    REXLOG_INFO("texture mods: Kingdom Come skin on");
  } else if (skin != "none" && !skin.empty()) {
    REXLOG_WARN("texture mods: unknown costume skin '{}' (none, kingdom_come)", skin);
  }
}
}  // namespace superman_returns::graphics::texture_mods
