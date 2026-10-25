#pragma once

// Diagnostics for the VS gViewProjectionXform candidate found in the shader
// catalog. It deliberately does not classify a matrix as a camera, infer its
// handedness/layout, or supply it to a temporal consumer before verification.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace superman_returns::graphics {

struct CameraCandidateFrame {
  uint64_t frame = 0;
  uint32_t draws = 0, valid = 0, rejected = 0, varying_draws = 0;
  bool has_previous = false;
  float max_within_frame_delta = 0.0f;
  float max_previous_frame_delta = 0.0f;
  std::array<float, 16> first{}, last{};
};

class CameraCandidateProbe {
 public:
  // Reflected as gViewProjectionXform at VS register c0 in the city pass.
  static constexpr uint64_t kViewProjectionShader = 0x2C45BFFE76D77398ull;

  void Begin(uint64_t frame) {
    current_ = {};
    current_.frame = frame;
  }

  bool Observe(uint64_t shader, std::span<const float, 16> values) {
    if (shader != kViewProjectionShader) return false;
    ++current_.draws;
    std::array<float, 16> matrix{};
    std::copy(values.begin(), values.end(), matrix.begin());
    if (!Invertible(matrix)) {
      ++current_.rejected;
      return false;
    }
    if (current_.valid == 0) {
      current_.first = matrix;
      if (previous_valid_ && previous_frame_ + 1 == current_.frame) {
        current_.has_previous = true;
        current_.max_previous_frame_delta = Delta(previous_, matrix);
      }
    } else {
      float delta = Delta(current_.first, matrix);
      current_.max_within_frame_delta = std::max(current_.max_within_frame_delta, delta);
      if (delta > 1e-4f) ++current_.varying_draws;
    }
    ++current_.valid;
    current_.last = matrix;
    return true;
  }

  void End() {
    previous_valid_ = current_.valid > 0;
    previous_frame_ = current_.frame;
    if (previous_valid_) previous_ = current_.first;
  }

  const CameraCandidateFrame& frame() const { return current_; }

 private:
  static float Delta(const std::array<float, 16>& a, const std::array<float, 16>& b) {
    float result = 0;
    for (size_t i = 0; i < a.size(); ++i) result = std::max(result, std::abs(a[i] - b[i]));
    return result;
  }

  static bool Invertible(const std::array<float, 16>& matrix) {
    // Matrix and its transpose have the same determinant; this only rejects
    // clearly unusable data, without claiming a row/column convention.
    double m[4][4];
    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 4; ++col) {
        const float v = matrix[row * 4 + col];
        if (!std::isfinite(v)) return false;
        m[row][col] = v;
      }
    }
    for (int col = 0; col < 4; ++col) {
      int pivot = col;
      for (int row = col + 1; row < 4; ++row) {
        if (std::abs(m[row][col]) > std::abs(m[pivot][col])) pivot = row;
      }
      if (std::abs(m[pivot][col]) < 1e-20) return false;
      if (pivot != col) {
        for (int k = col; k < 4; ++k) std::swap(m[pivot][k], m[col][k]);
      }
      for (int row = col + 1; row < 4; ++row) {
        double factor = m[row][col] / m[col][col];
        for (int k = col + 1; k < 4; ++k) m[row][k] -= factor * m[col][k];
      }
    }
    return true;
  }

  CameraCandidateFrame current_{};
  std::array<float, 16> previous_{};
  uint64_t previous_frame_ = 0;
  bool previous_valid_ = false;
};

}  // namespace superman_returns::graphics
