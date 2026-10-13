#pragma once
// Per-pass GPU time aggregation shared by the D3D12 and Vulkan native renderers (SR_GPU_PASS_TIMING=1).
// A "pass" is a run of draws on the same render-target set; the key is API independent so the two
// backends can be compared: width x height x colour target count x has-depth.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace superman_returns::graphics {

struct GpuPassKey {
  uint32_t width = 0, height = 0, colors = 0;
  bool depth = false;
  bool operator==(const GpuPassKey&) const = default;
};
struct GpuPassKeyHash {
  size_t operator()(const GpuPassKey& key) const {
    size_t h = key.width;
    h = h * 1000003u ^ key.height;
    h = h * 1000003u ^ (key.colors * 2u + (key.depth ? 1u : 0u));
    return h;
  }
};
struct GpuPassSegment {
  GpuPassKey key;
  uint64_t ns = 0;
};

class GpuPassStats {
 public:
  // One frame: the pass segments, plus the frame's remaining GPU time (gaps between passes).
  void AddFrame(const std::vector<GpuPassSegment>& segments, uint64_t other_ns) {
    ++frames_;
    other_ns_ += other_ns;
    for (const auto& segment : segments) {
      Entry& entry = entries_[segment.key];
      entry.ns += segment.ns;
      ++entry.passes;
    }
  }
  uint64_t frames() const { return frames_; }
  void Reset() {
    entries_.clear();
    frames_ = 0;
    other_ns_ = 0;
  }
  // "gpu passes (per frame over N frames): 1280x720 c1 d1: 6.00 ms (1.5 passes) | ... | other 1.00 ms"
  std::string Format(size_t limit) const {
    if (!frames_) return "gpu passes: no frames";
    std::vector<std::pair<GpuPassKey, Entry>> sorted(entries_.begin(), entries_.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.ns > b.second.ns; });
    const double frames = double(frames_);
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "gpu passes (per frame over %llu frames):", (unsigned long long)frames_);
    std::string text = buffer;
    for (size_t i = 0; i < sorted.size() && i < limit; ++i) {
      const auto& [key, entry] = sorted[i];
      std::snprintf(buffer, sizeof(buffer), " %ux%u c%u d%d: %.2f ms (%.1f passes) |", key.width, key.height,
                    key.colors, key.depth ? 1 : 0, double(entry.ns) / 1e6 / frames, double(entry.passes) / frames);
      text += buffer;
    }
    if (sorted.size() > limit) {
      double rest = 0;
      for (size_t i = limit; i < sorted.size(); ++i) rest += double(sorted[i].second.ns);
      std::snprintf(buffer, sizeof(buffer), " %zu more groups: %.2f ms |", sorted.size() - limit, rest / 1e6 / frames);
      text += buffer;
    }
    std::snprintf(buffer, sizeof(buffer), " other %.2f ms", double(other_ns_) / 1e6 / frames);
    return text + buffer;
  }

 private:
  struct Entry {
    uint64_t ns = 0, passes = 0;
  };
  std::unordered_map<GpuPassKey, Entry, GpuPassKeyHash> entries_;
  uint64_t frames_ = 0, other_ns_ = 0;
};

}  // namespace superman_returns::graphics
