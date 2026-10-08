#pragma once

#include <cstdint>
#include <map>
#include <vector>
#include <array>
#include <string>
#include <memory>
#include "pm4_mirror.h"
#include "texture_content.h"
#include "../graphics/guest/texture_capture.h"

namespace superman_returns::native {

struct CapturedTextureEntry {
  std::shared_ptr<const graphics::guest::TextureCapture> snapshot;
  uint64_t content_hash=0, checked_frame=~0ull;
  uint32_t watch_seq=0;
  CleanTextureWatchScan watch_scan;
  uint64_t next_check_frame=0;
  uint32_t stable_checks=0;
  uint64_t failed_frame=~0ull;
  std::string failure;
};

struct RingConstants {
  bool pixel;
  uint32_t start, count, ring_ptr;
};

struct NativeFrontend {
  Pm4Mirror mirror_;
  Pm4Mirror capture_mirror_;
  uint64_t capture_serial_ = 0;
  bool capture_tiling_active_ = false;
  std::map<std::array<uint32_t,6>, CapturedTextureEntry> captured_textures_;
  std::vector<uint32_t> mirror_snapshot_;
  uint32_t ring_last_ = 0;
  uint64_t ring_resyncs_ = 0;
  std::vector<RingConstants> pending_ring_constants_;
  uint64_t swap_number_ = 0;
  uint32_t pass_draw_index_[32] = {};
  uint32_t trace_draws_ = 0;
};

} // namespace superman_returns::native
