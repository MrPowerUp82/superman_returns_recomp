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
