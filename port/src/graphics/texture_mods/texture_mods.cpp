#include "texture_mods.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <tuple>

namespace superman_returns::graphics::texture_mods {
namespace {
std::string KeyText(uint64_t key) {
  char text[24];
  std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(key));
  return text;
}

enum class FileKind { kFull, kColor, kAlpha };

bool EndsWithNoCase(const std::wstring& text, const wchar_t* suffix) {
  const size_t n = std::char_traits<wchar_t>::length(suffix);
  if (text.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    wchar_t c = text[text.size() - n + i];
    if (c >= L'A' && c <= L'Z') c = wchar_t(c - L'A' + L'a');
    if (c != suffix[i]) return false;
  }
  return true;
}

// <16 hex>.png, <16 hex>.color.png or <16 hex>.alpha.png (any case). Compared
// on the wide name: a name the ANSI code page cannot hold must not throw.
bool ParseName(const std::filesystem::path& file, uint64_t& key, FileKind& kind) {
  std::wstring name = file.filename().wstring();
  if (EndsWithNoCase(name, L".color.png")) kind = FileKind::kColor, name.resize(name.size() - 10);
  else if (EndsWithNoCase(name, L".alpha.png")) kind = FileKind::kAlpha, name.resize(name.size() - 10);
  else if (EndsWithNoCase(name, L".png")) kind = FileKind::kFull, name.resize(name.size() - 4);
  else return false;
  if (name.size() != 16) return false;
  key = 0;
  for (const wchar_t c : name) {
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

bool HasTransparency(const Image& image) {
  for (size_t i = 3; i < image.rgba.size(); i += 4)
    if (image.rgba[i] != 255) return true;
  return false;
}

std::string PathText(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}

// 3x5 glyphs, one row per 3 bits (bit 2 = left): 0-9, A-F, then a right-pointing
// arrow that starts each line, so a mirrored UV shows up as a left-pointing one.
constexpr uint8_t kGlyphs[17][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
    {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {7, 5, 7, 5, 5}, {6, 5, 6, 5, 6},
    {7, 4, 4, 4, 7}, {6, 5, 5, 5, 6}, {7, 4, 7, 4, 7}, {7, 4, 7, 4, 4}, {4, 6, 7, 6, 4}};
constexpr int kArrow = 16;
}  // namespace

void TagColor(uint64_t key, uint8_t rgb[3]) {
  // Saturated hue from the key; light enough for black text.
  const uint64_t mixed = key * 0x9E3779B97F4A7C15ull;
  const float hue = float(mixed >> 40 & 0xFFFF) / 65536.0f * 6.0f;
  const int sector = int(hue) % 6;
  const float f = hue - float(int(hue));
  const float v = 1.0f, lo = 0.25f, down = v - (v - lo) * f, up = lo + (v - lo) * f;
  const float table[6][3] = {{v, up, lo}, {down, v, lo}, {lo, v, up}, {lo, down, v}, {up, lo, v}, {v, lo, down}};
  for (int c = 0; c < 3; ++c) rgb[c] = uint8_t(table[sector][c] * 255.0f + 0.5f);
}

void PaintTag(uint64_t key, Image& image) {
  uint8_t fill[3];
  TagColor(key, fill);
  const uint32_t w = image.width, h = image.height;
  for (size_t i = 0; i < image.rgba.size(); i += 4) std::copy(fill, fill + 3, &image.rgba[i]);
  // Two lines of eight digits, each after an arrow: 36 units wide (4 per glyph),
  // 13 tall with the gaps, tiled over the whole texture.
  const std::string text = KeyText(key);
  const uint32_t unit = std::max(1u, std::min(w, h) / 76);
  const uint32_t block_w = 38 * unit, block_h = 13 * unit;
  const uint8_t ink = (fill[0] * 3 + fill[1] * 6 + fill[2]) / 10 > 110 ? 0 : 255;
  auto plot = [&](uint32_t x0, uint32_t y0) {
    for (uint32_t dy = 0; dy < unit && y0 + dy < h; ++dy)
      for (uint32_t dx = 0; dx < unit && x0 + dx < w; ++dx) {
        uint8_t* p = &image.rgba[(size_t(y0 + dy) * w + x0 + dx) * 4];
        p[0] = p[1] = p[2] = ink;
      }
  };
  for (uint32_t row = 0, by = unit; by < h; ++row, by += block_h) {
    const uint32_t shift = (row & 1) ? block_w / 2 : 0;
    for (int64_t bx = int64_t(unit) - int64_t(shift); bx < int64_t(w); bx += block_w) {
      for (int line = 0; line < 2; ++line) {
        for (int g = 0; g < 9; ++g) {
          const int glyph = g == 0 ? kArrow : std::isdigit(text[line * 8 + g - 1]) ? text[line * 8 + g - 1] - '0'
                                                                                     : text[line * 8 + g - 1] - 'A' + 10;
          for (int gy = 0; gy < 5; ++gy)
            for (int gx = 0; gx < 3; ++gx) {
              if (!(kGlyphs[glyph][gy] >> (2 - gx) & 1)) continue;
              const int64_t x = bx + int64_t((g * 4 + gx) * unit);
              const uint32_t y = by + uint32_t((line * 6 + gy) * unit);
              if (x >= 0 && x < int64_t(w) && y < h) plot(uint32_t(x), y);
            }
        }
      }
    }
  }
}

TextureMods::TextureMods(ModConfig config) : config_(std::move(config)) {
  // Tag filter: "all", or key prefixes separated by commas or spaces.
  std::string token;
  auto flush = [&] {
    if (token.empty()) return;
    std::string upper;
    for (const char c : token) upper += char(std::toupper(static_cast<unsigned char>(c)));
    if (upper == "ALL" || upper == "TRUE" || upper == "1") tag_all_ = true;
    else if (upper == "NONE" || upper == "FALSE" || upper == "OFF" || upper == "0") {}
    else if (upper.size() <= 16 && upper.find_first_not_of("0123456789ABCDEF") == std::string::npos)
      tag_prefixes_.push_back(upper);
    else scan_warnings_.push_back("ignoring tag filter '" + token + "' (expected all or hex key prefixes)");
    token.clear();
  };
  for (const char c : config_.tag) {
    if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c))) flush();
    else token += c;
  }
  flush();

