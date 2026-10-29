#include "kingdom_come.h"
#include "texture_mods.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace superman_returns::graphics::texture_mods {
namespace {
// The film suit (Superman Returns), the costume the game dresses Superman in:
// its shield already has the Kingdom Come shape, and the model has trunks.
constexpr uint64_t kBodyKey = 0xD25F525A7A0BBE06ull;               // blue suit + shield
constexpr uint64_t kCapeTrunksBootsKey = 0x7D58BD0FC5DA272Eull;  // cape, trunks, boots, belt

// Target look (spec; the CW suit Brandon Routh wore): the film's blue, a red
// shield with a black S, red trunks and boots, black belt with a gold oval
// buckle, dark red cape.
constexpr Rgb kShieldRed{0xB3, 0x14, 0x1B};
constexpr Rgba kEmblemBlack{0x11, 0x11, 0x11, 255};
constexpr Rgb kTrunksRed{0xB3, 0x14, 0x1B};
constexpr Rgb kBeltBlack{0x15, 0x15, 0x15};
constexpr Rgba kBuckleGold{0xC9, 0xA2, 0x3A, 255};
constexpr Rgba kBuckleLight{0xE2, 0xC0, 0x5A, 255};
constexpr Rgb kCapeRed{0x6E, 0x12, 0x18};

// Hues of the film suit textures (degrees): maroon cloth, gold S and belt.
constexpr float kMaroonHue = 340, kGoldHue = 35, kRedHue = 357;

float Luma(const uint8_t* p) { return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]; }

Rect Clamp(const Image& image, Rect rect) {
  rect.x0 = std::clamp(rect.x0, 0, int(image.width));
  rect.x1 = std::clamp(rect.x1, 0, int(image.width));
  rect.y0 = std::clamp(rect.y0, 0, int(image.height));
  rect.y1 = std::clamp(rect.y1, 0, int(image.height));
  return rect;
}

bool HueMatches(const uint8_t* p, float hue_from, float hue_width) {
  const float r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
  const float hi = std::max({r, g, b}), lo = std::min({r, g, b}), chroma = hi - lo;
  if (chroma < 0.15f || hi < 0.12f) return false;  // grey or black
  float hue;
  if (hi == r) hue = 60.0f * std::fmod((g - b) / chroma + 6.0f, 6.0f);
  else if (hi == g) hue = 60.0f * ((b - r) / chroma + 2.0f);
  else hue = 60.0f * ((r - g) / chroma + 4.0f);
  float distance = std::fabs(hue - hue_from);
  distance = std::min(distance, 360.0f - distance);
  return distance <= hue_width * 0.5f;
}

void Blend(uint8_t* p, Rgba color, float coverage) {
  const float alpha = coverage * color.a / 255.0f;
  p[0] = uint8_t(std::lround(p[0] + (color.r - p[0]) * alpha));
  p[1] = uint8_t(std::lround(p[1] + (color.g - p[1]) * alpha));
  p[2] = uint8_t(std::lround(p[2] + (color.b - p[2]) * alpha));
}

// Even-odd rule.
bool Inside(std::span<const Vec2> polygon, float x, float y) {
  bool inside = false;
  for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
    const Vec2 a = polygon[i], b = polygon[j];
    if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) inside = !inside;
  }
  return inside;
}

template <typename Covered>
void FillSamples(Image& image, Rect clip, Rgba color, Covered inside) {
  clip = Clamp(image, clip);
  for (int y = clip.y0; y < clip.y1; ++y)
    for (int x = clip.x0; x < clip.x1; ++x) {
      int hits = 0;
      for (int sy = 0; sy < 4; ++sy)
        for (int sx = 0; sx < 4; ++sx) hits += inside(x + (sx + 0.5f) / 4.0f, y + (sy + 0.5f) / 4.0f);
      if (hits) Blend(&image.rgba[(size_t(y) * image.width + x) * 4], color, hits / 16.0f);
    }
}

// The shield on the body texture: a pentagon with corners (342,8), (483,10),
// (509,42), tip (412,131) and (315,42). Emblem(x, y) maps a point of the
// 900x680 reference emblem (corners (160,12), (740,12), (885,160), tip
// (450,665), (15,160)) onto it, piecewise in y at the side corners.
constexpr Rect kEmblemRect{308, 2, 512, 138};
Vec2 Emblem(float x, float y) {
  const float ty = y <= 160 ? 8.0f + (y - 12) / 148.0f * 34.0f : 42.0f + (y - 160) / 505.0f * 89.0f;
  return {315.0f + (x - 15) / 870.0f * 194.0f, ty};
}

void PaintEmblem(Image& image) {
  // The shield turns red. Its rim keeps the cloth shading; the inside is filled
  // flat so the film's gold S leaves no trace.
  RecolorHue(image, kEmblemRect, kMaroonHue, 40, kShieldRed);
  RecolorHue(image, kEmblemRect, kGoldHue, 40, kShieldRed);
  const Vec2 shield[] = {{343, 11}, {482, 13}, {505, 42}, {412, 127}, {319, 42}};
  FillPolygon(image, shield, Rgba{kShieldRed.r, kShieldRed.g, kShieldRed.b, 255}, kEmblemRect);
  // Kingdom Come: the shield stays red and two black cut-outs leave the red S
  // (half a Z) between them. Drawn on the user's 900x680 reference emblem.
  const Vec2 upper[] = {
      Emblem(285, 75),  Emblem(715, 75),  Emblem(825, 180), Emblem(828, 192), Emblem(735, 298),
      Emblem(718, 300), Emblem(712, 285), Emblem(700, 258), Emblem(670, 240), Emblem(285, 128),
      Emblem(268, 110), Emblem(272, 88),
  };
  const Vec2 lower[] = {
      Emblem(168, 290), Emblem(530, 395), Emblem(565, 412), Emblem(582, 440),
      Emblem(578, 465), Emblem(445, 615),
  };
  FillPolygon(image, upper, kEmblemBlack, kEmblemRect);
  FillPolygon(image, lower, kEmblemBlack, kEmblemRect);
}

