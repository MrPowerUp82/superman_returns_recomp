#pragma once
#include "../guest/texture_layout.h"

namespace superman_returns::graphics::texture_mods {
class TextureMods;
// Configured from sr_texture_* on first use (both backends decode on worker threads).
TextureMods& GlobalTextureMods();
// Called right after each guest texture is decoded, before upload.
void ApplyTextureMods(guest::LinearTexture& texture);
// Registers the selected costume skin's recipe (costume_skin.cpp); no-op without one.
void InstallCostumeSkin(TextureMods& mods);
}  // namespace superman_returns::graphics::texture_mods