  std::error_code error;
  if (!config_.replace || config_.replace_dir.empty() || !std::filesystem::is_directory(config_.replace_dir, error))
    return;
  // Subfolders too, so a skin can be shared as a folder. A scan error
  // (permissions, a file removed meanwhile) ends the scan; it never throws.
  std::vector<std::tuple<std::filesystem::path, uint64_t, FileKind>> found;
  std::filesystem::recursive_directory_iterator it(
      config_.replace_dir, std::filesystem::directory_options::skip_permission_denied, error),
      end;
  for (; !error && it != end; it.increment(error)) {
    uint64_t key = 0;
    FileKind kind;
    if (it->is_regular_file(error) && ParseName(it->path(), key, kind)) found.emplace_back(it->path(), key, kind);
  }
  // Sorted, so the same file wins every run when two folders carry one key.
  std::sort(found.begin(), found.end(),
            [](const auto& a, const auto& b) { return std::get<0>(a).wstring() < std::get<0>(b).wstring(); });
  for (const auto& [path, key, kind] : found) {
    auto& files = replace_files_[key];
    auto& slot = kind == FileKind::kFull ? files.full : kind == FileKind::kColor ? files.color : files.alpha;
    if (slot.empty()) slot = path;
    else scan_warnings_.push_back("two files for the same texture: using " + PathText(slot) + ", ignoring " +
                                  PathText(path));
  }
}

