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

namespace superman_returns::graphics::texture_mods {

struct ModConfig {
  std::filesystem::path dump_dir;     // empty = no dump
  std::filesystem::path replace_dir;  // scanned once for <16 hex>.png
  bool replace = true;
};

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
  bool Active() const;
  void Dump(uint64_t key, const guest::LinearTexture& texture);
  void WarnOnce(uint64_t key, const std::string& message);
  void NoteOnce(uint64_t key, const char* source);
  std::shared_ptr<const Image> Replacement(uint64_t key, uint32_t width, uint32_t height);
  ModConfig config_;
  std::set<uint64_t> replace_keys_;
  std::function<bool(uint64_t, Image&)> recipe_;
  std::function<bool(uint64_t)> recipe_wants_;
  std::function<void(const std::string&)> warn_, note_;
  std::function<void(uint64_t)> observer_;
  std::mutex mutex_;  // dump state, warnings; backends decode on several threads
  std::set<uint64_t> dumped_, warned_, noted_;
  std::map<uint64_t, std::shared_ptr<const Image>> replacements_;  // null = unusable file
  bool dump_index_started_ = false;
  std::atomic<uint64_t> hashes_{0};
};

}  // namespace superman_returns::graphics::texture_mods
