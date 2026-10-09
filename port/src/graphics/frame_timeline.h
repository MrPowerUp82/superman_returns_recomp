#pragma once
// Per-frame stage timeline shared by the D3D12 and Vulkan native renderers.
// SR_FRAME_TIMELINE=<file.csv> turns it on; without it every call returns at
// once. A frame is identified by the guest swap number, which every stage sees.
// Each stage writes only its own slot, so threads never share a field; the CSV
// is written from complete frames only (Flush keeps a margin behind the newest).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>

namespace superman_returns::graphics {

enum class TimelineStage : uint8_t {
  kGame, kCapture, kFrontWait, kWorker, kRecord, kGpu,
  // Front-end detail on the game thread (SR_FRAME_TIMELINE_DETAIL=1).
  kFrontend, kFeBegin, kFeRing, kFeDevice, kFeIndex, kFeStreams, kFeEnd, kFeFlush,
  kFdShaders, kFsPrep, kFsPlan, kFePush,
  // nest inside fe_end / fs_plan; listed, never summed
  kFePm4, kFeTextures, kFsResolve, kFsBuffer,
  kFbRefresh, kFbHash, kFpPrimary, kFpRead, kFpCopy,
  kGpuReal, kGpuIdle,
  kCount
};

inline const char* TimelineStageName(TimelineStage stage) {
  switch (stage) {
    case TimelineStage::kGame: return "game";
    case TimelineStage::kCapture: return "capture";
    case TimelineStage::kFrontWait: return "front_wait";
    case TimelineStage::kWorker: return "worker";
    case TimelineStage::kRecord: return "record";
    case TimelineStage::kGpu: return "gpu";
    case TimelineStage::kFrontend: return "frontend";
    case TimelineStage::kFeBegin: return "fe_begin";
    case TimelineStage::kFeRing: return "fe_ring";
    case TimelineStage::kFeDevice: return "fe_device";
    case TimelineStage::kFeIndex: return "fe_index";
    case TimelineStage::kFeStreams: return "fe_streams";
    case TimelineStage::kFeEnd: return "fe_end";
    case TimelineStage::kFeFlush: return "fe_flush";
    case TimelineStage::kFdShaders: return "fd_shaders";
    case TimelineStage::kFsPrep: return "fs_prep";
    case TimelineStage::kFsPlan: return "fs_plan";
    case TimelineStage::kFePush: return "fe_push";
    case TimelineStage::kFePm4: return "fe_pm4";
    case TimelineStage::kFeTextures: return "fe_textures";
    case TimelineStage::kFsResolve: return "fs_resolve";
    case TimelineStage::kFsBuffer: return "fs_buffer";
    case TimelineStage::kFbRefresh: return "fb_refresh";
    case TimelineStage::kFbHash: return "fb_hash";
    case TimelineStage::kFpPrimary: return "fp_primary";
    case TimelineStage::kFpRead: return "fp_read";
    case TimelineStage::kFpCopy: return "fp_copy";
    case TimelineStage::kGpuReal: return "gpu_real";
    case TimelineStage::kGpuIdle: return "gpu_idle";
    default: return "unknown";
  }
}

class FrameTimeline {
 public:
  static constexpr uint64_t kRing = 4096;
  static constexpr uint64_t kFlushMargin = 16;

  explicit FrameTimeline(std::string path) : path_(std::move(path)), enabled_(!path_.empty()) {
    if (enabled_) spans_ = std::make_unique<Span[]>(size_t(TimelineStage::kCount) * kRing);
  }
  FrameTimeline(const FrameTimeline&) = delete;
  FrameTimeline& operator=(const FrameTimeline&) = delete;

  static FrameTimeline& Global() {
    static FrameTimeline instance(PathFromEnvironment());
    return instance;
  }
  static std::string PathFromEnvironment() {
    const char* value = std::getenv("SR_FRAME_TIMELINE");
    if (!value || !*value || std::string(value) == "0") return {};
    return value;
  }
  // Fine-grained front-end timers add a few clock reads per draw, so they are
  // opt-in on top of SR_FRAME_TIMELINE.
  static bool Detail() {
    static const bool on = [] {
      if (!Global().enabled()) return false;
      const char* value = std::getenv("SR_FRAME_TIMELINE_DETAIL");
      return value && *value && std::string(value) != "0";
    }();
    return on;
  }

  bool enabled() const { return enabled_; }
  static uint64_t ToNs(std::chrono::steady_clock::time_point time) {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count());
  }
  static uint64_t NowNs() { return ToNs(std::chrono::steady_clock::now()); }

