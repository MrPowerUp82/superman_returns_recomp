#include "test_main.h"
#include "../../port/src/graphics/texture_mods/kingdom_come.h"
#include "../../port/src/graphics/texture_mods/texture_mods.h"
#include <string>
#include <vector>

using namespace superman_returns::graphics;
using namespace superman_returns::graphics::texture_mods;

namespace {
Image Blank(uint32_t w, uint32_t h, Rgb color) {
  Image image{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
  for (size_t i = 0; i < image.rgba.size(); i += 4) {
    image.rgba[i] = color.r; image.rgba[i + 1] = color.g; image.rgba[i + 2] = color.b; image.rgba[i + 3] = 255;
  }
  return image;
}
const uint8_t* At(const Image& image, uint32_t x, uint32_t y) { return &image.rgba[(size_t(y) * image.width + x) * 4]; }
int Luma(const uint8_t* p) { return 299 * p[0] + 587 * p[1] + 114 * p[2]; }
guest::LinearTexture Rgba8x8(uint8_t fill) {
  guest::LinearTexture t;
  t.width = 8; t.height = 8; t.mip_levels = 1; t.dimension = 1;
  t.format = guest::LinearFormat::kRGBA8Unorm;
  t.levels.push_back({8, 8, 32, 8, 0});
  t.data.assign(8 * 8 * 4, fill);
  return t;
}
}  // namespace

SR_TEST(recolor_keeps_shading) {
  // Row 0: a blue gradient; row 1: red (other hue); row 2: blue outside the rect.
  Image image = Blank(8, 3, {200, 30, 30});
  for (uint32_t x = 0; x < 8; ++x) {
    uint8_t* p = &image.rgba[size_t(x) * 4];
    p[0] = 10; p[1] = uint8_t(40 + 10 * x); p[2] = uint8_t(120 + 15 * x);
    uint8_t* q = &image.rgba[(size_t(2) * 8 + x) * 4];
    q[0] = 10; q[1] = 80; q[2] = 200;
  }
  const Image before = image;
  RecolorHue(image, Rect{0, 0, 8, 2}, 210.0f, 40.0f, Rgb{128, 128, 128});
  for (uint32_t x = 0; x < 8; ++x) {
    const uint8_t* p = At(image, x, 0);
    SR_CHECK(p[0] == p[1] && p[1] == p[2]);  // grey now
    if (x) SR_CHECK(Luma(p) > Luma(At(image, x - 1, 0)));  // same luminance order
    for (int c = 0; c < 4; ++c) {
      SR_CHECK_EQ(At(image, x, 1)[c], At(before, x, 1)[c]);  // other hue untouched
      SR_CHECK_EQ(At(image, x, 2)[c], At(before, x, 2)[c]);  // outside the rect untouched
    }
  }
}

SR_TEST(recolor_in_outline_keeps_outside_pixels) {
  Image image = Blank(8, 8, {180, 30, 30});
  const Vec2 left_half[] = {{0, 0}, {4, 0}, {4, 8}, {0, 8}};
  RecolorHue(image, left_half, 0.0f, 40.0f, Rgb{20, 20, 200});
  SR_CHECK(At(image, 3, 5)[2] > 150);     // inside: blue now
  SR_CHECK_EQ(At(image, 4, 5)[0], 180u);  // outside: untouched
}

SR_TEST(polygon_stays_inside_clip) {
  Image image = Blank(16, 16, {0, 0, 255});
  const Vec2 triangle[] = {{-10.0f, -10.0f}, {40.0f, -10.0f}, {8.0f, 40.0f}};
  FillPolygon(image, triangle, Rgba{255, 0, 0, 255}, Rect{4, 4, 12, 12});
  for (uint32_t y = 0; y < 16; ++y)
    for (uint32_t x = 0; x < 16; ++x) {
      const bool inside = x >= 4 && x < 12 && y >= 4 && y < 12;
      SR_CHECK_EQ(At(image, x, y)[0], inside ? 255u : 0u);
      SR_CHECK_EQ(At(image, x, y)[2], inside ? 0u : 255u);
    }
}

SR_TEST(polygon_and_ellipse_antialias_edges) {
  Image image = Blank(16, 16, {0, 0, 0});
  const Vec2 half[] = {{0.0f, 0.0f}, {8.5f, 0.0f}, {8.5f, 16.0f}, {0.0f, 16.0f}};
  FillPolygon(image, half, Rgba{200, 200, 200, 255}, Rect{0, 0, 16, 16});
  SR_CHECK_EQ(At(image, 7, 5)[0], 200u);
  SR_CHECK(At(image, 8, 5)[0] > 50 && At(image, 8, 5)[0] < 150);  // half-covered pixel
  SR_CHECK_EQ(At(image, 9, 5)[0], 0u);
  Image oval = Blank(16, 16, {0, 0, 0});
  FillEllipse(oval, Vec2{8.0f, 8.0f}, Vec2{6.0f, 3.0f}, Rgba{0, 255, 0, 255}, Rect{0, 0, 16, 16});
  SR_CHECK_EQ(At(oval, 8, 8)[1], 255u);
  SR_CHECK_EQ(At(oval, 8, 2)[1], 0u);   // above the short radius
  SR_CHECK_EQ(At(oval, 3, 8)[1], 255u);  // inside the long radius
}

SR_TEST(unknown_key_paints_nothing) {
  Image image = Blank(512, 512, {8, 89, 197});
  const Image before = image;
  SR_CHECK(!PaintKingdomCome(0x0123456789ABCDEFull, image));
  SR_CHECK(image.rgba == before.rgba);
  SR_CHECK(!IsKingdomComeKey(0x0123456789ABCDEFull));
  SR_CHECK(IsKingdomComeKey(0xD25F525A7A0BBE06ull));
  SR_CHECK(!IsKingdomComeKey(0xE4620603169D8112ull));  // Golden Age stays as it is
}

SR_TEST(film_shield_turns_red_with_black_s_and_suit_stays_blue) {
  Image body = Blank(512, 512, {41, 70, 185});  // film blue
  for (uint32_t y = 8; y < 130; ++y)  // a maroon shield block with a gold stripe
    for (uint32_t x = 340; x < 480; ++x) {
      uint8_t* p = &body.rgba[(size_t(y) * 512 + x) * 4];
      const bool gold = y >= 100 && y < 110;
      p[0] = gold ? 207 : 84; p[1] = gold ? 133 : 34; p[2] = gold ? 9 : 48;
    }
  SR_CHECK(PaintKingdomCome(0xD25F525A7A0BBE06ull, body));
  const uint8_t* suit = At(body, 100, 300);
  SR_CHECK(suit[0] == 41 && suit[1] == 70 && suit[2] == 185);  // the suit keeps the film blue
  const uint8_t* rim = At(body, 380, 15);
  SR_CHECK(rim[0] > rim[1] + 80 && rim[0] > rim[2] + 80);  // red rim, no longer maroon
  for (const auto [x, y] : {std::pair{412u, 34u}, std::pair{400u, 100u}}) {
    const uint8_t* cut = At(body, x, y);
    SR_CHECK(cut[0] < 40 && cut[1] < 40 && cut[2] < 40);  // the two black cut-outs
  }
  const uint8_t* s_band = At(body, 412, 62);
  SR_CHECK(s_band[0] > s_band[1] + 80);  // the red S between them
}

SR_TEST(skin_warns_once_when_no_key_matches) {
  int warnings = 0;
  TextureMods mods(ModConfig{{}, {}, true});
  InstallKingdomCome(mods, [&](const std::string&) { ++warnings; }, 3);
  for (uint8_t fill = 0; fill < 10; ++fill) {
    auto t = Rgba8x8(fill);
    SR_CHECK(!mods.Apply(t));
  }
  SR_CHECK_EQ(warnings, 1);
}
