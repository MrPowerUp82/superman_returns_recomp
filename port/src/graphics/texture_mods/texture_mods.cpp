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
// Exactly 16 hex digits plus ".png". Compared on the wide name: a name the
// ANSI code page cannot hold must not throw.
bool ParseKey(const std::filesystem::path& file, uint64_t& key) {
  const std::wstring stem = file.stem().wstring();
  if (stem.size() != 16 || file.extension().wstring() != L".png") return false;
  key = 0;
  for (const wchar_t c : stem) {
    int digit;
    if (c >= L'0' && c <= L'9') digit = c - L'0';
    else if (c >= L'A' && c <= L'F') digit = c - L'A' + 10;
    else if (c >= L'a' && c <= L'f') digit = c - L'a' + 10;
    else return false;
    key = key << 4 | uint64_t(digit);
  }
  return true;
}
bool Plain2D(const guest::LinearTexture& t) {
  return t.dimension == 1 && t.depth == 1 && !t.levels.empty();
}
}  // namespace

TextureMods::TextureMods(ModConfig config) : config_(std::move(config)) {
  std::error_code error;
  if (!config_.replace || config_.replace_dir.empty() || !std::filesystem::is_directory(config_.replace_dir, error))
    return;
  // A scan error (permissions, a file removed meanwhile) ends the scan; it never throws.
  std::filesystem::directory_iterator it(config_.replace_dir, error), end;
  for (; !error && it != end; it.increment(error)) {
    uint64_t key = 0;
    if (it->is_regular_file(error) && ParseKey(it->path(), key)) replace_keys_.insert(key);
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

// Read once per key: the decoded image, or null when the file is unusable
// (warned once). The size is checked from the header before decoding.
std::shared_ptr<const Image> TextureMods::Replacement(uint64_t key, uint32_t width, uint32_t height) {
  {
    std::lock_guard lock(mutex_);
    if (const auto found = replacements_.find(key); found != replacements_.end()) return found->second;
  }
  const auto path = config_.replace_dir / (KeyText(key) + ".png");
  std::shared_ptr<const Image> result;
  uint32_t png_width = 0, png_height = 0;
  if (!PngSize(path, png_width, png_height)) {
    WarnOnce(key, "texture " + KeyText(key) + ".png could not be read; using the original");
  } else if (png_width != width || png_height != height) {
    WarnOnce(key, "texture " + KeyText(key) + ".png is " + std::to_string(png_width) + "x" +
                      std::to_string(png_height) + ", the original is " + std::to_string(width) + "x" +
                      std::to_string(height) + "; using the original");
  } else if (auto image = std::make_shared<Image>(); ReadPng(path, *image) && image->width == width &&
                                                      image->height == height) {
    result = std::move(image);
  } else {
    WarnOnce(key, "texture " + KeyText(key) + ".png could not be read; using the original");
  }
  std::lock_guard lock(mutex_);
  return replacements_.try_emplace(key, std::move(result)).first->second;
}

bool TextureMods::Apply(guest::LinearTexture& texture) {
  if (!Active() || !Plain2D(texture)) return false;
  hashes_.fetch_add(1, std::memory_order_relaxed);
  const uint64_t key = TextureKey(texture);
  if (observer_) observer_(key);
  if (!config_.dump_dir.empty()) Dump(key, texture);
  const uint32_t width = texture.levels[0].width, height = texture.levels[0].height;
  if (replace_keys_.count(key)) {
    if (const auto image = Replacement(key, width, height)) {
      ImageToTexture(*image, texture);
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
