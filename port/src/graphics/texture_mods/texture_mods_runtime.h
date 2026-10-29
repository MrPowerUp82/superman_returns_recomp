#pragma once
#include "../guest/texture_layout.h"

namespace superman_returns::graphics::texture_mods {
class TextureMods;
// Configured from sr_texture_* on first use (both backends decode on worker threads).
TextureMods& GlobalTextureMods();
// Called right after each guest texture is decoded, before upload.
// True when the content was replaced: it is RGBA8 now, whatever the guest format.
bool ApplyTextureMods(guest::LinearTexture& texture);
// Registers the selected costume skin's recipe (costume_skin.cpp); no-op without one.
void InstallCostumeSkin(TextureMods& mods);
// Suit body textures, decoded whenever the game dresses Superman in that suit.
constexpr uint64_t kFilmSuitBodyKey = 0xD25F525A7A0BBE06ull;
constexpr uint64_t kGoldenAgeBodyKey = 0xE4620603169D8112ull;
// Decode counts, kept only while --sr_costume asks for another costume:
// costume_override.cpp re-applies the costume when the film suit comes back.
uint32_t FilmSuitDecodes();
uint32_t GoldenAgeDecodes();
}  // namespace superman_returns::graphics::texture_mods
