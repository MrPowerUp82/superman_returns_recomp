#include "test_main.h"
#include "../../port/src/graphics/texture_mods/texture_mods.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace superman_returns::graphics;
using namespace superman_returns::graphics::texture_mods;

namespace {
guest::LinearTexture Rgba8x8(uint8_t fill) {
  guest::LinearTexture t;
  t.width = 8; t.height = 8; t.mip_levels = 1; t.dimension = 1;
  t.format = guest::LinearFormat::kRGBA8Unorm;
  t.levels.push_back({8, 8, 32, 8, 0});
  t.data.assign(8 * 8 * 4, fill);
  return t;
}
std::filesystem::path FreshDir(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / name;
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
  return dir;
}
std::string KeyName(uint64_t key) {
  char text[32];
  std::snprintf(text, sizeof(text), "%016llX.png", static_cast<unsigned long long>(key));
  return text;
}
Image Solid(uint32_t w, uint32_t h, uint8_t r) {
  Image image{w, h, std::vector<uint8_t>(size_t(w) * h * 4, 0)};
  for (size_t i = 0; i < image.rgba.size(); i += 4) { image.rgba[i] = r; image.rgba[i + 3] = 255; }
  return image;
}
}  // namespace

SR_TEST(mods_inactive_never_hash) {
  TextureMods mods(ModConfig{{}, FreshDir("sr_mods_missing"), true});
  auto t = Rgba8x8(9);
  const auto before = t.data;
  SR_CHECK(!mods.Apply(t));
  SR_CHECK_EQ(mods.hashes_computed(), 0u);
  SR_CHECK(t.data == before);
  SR_CHECK(!std::filesystem::exists(std::filesystem::temp_directory_path() / "sr_mods_missing"));
}

