#pragma once

// Shared, API-independent input contract for temporal upscalers and denoisers.
// Observing a surface is not the same as supplying usable motion vectors or
// depth: a consumer must check the blocker before touching any GPU resource.
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace superman_returns::graphics {

enum class TemporalBlocker : uint8_t {
  kNone,
  kCamera,
  kPreviousCamera,
  kSceneColor,
  kSceneDepth,
  kDepthConvention,
  kMotionVectors,
  kObjectMotion,
  kJitter,
  kHudBoundary,
};

inline const char* TemporalBlockerName(TemporalBlocker reason) {
  switch (reason) {
    case TemporalBlocker::kNone: return "ready";
    case TemporalBlocker::kCamera: return "camera_missing";
    case TemporalBlocker::kPreviousCamera: return "previous_camera_missing";
    case TemporalBlocker::kSceneColor: return "scene_color_missing";
    case TemporalBlocker::kSceneDepth: return "scene_depth_missing";
    case TemporalBlocker::kDepthConvention: return "depth_convention_unverified";
    case TemporalBlocker::kMotionVectors: return "motion_vectors_missing";
    case TemporalBlocker::kObjectMotion: return "object_motion_missing";
    case TemporalBlocker::kJitter: return "jitter_missing";
    case TemporalBlocker::kHudBoundary: return "hud_boundary_missing";
  }
  return "unknown";
}

struct TemporalFrame {
  uint64_t frame = 0;
  uint32_t width = 0, height = 0;
  uint64_t scene_color = 0, scene_depth = 0;
  std::array<float, 16> inv_view_projection{};
  std::array<float, 16> previous_inv_view_projection{};
  float jitter_x = 0, jitter_y = 0;
  bool camera_valid = false, previous_camera_valid = false;
  bool depth_convention_valid = false;
  bool has_motion_vectors = false, has_object_motion = false;
  bool jitter_valid = false, hud_boundary_valid = false;
};

class TemporalFrameState {
 public:
  void Begin(uint64_t frame, uint32_t width, uint32_t height) {
    TemporalFrame next{};
    next.frame = frame;
    next.width = width;
    next.height = height;
    if (has_history_ && frame == previous_frame_ + 1 && width == previous_width_ &&
        height == previous_height_ && width && height) {
      next.previous_inv_view_projection = previous_camera_;
      next.previous_camera_valid = true;
    }
    current_ = next;
  }

  bool CaptureCamera(std::span<const float, 16> inverse_view_projection) {
    for (float value : inverse_view_projection) {
      if (!std::isfinite(value)) return false;
    }
    for (size_t i = 0; i < 16; ++i) current_.inv_view_projection[i] = inverse_view_projection[i];
    current_.camera_valid = true;
    return true;
  }

  void ObserveScene(uint64_t color_resource, uint64_t depth_resource) {
    current_.scene_color = color_resource;
    current_.scene_depth = depth_resource;
  }
  void ObserveDepthConvention(bool verified) { current_.depth_convention_valid = verified; }
  void ObserveMotionVectors(bool has_vectors, bool covers_objects) {
    current_.has_motion_vectors = has_vectors;
    current_.has_object_motion = has_vectors && covers_objects;
  }
  void ObserveJitter(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    current_.jitter_x = x;
    current_.jitter_y = y;
    current_.jitter_valid = true;
  }
  void ObserveHudBoundary() { current_.hud_boundary_valid = true; }
  void CutCamera() { has_history_ = false; current_.previous_camera_valid = false; }

  TemporalBlocker DlssBlocker() const {
    if (!current_.camera_valid) return TemporalBlocker::kCamera;
    if (!current_.previous_camera_valid) return TemporalBlocker::kPreviousCamera;
    if (!current_.scene_color) return TemporalBlocker::kSceneColor;
    if (!current_.scene_depth) return TemporalBlocker::kSceneDepth;
    if (!current_.depth_convention_valid) return TemporalBlocker::kDepthConvention;
    if (!current_.has_motion_vectors) return TemporalBlocker::kMotionVectors;
    if (!current_.has_object_motion) return TemporalBlocker::kObjectMotion;
    if (!current_.jitter_valid) return TemporalBlocker::kJitter;
    if (!current_.hud_boundary_valid) return TemporalBlocker::kHudBoundary;
    return TemporalBlocker::kNone;
  }

  void End() {
    previous_frame_ = current_.frame;
    previous_width_ = current_.width;
    previous_height_ = current_.height;
    has_history_ = current_.camera_valid && previous_width_ && previous_height_;
    if (has_history_) previous_camera_ = current_.inv_view_projection;
  }

  const TemporalFrame& current() const { return current_; }

 private:
  TemporalFrame current_{};
  std::array<float, 16> previous_camera_{};
  uint64_t previous_frame_ = 0;
  uint32_t previous_width_ = 0, previous_height_ = 0;
  bool has_history_ = false;
};

}  // namespace superman_returns::graphics
