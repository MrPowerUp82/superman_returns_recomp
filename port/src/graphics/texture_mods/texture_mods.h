#pragma once
// Texture dump and replacement (PNG files and in-code recipes), applied to
// every decoded guest texture by both native backends.
#include "texture_image.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace superman_returns::graphics::texture_mods {

struct ModConfig {
  std::filesystem::path dump_dir;     // empty = no dump
  // Scanned once, subfolders included, for <key>.png, <key>.color.png and
  // <key>.alpha.png (key = 16 hex digits).
  std::filesystem::path replace_dir;
  bool replace = true;
  // Tag mode: paints every matching texture with a solid color and its key
  // written on it, to find a texture's key by looking at the game. "" = off,
  // "all", or comma-separated key prefixes ("81E9,7D58BD0F").
  std::string tag;
};

// The color a tagged texture is painted with (RGB).
void TagColor(uint64_t key, uint8_t rgb[3]);
// Paints `image` as tag mode does; alpha stays as it was.
void PaintTag(uint64_t key, Image& image);

class TextureMods {
 public:
  explicit TextureMods(ModConfig config);
  // True when the texture content was replaced (PNG or recipe). Only plain 2D
  // textures (one slice) are dumped or replaced; others pass untouched. With
  // nothing active it returns at once, without hashing.
  bool Apply(guest::LinearTexture& texture);
  // In-code repaint by key; a user PNG for the same key wins. `wants` picks the
  // keys worth converting to an Image first (empty = every texture).
  void SetRecipe(std::function<bool(uint64_t key, Image&)> recipe,
                 std::function<bool(uint64_t key)> wants = {});
  void SetWarn(std::function<void(const std::string&)> warn);
  // Once per replaced key: which key and whether a PNG or the recipe did it.
  void SetNote(std::function<void(const std::string&)> note);
  // Sees the key of every plain 2D texture decoded (turns hashing on by itself).
  void SetObserver(std::function<void(uint64_t key)> observer);
  uint64_t hashes_computed() const { return hashes_.load(std::memory_order_relaxed); }

 private:
  // The files found for one key (empty path = none).
  struct Files {
    std::filesystem::path full, color, alpha;
  };
  struct Loaded {
    std::shared_ptr<const Image> image;  // null = unusable file
    bool has_alpha = true;
  };
  bool Active() const;
  bool Tagged(uint64_t key) const;
  void Dump(uint64_t key, const guest::LinearTexture& texture);
  void WarnOnce(const std::string& id, const std::string& message);
  void NoteOnce(uint64_t key, const std::string& source);
  Loaded Load(const std::filesystem::path& path, uint32_t width, uint32_t height);
  struct Replaced {
    std::shared_ptr<const Image> image;  // null = no usable file
    std::string source;                  // what the log says replaced it
  };
  Replaced Replacement(uint64_t key, const Files& files, const guest::LinearTexture& texture);
  ModConfig config_;
  std::map<uint64_t, Files> replace_files_;
  std::vector<std::string> tag_prefixes_;  // upper-case hex; empty with tag_all_ = off
  bool tag_all_ = false;
  std::vector<std::string> scan_warnings_;  // reported when SetWarn is called
  std::function<bool(uint64_t, Image&)> recipe_;
  std::function<bool(uint64_t)> recipe_wants_;
  std::function<void(const std::string&)> warn_, note_;
  std::function<void(uint64_t)> observer_;
  std::mutex mutex_;  // dump state, warnings; backends decode on several threads
  std::set<uint64_t> dumped_, noted_;
  std::set<std::string> warned_;
  std::map<std::filesystem::path, Loaded> loaded_;                 // per file
  std::map<uint64_t, Replaced> replacements_;  // per key (the key is a content hash)
  bool dump_index_started_ = false;
  std::atomic<uint64_t> hashes_{0};
};

}  // namespace superman_returns::graphics::texture_mods