SR_TEST(mods_dump_writes_each_texture_once) {
  const auto dir = FreshDir("sr_mods_dump");
  TextureMods mods(ModConfig{dir, {}, true});
  auto t = Rgba8x8(9);
  const uint64_t key = TextureKey(t);
  mods.Apply(t);
  auto again = Rgba8x8(9);
  mods.Apply(again);
  SR_CHECK(std::filesystem::exists(dir / KeyName(key)));
  std::ifstream csv(dir / "index.csv");
  std::string header, line, extra;
  std::getline(csv, header);
  std::getline(csv, line);
  SR_CHECK(header == "key,width,height,format");
  SR_CHECK(line.rfind(KeyName(key).substr(0, 16) + ",8,8,", 0) == 0);
  SR_CHECK(!std::getline(csv, extra));  // one line per texture
  csv.close();
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(mods_replace_uses_png_with_same_size) {
  const auto dir = FreshDir("sr_mods_replace");
  std::filesystem::create_directories(dir);
  auto t = Rgba8x8(9);
  SR_CHECK(WritePng(dir / KeyName(TextureKey(t)), Solid(8, 8, 200)));
  TextureMods mods(ModConfig{{}, dir, true});
  SR_CHECK(mods.Apply(t));
  SR_CHECK(t.format == guest::LinearFormat::kRGBA8Unorm);
  SR_CHECK_EQ(t.data[0], 200u);
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(mods_replace_ignores_other_size_and_garbage) {
  const auto dir = FreshDir("sr_mods_bad");
  std::filesystem::create_directories(dir);
  auto wrong_size = Rgba8x8(9), garbage = Rgba8x8(11);
  SR_CHECK(WritePng(dir / KeyName(TextureKey(wrong_size)), Solid(16, 16, 200)));
  { std::ofstream out(dir / KeyName(TextureKey(garbage)), std::ios::binary); out << "junk"; }
  int warnings = 0;
  TextureMods mods(ModConfig{{}, dir, true});
  mods.SetWarn([&](const std::string&) { ++warnings; });
  const auto before = wrong_size.data;
  SR_CHECK(!mods.Apply(wrong_size));
  SR_CHECK(wrong_size.data == before);
  auto wrong_again = Rgba8x8(9);
  SR_CHECK(!mods.Apply(wrong_again));
  SR_CHECK(!mods.Apply(garbage));
  SR_CHECK_EQ(garbage.data[0], 11u);
  SR_CHECK_EQ(warnings, 2);  // once per key
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(mods_scan_survives_unicode_and_odd_names) {
  const auto dir = FreshDir("sr_mods_unicode");
  std::filesystem::create_directories(dir);
  auto t = Rgba8x8(9);
  { std::ofstream(dir / std::filesystem::path(L"Superman_日本.png")) << "x"; }
  { std::ofstream(dir / std::filesystem::path(L" 00000000000001F.png")) << "x"; }
  SR_CHECK(WritePng(dir / KeyName(TextureKey(t)), Solid(8, 8, 200)));
  int warnings = 0;
  TextureMods mods(ModConfig{{}, dir, true});  // must not throw
  mods.SetWarn([&](const std::string&) { ++warnings; });
  SR_CHECK(mods.Apply(t));
  SR_CHECK_EQ(warnings, 0);  // the odd names are ignored, not warned about
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(mods_png_is_read_once_per_key) {
  const auto dir = FreshDir("sr_mods_once");
  std::filesystem::create_directories(dir);
  auto t = Rgba8x8(9);
  const auto png = dir / KeyName(TextureKey(t));
  SR_CHECK(WritePng(png, Solid(8, 8, 200)));
  TextureMods mods(ModConfig{{}, dir, true});
  SR_CHECK(mods.Apply(t));
  std::filesystem::remove(png);  // later decodes use the cached image
  auto again = Rgba8x8(9);
  SR_CHECK(mods.Apply(again));
  SR_CHECK_EQ(again.data[0], 200u);
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(mods_png_with_other_size_is_rejected_from_its_header) {
  const auto dir = FreshDir("sr_mods_header");
  std::filesystem::create_directories(dir);
  auto t = Rgba8x8(9);
  const auto png = dir / KeyName(TextureKey(t));
  SR_CHECK(WritePng(png, Solid(64, 64, 200)));
  Image size_only;
  SR_CHECK(PngSize(png, size_only.width, size_only.height));
  SR_CHECK_EQ(size_only.width, 64u);
  SR_CHECK_EQ(size_only.height, 64u);
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(mods_png_overrides_recipe) {
  const auto dir = FreshDir("sr_mods_priority");
  std::filesystem::create_directories(dir);
  auto t = Rgba8x8(9);
  SR_CHECK(WritePng(dir / KeyName(TextureKey(t)), Solid(8, 8, 200)));
  TextureMods mods(ModConfig{{}, dir, true});
  mods.SetRecipe([](uint64_t, Image& image) { image.rgba[0] = 50; return true; });
  SR_CHECK(mods.Apply(t));
  SR_CHECK_EQ(t.data[0], 200u);
  auto other = Rgba8x8(7);  // no PNG: the recipe paints
  SR_CHECK(mods.Apply(other));
  SR_CHECK_EQ(other.data[0], 50u);
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(mods_skip_non_2d_textures) {
  TextureMods mods(ModConfig{{}, {}, true});
  mods.SetRecipe([](uint64_t, Image& image) { image.rgba[0] = 50; return true; });
  auto cube = Rgba8x8(9); cube.depth = 6; cube.dimension = 3;
  auto volume = Rgba8x8(9); volume.dimension = 2;
  SR_CHECK(!mods.Apply(cube));
  SR_CHECK(!mods.Apply(volume));
  SR_CHECK_EQ(cube.data[0], 9u);
  SR_CHECK_EQ(volume.data[0], 9u);
}

SR_TEST(mods_observer_sees_every_decode) {
  TextureMods mods(ModConfig{{}, {}, true});
  std::vector<uint64_t> keys;
  mods.SetObserver([&](uint64_t key) { keys.push_back(key); });
  auto a = Rgba8x8(9), b = Rgba8x8(9);
  const auto before = a.data;
  SR_CHECK(!mods.Apply(a));
  mods.Apply(b);
  SR_CHECK_EQ(keys.size(), 2u);  // every decode, not once per key
  SR_CHECK_EQ(keys[0], TextureKey(b));
  SR_CHECK(a.data == before);
}

SR_TEST(mods_report_each_replacement_once) {
  TextureMods mods(ModConfig{{}, {}, true});
  mods.SetRecipe([](uint64_t, Image& image) { image.rgba[0] = 50; return true; });
  std::vector<std::string> notes;
  mods.SetNote([&](const std::string& note) { notes.push_back(note); });
  auto a = Rgba8x8(9), b = Rgba8x8(9);
  mods.Apply(a);
  mods.Apply(b);
  SR_CHECK_EQ(notes.size(), 1u);
  SR_CHECK(notes[0].find("recipe") != std::string::npos);
}

namespace {
// 8x8 texture whose alpha is a mask: 0 on the left half (with color under it,
// like the skin under Superman's hair mask), 255 on the right half.
guest::LinearTexture MaskedRgba8x8() {
  auto t = Rgba8x8(0);
  for (uint32_t i = 0; i < 64; ++i) {
    uint8_t* p = &t.data[i * 4];
    p[0] = 210; p[1] = 150; p[2] = 120;
    p[3] = (i % 8) < 4 ? 0 : 255;
  }
  return t;
}
std::string Named(uint64_t key, const char* suffix) { return KeyName(key).substr(0, 16) + suffix; }
// 8x8 RGB PNG (color type 2, no alpha) filled with (200,10,20).
const unsigned char kRgbPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x08, 0x08, 0x02, 0x00, 0x00, 0x00, 0x4B, 0x6D, 0x29, 0xDC, 0x00, 0x00, 0x00, 0x14, 0x49,
    0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x3C, 0xC1, 0x25, 0xC2, 0x80, 0x0D, 0x30, 0x61, 0x15, 0x1D, 0xB4, 0x12, 0x00,
    0xB9, 0xA0, 0x00, 0xF6, 0x23, 0x8B, 0x53, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60,
    0x82};
}  // namespace

SR_TEST(mods_color_png_keeps_the_original_alpha) {
  const auto dir = FreshDir("sr_mods_color");
  std::filesystem::create_directories(dir);
  auto t = MaskedRgba8x8();
  Image color = Solid(8, 8, 90);
  for (size_t i = 3; i < color.rgba.size(); i += 4) color.rgba[i] = 0;  // its alpha is ignored
  SR_CHECK(WritePng(dir / Named(TextureKey(t), ".color.png"), color));
  TextureMods mods(ModConfig{{}, dir, true});
  SR_CHECK(mods.Apply(t));
  SR_CHECK_EQ(t.data[0], 90u);       // color from the file
  SR_CHECK_EQ(t.data[3], 0u);        // left: original alpha 0
  SR_CHECK_EQ(t.data[7 * 4 + 3], 255u);  // right: original alpha 255
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(mods_alpha_png_replaces_only_alpha) {
  const auto dir = FreshDir("sr_mods_alpha");
  std::filesystem::create_directories(dir);
  auto t = MaskedRgba8x8();
  Image alpha = Solid(8, 8, 77);  // gray is read from red
  SR_CHECK(WritePng(dir / Named(TextureKey(t), ".alpha.png"), alpha));
  TextureMods mods(ModConfig{{}, dir, true});
  SR_CHECK(mods.Apply(t));
  SR_CHECK_EQ(t.data[0], 210u);
  SR_CHECK_EQ(t.data[3], 77u);
  SR_CHECK_EQ(t.data[7 * 4 + 3], 77u);
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(mods_png_gets_back_color_an_editor_dropped_under_alpha_zero) {
  const auto dir = FreshDir("sr_mods_restore");
  std::filesystem::create_directories(dir);
  auto t = MaskedRgba8x8();
  Image edited{8, 8, t.data};
  for (size_t i = 0; i < edited.rgba.size(); i += 4) {
    if (edited.rgba[i + 3] == 0) edited.rgba[i] = edited.rgba[i + 1] = edited.rgba[i + 2] = 0;  // the editor's black
    else edited.rgba[i] = 30;                                                                  // painted hair
  }
  SR_CHECK(WritePng(dir / KeyName(TextureKey(t)), edited));
  std::vector<std::string> notes;
  TextureMods mods(ModConfig{{}, dir, true});
  mods.SetNote([&](const std::string& note) { notes.push_back(note); });
  SR_CHECK(mods.Apply(t));
  SR_CHECK_EQ(t.data[0], 210u);  // the skin is back
  SR_CHECK_EQ(t.data[3], 0u);
  SR_CHECK_EQ(t.data[7 * 4], 30u);  // the paint stays
  SR_CHECK_EQ(notes.size(), 1u);
  SR_CHECK(notes[0].find("restored") != std::string::npos);
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(mods_png_without_alpha_keeps_the_original_alpha) {
  const auto dir = FreshDir("sr_mods_rgb");
  std::filesystem::create_directories(dir);
  auto t = MaskedRgba8x8();
  {
    std::ofstream out(dir / KeyName(TextureKey(t)), std::ios::binary);
    out.write(reinterpret_cast<const char*>(kRgbPng), sizeof(kRgbPng));
  }
  TextureMods mods(ModConfig{{}, dir, true});
  SR_CHECK(mods.Apply(t));
  SR_CHECK_EQ(t.data[0], 200u);
  SR_CHECK_EQ(t.data[3], 0u);
  SR_CHECK_EQ(t.data[7 * 4 + 3], 255u);
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(mods_scan_finds_files_in_subfolders_and_warns_on_duplicates) {
  const auto dir = FreshDir("sr_mods_folders");
  std::filesystem::create_directories(dir / "a_skin");
  std::filesystem::create_directories(dir / "b_skin" / "deeper");
  auto t = Rgba8x8(9);
  SR_CHECK(WritePng(dir / "a_skin" / KeyName(TextureKey(t)), Solid(8, 8, 100)));
  SR_CHECK(WritePng(dir / "b_skin" / "deeper" / KeyName(TextureKey(t)), Solid(8, 8, 200)));
  std::vector<std::string> warnings;
  TextureMods mods(ModConfig{{}, dir, true});
  mods.SetWarn([&](const std::string& message) { warnings.push_back(message); });
  SR_CHECK(mods.Apply(t));
  SR_CHECK_EQ(t.data[0], 100u);  // sorted by path: a_skin wins
  SR_CHECK_EQ(warnings.size(), 1u);
  SR_CHECK(warnings[0].find("two files") != std::string::npos);
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(mods_dump_adds_color_and_alpha_files_for_textures_with_alpha) {
  const auto dir = FreshDir("sr_mods_dump_alpha");
  TextureMods mods(ModConfig{dir, {}, true});
  auto masked = MaskedRgba8x8();
  auto opaque = Rgba8x8(255);
  const uint64_t masked_key = TextureKey(masked), opaque_key = TextureKey(opaque);
  mods.Apply(masked);
  mods.Apply(opaque);
  Image color, alpha;
  SR_CHECK(ReadPng(dir / Named(masked_key, ".color.png"), color));
  SR_CHECK(ReadPng(dir / Named(masked_key, ".alpha.png"), alpha));
  SR_CHECK_EQ(color.rgba[0], 210u);
  SR_CHECK_EQ(color.rgba[3], 255u);  // opaque, so editors keep the color
  SR_CHECK_EQ(alpha.rgba[0], 0u);
  SR_CHECK_EQ(alpha.rgba[7 * 4], 255u);
  SR_CHECK(std::filesystem::exists(dir / KeyName(opaque_key)));
  SR_CHECK(!std::filesystem::exists(dir / Named(opaque_key, ".color.png")));
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

SR_TEST(mods_tag_paints_matching_textures_and_keeps_alpha) {
  auto t = MaskedRgba8x8();
  auto other = Rgba8x8(9);
  const uint64_t key = TextureKey(t);
  ModConfig config{{}, {}, true};
  config.tag = KeyName(key).substr(0, 6);
  TextureMods mods(config);
  SR_CHECK(mods.Apply(t));
  uint8_t fill[3];
  TagColor(key, fill);
  bool saw_fill = false;
  for (size_t i = 0; i < 64 * 4; i += 4) saw_fill |= t.data[i] == fill[0] && t.data[i + 1] == fill[1];
  SR_CHECK(saw_fill);
  SR_CHECK_EQ(t.data[3], 0u);
  SR_CHECK_EQ(t.data[7 * 4 + 3], 255u);
  if (KeyName(TextureKey(other)).substr(0, 6) != config.tag) SR_CHECK(!mods.Apply(other));
}

SR_TEST(mods_tag_all_writes_the_key_in_ink) {
  Image image{256, 256, std::vector<uint8_t>(256 * 256 * 4, 255)};
  PaintTag(0x0123456789ABCDEFull, image);
  uint8_t fill[3];
  TagColor(0x0123456789ABCDEFull, fill);
  size_t ink = 0;
  for (size_t i = 0; i < image.rgba.size(); i += 4)
    if (image.rgba[i] != fill[0] || image.rgba[i + 1] != fill[1] || image.rgba[i + 2] != fill[2]) ++ink;
  SR_CHECK(ink > 256 * 256 / 20);  // the text covers a good part of it
  SR_CHECK(ink < 256 * 256 / 2);
  ModConfig config{{}, {}, true};
  config.tag = "all";
  TextureMods mods(config);
  auto t = Rgba8x8(9);
  SR_CHECK(mods.Apply(t));
}
