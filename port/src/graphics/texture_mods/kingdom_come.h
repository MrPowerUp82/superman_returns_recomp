#pragma once
// Kingdom Come skin (the CW suit Brandon Routh wore) painted over the player's
// own film-suit (Superman Returns) textures. Keys and UV regions:
// docs/kingdom-come-texture-map-2026-10-28.md.
#include "texture_image.h"
#include <functional>
#include <span>
#include <string>

namespace superman_returns::graphics::texture_mods {
class TextureMods;

struct Rect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };  // pixels, half-open
struct Rgb { uint8_t r = 0, g = 0, b = 0; };
struct Rgba { uint8_t r = 0, g = 0, b = 0, a = 255; };
struct Vec2 { float x = 0, y = 0; };

// Pixels inside `rect` whose hue (degrees) is within hue_width/2 of hue_from,
// and that are not grey or black, become `target`, scaled by their luminance
// over the mean luminance of the matched pixels (shading is kept).
void RecolorHue(Image& image, Rect rect, float hue_from, float hue_width, Rgb target);
// Same, inside a polygon (pixel centers, even-odd rule).
void RecolorHue(Image& image, std::span<const Vec2> outline, float hue_from, float hue_width, Rgb target);
// Antialiased fill (4x4 samples per pixel, even-odd rule), never outside `clip`.
void FillPolygon(Image& image, std::span<const Vec2> polygon, Rgba color, Rect clip);
void FillEllipse(Image& image, Vec2 center, Vec2 radii, Rgba color, Rect clip);

bool IsKingdomComeKey(uint64_t key);
// Repaints one film-suit texture; false (and `image` untouched) for any other key.
bool PaintKingdomCome(uint64_t key, Image& image);
// Registers the recipe on `mods`. If `textures_before_warning` textures pass
// without any film-suit texture, `warn` is called once.
void InstallKingdomCome(TextureMods& mods, std::function<void(const std::string&)> warn,
                        uint32_t textures_before_warning);

}  // namespace superman_returns::graphics::texture_mods
