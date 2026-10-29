#include "test_main.h"
#include "../../port/src/graphics/texture_mods/texture_image.h"
#include <filesystem>
#include <fstream>

using namespace superman_returns::graphics;
using namespace superman_returns::graphics::texture_mods;

namespace {
guest::LinearTexture Rgba(uint32_t w, uint32_t h, uint32_t mips = 1) {
  guest::LinearTexture t;
  t.width = w; t.height = h; t.mip_levels = mips; t.dimension = 1;
  t.format = guest::LinearFormat::kRGBA8Unorm;
  size_t offset = 0;
  for (uint32_t m = 0; m < mips; ++m) {
    const uint32_t lw = std::max(1u, w >> m), lh = std::max(1u, h >> m);
    t.levels.push_back({lw, lh, lw * 4, lh, offset});
    offset += size_t(lw) * lh * 4;
  }
  t.data.assign(offset, 0);
  for (size_t i = 0; i < t.data.size(); ++i) t.data[i] = uint8_t(i * 7);
  return t;
}
std::filesystem::path Temp(const char* name) { return std::filesystem::temp_directory_path() / name; }
}  // namespace

SR_TEST(texture_key_ignores_guest_address) {
  auto a = Rgba(8, 8), b = Rgba(8, 8);
  a.base_address = 0x1000; b.base_address = 0x9000;
  SR_CHECK_EQ(TextureKey(a), TextureKey(b));
  b.data[3] ^= 1;
  SR_CHECK(TextureKey(a) != TextureKey(b));
  auto c = Rgba(8, 8); c.width = 9;
  SR_CHECK(TextureKey(a) != TextureKey(c));
}

SR_TEST(bc1_block_decodes_to_reference_colors) {
  // c0 = pure red (0xF800) > c1 = pure blue (0x001F): 4-color mode.
  // Index row 0 = 0,1,2,3 (bits 0b11100100), other rows index 0.
  guest::LinearTexture t;
  t.width = 4; t.height = 4; t.mip_levels = 1; t.dimension = 1;
  t.format = guest::LinearFormat::kBC1;
  t.levels.push_back({4, 4, 8, 1, 0});
  t.data = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0x00, 0x00, 0x00};
  Image image;
  SR_CHECK(BaseLevelToImage(t, image));
  SR_CHECK_EQ(image.width, 4u); SR_CHECK_EQ(image.height, 4u);
  auto px = [&](int x, int y) { return &image.rgba[(size_t(y) * 4 + x) * 4]; };
  SR_CHECK(px(0, 0)[0] == 255 && px(0, 0)[2] == 0 && px(0, 0)[3] == 255);   // c0 red
  SR_CHECK(px(1, 0)[0] == 0 && px(1, 0)[2] == 255);                          // c1 blue
  SR_CHECK(px(2, 0)[0] == 170 && px(2, 0)[2] == 85);                         // 2/3 c0 + 1/3 c1
  SR_CHECK(px(3, 0)[0] == 85 && px(3, 0)[2] == 170);                         // 1/3 c0 + 2/3 c1
  SR_CHECK(px(0, 3)[0] == 255);
}

SR_TEST(png_round_trip_keeps_pixels) {
  Image image{7, 5, {}};
  for (uint32_t i = 0; i < 7 * 5 * 4; ++i) image.rgba.push_back(uint8_t(i * 13 + 1));
  const auto path = Temp("sr_png_round_trip.png");
  SR_CHECK(WritePng(path, image));
  Image back;
  SR_CHECK(ReadPng(path, back));
  SR_CHECK_EQ(back.width, 7u); SR_CHECK_EQ(back.height, 5u);
  SR_CHECK(back.rgba == image.rgba);
  std::filesystem::remove(path);
}

SR_TEST(image_to_texture_builds_mip_chain) {
  auto t = Rgba(8, 8, 4);
  t.format = guest::LinearFormat::kBC3;
  t.swizzle = {2, 1, 0, 3};
  Image image{8, 8, std::vector<uint8_t>(8 * 8 * 4, 0)};
  // Top-left 2x2 block: 0, 40, 80, 120 in red -> mip 1 pixel 0 red = 60.
  image.rgba[0] = 0; image.rgba[4] = 40; image.rgba[8 * 4] = 80; image.rgba[8 * 4 + 4] = 120;
  ImageToTexture(image, t);
  SR_CHECK(t.format == guest::LinearFormat::kRGBA8Unorm);
  SR_CHECK_EQ(t.levels.size(), 4u);
  SR_CHECK(t.levels[1].width == 4 && t.levels[2].width == 2 && t.levels[3].width == 1);
  SR_CHECK_EQ(t.data[t.levels[1].offset], 60u);
  SR_CHECK(t.swizzle[0] == 2 && t.swizzle[2] == 0);
  SR_CHECK_EQ(t.data.size(), size_t((64 + 16 + 4 + 1) * 4));
}

SR_TEST(read_png_rejects_garbage) {
  const auto path = Temp("sr_png_garbage.png");
  { std::ofstream out(path, std::ios::binary); out << "not a png at all"; }
  Image image;
  SR_CHECK(!ReadPng(path, image));
  SR_CHECK(!ReadPng(Temp("sr_png_missing_file.png"), image));
  std::filesystem::remove(path);
}