  void Record(TimelineStage stage, uint64_t frame, uint64_t begin_ns, uint64_t end_ns, uint64_t busy_ns,
              uint64_t blocked_ns) {
    if (!enabled_ || frame == 0) return;
    Span& span = Slot(size_t(stage), frame);
    span.begin_ns = begin_ns;
    span.end_ns = end_ns;
    span.busy_ns = busy_ns;
    span.blocked_ns = blocked_ns;
    span.frame.store(frame, std::memory_order_release);
    uint64_t seen = max_frame_.load(std::memory_order_relaxed);
    while (frame > seen && !max_frame_.compare_exchange_weak(seen, frame, std::memory_order_relaxed)) {}
  }
  void RecordSpan(TimelineStage stage, uint64_t frame, uint64_t begin_ns, uint64_t end_ns, uint64_t blocked_ns) {
    const uint64_t wall = end_ns > begin_ns ? end_ns - begin_ns : 0;
    Record(stage, frame, begin_ns, end_ns, wall > blocked_ns ? wall - blocked_ns : 0, blocked_ns);
  }
  void RecordBusy(TimelineStage stage, uint64_t frame, uint64_t busy_ns) { Record(stage, frame, 0, 0, busy_ns, 0); }

  void WriteRows(std::ostream& out, uint64_t first, uint64_t last) const {
    if (!enabled_) return;
    for (uint64_t frame = std::max<uint64_t>(first, 1); frame <= last; ++frame) {
      for (size_t stage = 0; stage < size_t(TimelineStage::kCount); ++stage) {
        const Span& span = Slot(stage, frame);
        if (span.frame.load(std::memory_order_acquire) != frame) continue;
        out << frame << ',' << TimelineStageName(TimelineStage(stage)) << ',' << span.begin_ns << ','
            << span.end_ns << ',' << span.busy_ns << ',' << span.blocked_ns << '\n';
      }
    }
  }

  // Writes every frame up to newest_frame - kFlushMargin that was not written yet.
  void Flush(uint64_t newest_frame) {
    if (!enabled_ || newest_frame <= kFlushMargin) return;
    FlushUpTo(newest_frame - kFlushMargin);
  }
  // Writes everything recorded so far (shutdown, tests).
  void FlushAll() {
    if (!enabled_) return;
    FlushUpTo(max_frame_.load(std::memory_order_relaxed));
  }

 private:
  struct Span {
    std::atomic<uint64_t> frame{0};  // written last; 0 = empty
    uint64_t begin_ns = 0, end_ns = 0, busy_ns = 0, blocked_ns = 0;
  };
  Span& Slot(size_t stage, uint64_t frame) const { return spans_[stage * kRing + frame % kRing]; }
  void FlushUpTo(uint64_t last) {
    std::lock_guard<std::mutex> lock(flush_mutex_);
    if (last <= flushed_) return;
    std::ofstream out(path_, std::ios::out | (header_written_ ? std::ios::app : std::ios::trunc));
    if (!out) return;
    if (!header_written_) {
      out << "frame,stage,begin_ns,end_ns,busy_ns,blocked_ns\n";
      header_written_ = true;
    }
    WriteRows(out, flushed_ + 1, last);
    flushed_ = last;
  }

  std::string path_;
  bool enabled_;
  std::unique_ptr<Span[]> spans_;
  std::atomic<uint64_t> max_frame_{0};
  std::mutex flush_mutex_;
  uint64_t flushed_ = 0;
  bool header_written_ = false;
};

// Time this thread spent waiting on another stage since the last Take. The
// stage that owns the thread reads it when it closes its span.
inline uint64_t& TimelineBlockedNs() {
  thread_local uint64_t value = 0;
  return value;
}
inline uint64_t TakeTimelineBlockedNs() {
  uint64_t& value = TimelineBlockedNs();
  const uint64_t taken = value;
  value = 0;
  return taken;
}
class TimelineBlockScope {
 public:
  explicit TimelineBlockScope(const FrameTimeline& timeline = FrameTimeline::Global())
      : active_(timeline.enabled()), start_(active_ ? FrameTimeline::NowNs() : 0) {}
  ~TimelineBlockScope() {
    if (active_) TimelineBlockedNs() += FrameTimeline::NowNs() - start_;
  }
  TimelineBlockScope(const TimelineBlockScope&) = delete;
  TimelineBlockScope& operator=(const TimelineBlockScope&) = delete;

 private:
  bool active_;
  uint64_t start_;
};

}  // namespace superman_returns::graphics