constexpr Vec2 kCapeOutlinePoints[] = {{0, 0},     {257, 0},    {257, 140}, {241, 172},
                                       {234, 200}, {222, 250}, {180, 302}, {0, 382}};
constexpr std::span<const Vec2> kCapeOutline{kCapeOutlinePoints};

void PaintCapeTrunksBoots(Image& image) {
  // Every maroon piece turns red, then the cape (the big curved UV island at
  // the left) turns dark red. The cape touches the trunks at (241,172), so it
  // is cut out by an outline along its edge, not by connectivity.
  RecolorHue(image, Rect{0, 0, 512, 512}, kMaroonHue, 40, kTrunksRed);
  RecolorHue(image, kCapeOutline, kRedHue, 40, kCapeRed);
  constexpr Rect belt{0, 400, 512, 512};
  RecolorHue(image, belt, kGoldHue, 40, kBeltBlack);
  // The film's S buckle becomes a gold oval.
  FillEllipse(image, Vec2{66, 466}, Vec2{34, 24}, kBuckleGold, belt);
  FillEllipse(image, Vec2{66, 464}, Vec2{25, 16}, kBuckleLight, belt);
}
}  // namespace

namespace {
// `pixels` (offsets into rgba) become `target` scaled by luminance over their mean.
void Recolor(Image& image, const std::vector<size_t>& pixels, Rgb target) {
  double sum = 0;
  for (const size_t at : pixels) sum += Luma(&image.rgba[at]);
  if (pixels.empty() || sum <= 0) return;
  const float mean = float(sum / pixels.size());
  for (const size_t at : pixels) {
    uint8_t* p = &image.rgba[at];
    const float scale = Luma(p) / mean;
    p[0] = uint8_t(std::clamp(std::lround(target.r * scale), 0L, 255L));
    p[1] = uint8_t(std::clamp(std::lround(target.g * scale), 0L, 255L));
    p[2] = uint8_t(std::clamp(std::lround(target.b * scale), 0L, 255L));
  }
}
}  // namespace

void RecolorHue(Image& image, Rect rect, float hue_from, float hue_width, Rgb target) {
  rect = Clamp(image, rect);
  std::vector<size_t> pixels;
  for (int y = rect.y0; y < rect.y1; ++y)
    for (int x = rect.x0; x < rect.x1; ++x) {
      const size_t at = (size_t(y) * image.width + x) * 4;
      if (HueMatches(&image.rgba[at], hue_from, hue_width)) pixels.push_back(at);
    }
  Recolor(image, pixels, target);
}


void RecolorHue(Image& image, std::span<const Vec2> outline, float hue_from, float hue_width, Rgb target) {
  std::vector<size_t> pixels;
  for (uint32_t y = 0; y < image.height; ++y)
    for (uint32_t x = 0; x < image.width; ++x) {
      const size_t at = (size_t(y) * image.width + x) * 4;
      if (Inside(outline, x + 0.5f, y + 0.5f) && HueMatches(&image.rgba[at], hue_from, hue_width))
        pixels.push_back(at);
    }
  Recolor(image, pixels, target);
}

void FillPolygon(Image& image, std::span<const Vec2> polygon, Rgba color, Rect clip) {
  if (polygon.size() < 3) return;
  FillSamples(image, clip, color, [&](float x, float y) { return Inside(polygon, x, y); });
}

void FillEllipse(Image& image, Vec2 center, Vec2 radii, Rgba color, Rect clip) {
  if (radii.x <= 0 || radii.y <= 0) return;
  FillSamples(image, clip, color, [&](float x, float y) {
    const float dx = (x - center.x) / radii.x, dy = (y - center.y) / radii.y;
    return dx * dx + dy * dy <= 1.0f;
  });
}

bool IsKingdomComeKey(uint64_t key) {
  return key == kBodyKey || key == kCapeTrunksBootsKey;
}

bool PaintKingdomCome(uint64_t key, Image& image) {
  if (!IsKingdomComeKey(key) || image.width != 512 || image.height != 512) return false;
  if (key == kBodyKey) PaintEmblem(image);
  else PaintCapeTrunksBoots(image);
  return true;
}

void InstallKingdomCome(TextureMods& mods, std::function<void(const std::string&)> warn,
                        uint32_t textures_before_warning) {
  struct Seen {
    std::atomic<uint32_t> textures{0};
    std::atomic<bool> matched{false}, warned{false};
  };
  auto seen = std::make_shared<Seen>();
  mods.SetRecipe(PaintKingdomCome, [seen, warn = std::move(warn), textures_before_warning](uint64_t key) {
    if (IsKingdomComeKey(key)) {
      seen->matched = true;
      return true;
    }
    if (!seen->matched && seen->textures.fetch_add(1) + 1 >= textures_before_warning &&
        !seen->warned.exchange(true) && warn)
      warn("Kingdom Come skin: the film suit was not seen yet. The skin paints over the Superman "
           "Returns suit: leave --sr_costume at default (or 0) and wear that suit in the costume menu");
    return false;
  });
}

}  // namespace superman_returns::graphics::texture_mods
