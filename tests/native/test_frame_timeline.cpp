#include "../graphics/frame_timeline.h"
#include "test_main.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
using namespace superman_returns::graphics;

namespace {
std::string Rows(const FrameTimeline& timeline, uint64_t first, uint64_t last) {
  std::ostringstream out;
  timeline.WriteRows(out, first, last);
  return out.str();
}
std::string ReadAll(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}
size_t CountOf(const std::string& text, const std::string& needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++count;
  return count;
}
}  // namespace

SR_TEST(timeline_span_busy_excludes_blocked_time) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordSpan(TimelineStage::kWorker, 5, 1000, 4000, 1000);
  SR_CHECK(Rows(timeline, 5, 5) == "5,worker,1000,4000,2000,1000\n");
}

SR_TEST(timeline_blocked_longer_than_span_gives_zero_busy) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordSpan(TimelineStage::kRecord, 3, 100, 200, 500);
  SR_CHECK(Rows(timeline, 3, 3) == "3,record,100,200,0,500\n");
}

SR_TEST(timeline_gpu_rows_carry_only_busy_time) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordBusy(TimelineStage::kGpu, 7, 5000000);
  SR_CHECK(Rows(timeline, 7, 7) == "7,gpu,0,0,5000000,0\n");
}

SR_TEST(timeline_disabled_records_nothing) {
  FrameTimeline timeline("");
  SR_CHECK(!timeline.enabled());
  timeline.RecordSpan(TimelineStage::kGame, 1, 0, 10, 0);
  SR_CHECK(Rows(timeline, 1, 1).empty());
}

SR_TEST(timeline_frame_zero_is_not_a_frame) {
  FrameTimeline timeline("unused.csv");
  timeline.RecordBusy(TimelineStage::kGame, 0, 10);
  SR_CHECK(Rows(timeline, 0, 0).empty());
}

SR_TEST(timeline_ring_slot_reuse_hides_the_older_frame) {
  FrameTimeline timeline("unused.csv");
  const uint64_t later = 1 + FrameTimeline::kRing;
  timeline.RecordBusy(TimelineStage::kGame, 1, 10);
  timeline.RecordBusy(TimelineStage::kGame, later, 20);
  SR_CHECK(Rows(timeline, 1, 1).empty());
  SR_CHECK(Rows(timeline, later, later) == std::to_string(later) + ",game,0,0,20,0\n");
}

SR_TEST(timeline_flush_writes_header_then_only_settled_frames) {
  const auto path = std::filesystem::temp_directory_path() / "sr_frame_timeline_flush.csv";
  std::filesystem::remove(path);
  FrameTimeline timeline(path.string());
  for (uint64_t frame = 1; frame <= 40; ++frame) timeline.RecordBusy(TimelineStage::kGame, frame, frame);
  timeline.Flush(40);  // settled frames: <= 40 - 16 = 24
  timeline.Flush(40);  // nothing new may be written twice
  std::string text = ReadAll(path);
  SR_CHECK(text.rfind("frame,stage,begin_ns,end_ns,busy_ns,blocked_ns\n", 0) == 0);
  SR_CHECK(text.find("\n24,game,0,0,24,0\n") != std::string::npos);
  SR_CHECK(text.find("\n25,game") == std::string::npos);
  timeline.Flush(50);  // frames 25..34
  text = ReadAll(path);
  SR_CHECK(text.find("\n34,game,0,0,34,0\n") != std::string::npos);
  SR_CHECK(text.find("\n35,game") == std::string::npos);
  SR_CHECK_EQ(CountOf(text, "\n24,game"), 1u);
  std::filesystem::remove(path);
}

SR_TEST(timeline_flush_all_writes_every_recorded_frame_once) {
  const auto path = std::filesystem::temp_directory_path() / "sr_frame_timeline_all.csv";
  std::filesystem::remove(path);
  FrameTimeline timeline(path.string());
  for (uint64_t frame = 1; frame <= 5; ++frame) timeline.RecordBusy(TimelineStage::kGpu, frame, 100 + frame);
  timeline.FlushAll();
  timeline.FlushAll();
  const std::string text = ReadAll(path);
  SR_CHECK(text.find("\n1,gpu,0,0,101,0\n") != std::string::npos);
  SR_CHECK(text.find("\n5,gpu,0,0,105,0\n") != std::string::npos);
  SR_CHECK_EQ(CountOf(text, ",gpu,"), 5u);
  std::filesystem::remove(path);
}

SR_TEST(timeline_block_scope_accumulates_wait_only_when_enabled) {
  FrameTimeline on("unused.csv"), off("");
  TakeTimelineBlockedNs();
  {
    TimelineBlockScope scope(off);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  SR_CHECK_EQ(TakeTimelineBlockedNs(), 0u);
  {
    TimelineBlockScope scope(on);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  SR_CHECK(TakeTimelineBlockedNs() >= 1000000u);
  SR_CHECK_EQ(TakeTimelineBlockedNs(), 0u);
}

SR_TEST(timeline_recording_a_frame_costs_microseconds) {
  // Budget from the design: < 0.3 ms per frame. 100k frames of every stage must
  // take far less than 100k * 0.3 ms; the bound only catches a pathological regression.
  FrameTimeline timeline("unused.csv");
  const auto start = std::chrono::steady_clock::now();
  for (uint64_t frame = 1; frame <= 100000; ++frame)
    for (size_t stage = 0; stage < size_t(TimelineStage::kCount); ++stage)
      timeline.RecordSpan(TimelineStage(stage), frame, frame, frame + 10, 1);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  SR_CHECK(elapsed.count() < 500);
}
