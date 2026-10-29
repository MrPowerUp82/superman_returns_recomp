#include "texture_mods.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace superman_returns::graphics::texture_mods {
namespace {
std::string KeyText(uint64_t key) {
  char text[24];
  std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(key));
  return text;
}
bool ParseKey(const std::filesystem::path& file, uint64_t& key) {
  const auto stem = file.stem().string();
  if (stem.size() != 16 || file.extension() != ".png") return false;
  char* end = nullptr;
  key = std::strtoull(stem.c_str(), &end, 16);
  return end == stem.c_str() + 16;
}
bool Plain2D(const guest::LinearTexture& t) {
  return t.dimension == 1 && t.depth == 1 && !t.levels.empty();
}
}  // namespace

TextureMods::TextureMods(ModConfig config) : config_(std::move(config)) {
  std::error_code error;
  if (config_.replace && !config_.replace_dir.empty() && std::filesystem::is_directory(config_.replace_dir, error)) {
    for (const auto& entry : std::filesystem::directory_iterator(config_.replace_dir, error)) {
      uint64_t key = 0;
      if (entry.is_regular_file(error) && ParseKey(entry.path(), key)) replace_keys_.insert(key);
    }
  }
}

void TextureMods::SetRecipe(std::function<bool(uint64_t, Image&)> recipe, std::function<bool(uint64_t)> wants) {
  recipe_ = std::move(recipe);
  recipe_wants_ = std::move(wants);
}
void TextureMods::SetWarn(std::function<void(const std::string&)> warn) { warn_ = std::move(warn); }
void TextureMods::SetNote(std::function<void(const std::string&)> note) { note_ = std::move(note); }
void TextureMods::SetObserver(std::function<void(uint64_t)> observer) { observer_ = std::move(observer); }

void TextureMods::NoteOnce(uint64_t key, const char* source) {
  std::lock_guard lock(mutex_);
  if (noted_.insert(key).second && note_) note_("replaced " + KeyText(key) + " (" + source + ")");
}

bool TextureMods::Active() const {
  return !config_.dump_dir.empty() || !replace_keys_.empty() || bool(recipe_) || bool(observer_);
}

void TextureMods::WarnOnce(uint64_t key, const std::string& message) {
  std::lock_guard lock(mutex_);
  if (warned_.insert(key).second && warn_) warn_(message);
}

void TextureMods::Dump(uint64_t key, const guest::LinearTexture& texture) {
  {
    std::lock_guard lock(mutex_);
    if (!dumped_.insert(key).second) return;
  }
  Image image;
  if (!BaseLevelToImage(texture, image)) return;
  std::error_code error;
  std::filesystem::create_directories(config_.dump_dir, error);
  if (error) return;
  WritePng(config_.dump_dir / (KeyText(key) + ".png"), image);
  std::lock_guard lock(mutex_);
  std::ofstream index(config_.dump_dir / "index.csv", std::ios::app);
  if (!dump_index_started_) {
    dump_index_started_ = true;
    if (index.tellp() == 0) index << "key,width,height,format\n";
  }
  index << KeyText(key) << ',' << texture.width << ',' << texture.height << ','
        << static_cast<int>(texture.format) << '\n';
}

bool TextureMods::Apply(guest::LinearTexture& texture) {
  if (!Active() || !Plain2D(texture)) return false;
  hashes_.fetch_add(1, std::memory_order_relaxed);
  const uint64_t key = TextureKey(texture);
  if (observer_) observer_(key);
  if (!config_.dump_dir.empty()) Dump(key, texture);
  const uint32_t width = texture.levels[0].width, height = texture.levels[0].height;
  if (replace_keys_.count(key)) {
    Image image;
    if (!ReadPng(config_.replace_dir / (KeyText(key) + ".png"), image)) {
      WarnOnce(key, "texture " + KeyText(key) + ".png could not be read; using the original");
    } else if (image.width != width || image.height != height) {
      WarnOnce(key, "texture " + KeyText(key) + ".png is " + std::to_string(image.width) + "x" +
                        std::to_string(image.height) + ", the original is " + std::to_string(width) + "x" +
                        std::to_string(height) + "; using the original");
    } else {
      ImageToTexture(image, texture);
      NoteOnce(key, "png");
      return true;
    }
  }
  if (recipe_ && (!recipe_wants_ || recipe_wants_(key))) {
    Image image;
    if (BaseLevelToImage(texture, image) && recipe_(key, image)) {
      ImageToTexture(image, texture);
      NoteOnce(key, "recipe");
      return true;
    }
  }
  return false;
}

}  // namespace superman_returns::graphics::texture_mods