void TextureMods::SetRecipe(std::function<bool(uint64_t, Image&)> recipe, std::function<bool(uint64_t)> wants) {
  recipe_ = std::move(recipe);
  recipe_wants_ = std::move(wants);
}
void TextureMods::SetWarn(std::function<void(const std::string&)> warn) {
  warn_ = std::move(warn);
  if (warn_)
    for (const auto& message : scan_warnings_) warn_(message);
  scan_warnings_.clear();
}
void TextureMods::SetNote(std::function<void(const std::string&)> note) { note_ = std::move(note); }
void TextureMods::SetObserver(std::function<void(uint64_t)> observer) { observer_ = std::move(observer); }

void TextureMods::NoteOnce(uint64_t key, const std::string& source) {
  std::lock_guard lock(mutex_);
  if (noted_.insert(key).second && note_) note_("replaced " + KeyText(key) + " (" + source + ")");
}

bool TextureMods::Active() const {
  return !config_.dump_dir.empty() || !replace_files_.empty() || bool(recipe_) || bool(observer_) || tag_all_ ||
         !tag_prefixes_.empty();
}

bool TextureMods::Tagged(uint64_t key) const {
  if (tag_all_) return true;
  if (tag_prefixes_.empty()) return false;
  const std::string text = KeyText(key);
  for (const auto& prefix : tag_prefixes_)
    if (text.compare(0, prefix.size(), prefix) == 0) return true;
  return false;
}

void TextureMods::WarnOnce(const std::string& id, const std::string& message) {
  std::lock_guard lock(mutex_);
  if (warned_.insert(id).second && warn_) warn_(message);
}

void TextureMods::Dump(uint64_t key, const guest::LinearTexture& texture) {
  {
    std::lock_guard lock(mutex_);
    if (!dumped_.insert(key).second) return;
  }
  Image image;
  if (!BaseLevelToImage(texture, image)) {
    const int format = static_cast<int>(texture.format);
    WarnOnce("dump format " + std::to_string(format),
             "textures in format " + std::to_string(format) + " cannot be dumped (first: " + KeyText(key) + ")");
    return;
  }
  std::error_code error;
  std::filesystem::create_directories(config_.dump_dir, error);
  if (error) return;
  const std::string name = KeyText(key);
  WritePng(config_.dump_dir / (name + ".png"), image);
  // The game keeps masks (hair, shine) in alpha, and the color under alpha 0
  // is still drawn. Image editors drop that color when they save, so textures
  // with alpha also get an opaque copy to paint and the alpha as gray.
  if (HasTransparency(image)) {
    Image color = image, alpha = image;
    for (size_t i = 0; i < image.rgba.size(); i += 4) {
      color.rgba[i + 3] = 255;
      alpha.rgba[i] = alpha.rgba[i + 1] = alpha.rgba[i + 2] = image.rgba[i + 3];
      alpha.rgba[i + 3] = 255;
    }
    WritePng(config_.dump_dir / (name + ".color.png"), color);
    WritePng(config_.dump_dir / (name + ".alpha.png"), alpha);
  }
  std::lock_guard lock(mutex_);
  std::ofstream index(config_.dump_dir / "index.csv", std::ios::app);
  if (!dump_index_started_) {
    dump_index_started_ = true;
    if (index.tellp() == 0) index << "key,width,height,format\n";
  }
  index << name << ',' << texture.width << ',' << texture.height << ',' << static_cast<int>(texture.format) << '\n';
}

// Read once per file: the decoded image, or null when the file is unusable
// (warned once). The size is checked from the header before decoding.
TextureMods::Loaded TextureMods::Load(const std::filesystem::path& path, uint32_t width, uint32_t height) {
  {
    std::lock_guard lock(mutex_);
    if (const auto found = loaded_.find(path); found != loaded_.end()) return found->second;
  }
  const std::string name = PathText(path.filename());
  Loaded result;
  uint32_t png_width = 0, png_height = 0;
  if (!PngSize(path, png_width, png_height)) {
    WarnOnce(PathText(path), "texture " + name + " could not be read; using the original");
  } else if (png_width != width || png_height != height) {
    WarnOnce(PathText(path), "texture " + name + " is " + std::to_string(png_width) + "x" +
                                 std::to_string(png_height) + ", the original is " + std::to_string(width) + "x" +
                                 std::to_string(height) + "; using the original");
  } else if (auto image = std::make_shared<Image>();
             ReadPng(path, *image, &result.has_alpha) && image->width == width && image->height == height) {
    result.image = std::move(image);
  } else {
    WarnOnce(PathText(path), "texture " + name + " could not be read; using the original");
  }
  std::lock_guard lock(mutex_);
  return loaded_.try_emplace(path, std::move(result)).first->second;
}

