#pragma once

// Bounded, allocation-free record of RT/depth binds seen by native draws.
// This deliberately captures observations, not semantic game-pass labels:
// the game's pass IDs are still unverified.
#include <array>
#include <cstddef>
#include <cstdint>

namespace superman_returns::graphics {

struct SurfaceSignature {
  uint32_t color_edram = 0;
  uint32_t color_width = 0, color_height = 0, color_format = 0;
  uint32_t depth_edram = 0;
  uint32_t depth_width = 0, depth_height = 0;

  constexpr bool operator==(const SurfaceSignature&) const = default;
};

// Raw draw state for identifying which guest shaders use each observed surface.
// These fields do not establish a semantic pass, camera, or HUD boundary.
struct SurfaceDrawMetadata {
  uint64_t vs_hash = 0, ps_hash = 0;
  uint32_t guest_pass = 0, primitive = 0;
  uint32_t depth_control = 0, color_mask = 0;
};

struct SurfaceDrawSample {
  uint32_t draw_offset = 0;
  SurfaceDrawMetadata draw{};
};

struct SurfaceSegment {
  // Sample the first draw then exponentially spaced offsets through 4096 draws.
  static constexpr size_t kMaxSamples = 13;
  SurfaceSignature surface{};
  uint32_t first_draw = 0;
  uint32_t draws = 0;
  uint32_t sample_count = 0;
  std::array<SurfaceDrawSample, kMaxSamples> samples{};
};

template <size_t Capacity>
class SurfaceSequence {
  static_assert(Capacity > 0);
 public:
  void Reset() {
    size_ = 0;
    total_draws_ = 0;
    dropped_segments_ = 0;
    last_valid_ = false;
  }

  void ObserveDraw(SurfaceSignature signature) { ObserveDraw(signature, {}); }

  void ObserveDraw(SurfaceSignature signature, SurfaceDrawMetadata metadata) {
    const uint32_t draw_index = total_draws_++;
    if (last_valid_ && signature == last_) {
      if (size_ && segments_[size_ - 1].surface == signature &&
          segments_[size_ - 1].first_draw + segments_[size_ - 1].draws == draw_index) {
        auto& segment = segments_[size_ - 1];
        const uint32_t offset = segment.draws++;
        if ((offset & (offset - 1)) == 0 &&
            segment.sample_count < SurfaceSegment::kMaxSamples) {
          segment.samples[segment.sample_count++] = {offset, metadata};
        }
      }
      return;
    }
    last_valid_ = true;
    last_ = signature;
    if (size_ < Capacity) {
      auto& segment = segments_[size_++];
      segment = SurfaceSegment{};
      segment.surface = signature;
      segment.first_draw = draw_index;
      segment.draws = 1;
      segment.samples[0] = {0, metadata};
      segment.sample_count = 1;
    } else {
      ++dropped_segments_;
    }
  }

  size_t size() const { return size_; }
  uint32_t total_draws() const { return total_draws_; }
  uint32_t dropped_segments() const { return dropped_segments_; }
  const SurfaceSegment& operator[](size_t index) const { return segments_[index]; }

 private:
  std::array<SurfaceSegment, Capacity> segments_{};
  SurfaceSignature last_{};
  size_t size_ = 0;
  uint32_t total_draws_ = 0;
  uint32_t dropped_segments_ = 0;
  bool last_valid_ = false;
};

}  // namespace superman_returns::graphics