TextureMods::Replaced TextureMods::Replacement(uint64_t key, const Files& files, const guest::LinearTexture& texture) {
  {
    std::lock_guard lock(mutex_);
    if (const auto found = replacements_.find(key); found != replacements_.end()) return found->second;
  }
  const uint32_t width = texture.levels[0].width, height = texture.levels[0].height;
  Image original;
  const bool have_original = BaseLevelToImage(texture, original);
  const bool original_alpha = have_original && HasTransparency(original);
  Replaced result;
  const Loaded full = files.full.empty() ? Loaded{} : Load(files.full, width, height);
  if (full.image) {
    auto image = std::make_shared<Image>(*full.image);
    result.source = "png";
    if (original_alpha && !full.has_alpha) {
      // Saved without alpha: the game's mask stays.
      for (size_t i = 3; i < image->rgba.size(); i += 4) image->rgba[i] = original.rgba[i];
      result.source += ", the file has no alpha so the original alpha is kept";
    } else if (original_alpha) {
      // Editors write black under alpha 0, where the game still shows the color
      // (Superman's skin under the hair mask). Bring the original color back there.
      size_t restored = 0;
      for (size_t i = 0; i < image->rgba.size(); i += 4) {
        uint8_t* p = &image->rgba[i];
        const uint8_t* o = &original.rgba[i];
        if (p[3] == 0 && o[3] == 0 && !p[0] && !p[1] && !p[2] && (o[0] | o[1] | o[2])) {
          std::copy(o, o + 3, p);
          ++restored;
        }
      }
      if (restored)
        result.source += ", restored the original color of " + std::to_string(restored) +
                         " transparent pixels the editor turned black (use " + KeyText(key) + ".color.png to paint them)";
    }
    result.image = std::move(image);
  } else if (!files.color.empty() || !files.alpha.empty()) {
    // Over the original: color from <key>.color.png (its alpha ignored),
    // alpha from the gray of <key>.alpha.png.
    const Loaded color = files.color.empty() ? Loaded{} : Load(files.color, width, height);
    const Loaded alpha = files.alpha.empty() ? Loaded{} : Load(files.alpha, width, height);
    if (color.image || alpha.image) {
      auto image = std::make_shared<Image>();
      if (have_original) {
        *image = std::move(original);
      } else {
        image->width = width;
        image->height = height;
        image->rgba.assign(size_t(width) * height * 4, 255);
      }
      for (size_t i = 0; i < image->rgba.size(); i += 4) {
        if (color.image) std::copy(&color.image->rgba[i], &color.image->rgba[i] + 3, &image->rgba[i]);
        if (alpha.image) image->rgba[i + 3] = alpha.image->rgba[i];
      }
      result.source = color.image && alpha.image ? "color.png + alpha.png" : color.image ? "color.png" : "alpha.png";
      result.image = std::move(image);
    }
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
  if (Tagged(key)) {
    Image image;
    if (!BaseLevelToImage(texture, image)) {
      image.width = texture.levels[0].width;
      image.height = texture.levels[0].height;
      image.rgba.assign(size_t(image.width) * image.height * 4, 255);
    }
    PaintTag(key, image);
    ImageToTexture(image, texture);
    NoteOnce(key, "tag");
    return true;
  }
  if (const auto files = replace_files_.find(key); files != replace_files_.end()) {
    const Replaced replaced = Replacement(key, files->second, texture);
    if (replaced.image) {
      ImageToTexture(*replaced.image, texture);
      NoteOnce(key, replaced.source);
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
