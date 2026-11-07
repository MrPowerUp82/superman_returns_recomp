#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <ostream>
#include <string_view>
#include <thread>

namespace superman_returns::graphics {
inline uint32_t ParsePresentationTarget(std::string_view text) {
  uint32_t hz=0;
  const auto parsed=std::from_chars(text.data(),text.data()+text.size(),hz);
  return parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size()?std::min(hz,1000u):0;
}
// All times are monotonic host nanoseconds. A rational period avoids the
// cumulative truncation of 1e9/60; missed slots keep the original phase.
class PresentationDeadline {
 public:
  void Configure(uint32_t hz,uint64_t now) {
    hz=std::min(hz,1000u);
    if(hz_==hz && initialized_) return;
    hz_=hz;deadline_=epoch_=now;slot_=0;skipped_=0;initialized_=true;
    oversleep_={};samples_=cursor_=0;
  }
  uint64_t Remaining(uint64_t now) const {return hz_ && now<deadline_?deadline_-now:0;}
  void Complete(uint64_t now) {
    if(!hz_) return;
    ++slot_;
    // Rebase only after a backwards clock (steady_clock must not do this).
    if(now<epoch_) {epoch_=now;slot_=1;}
    const uint64_t elapsed=now-epoch_;
    const uint64_t first_future=(elapsed/1000000000ull)*hz_+
        ((elapsed%1000000000ull)*hz_)/1000000000ull+1;
    if(first_future>slot_) {skipped_+=first_future-slot_;slot_=first_future;}
    deadline_=epoch_+(slot_/hz_)*1000000000ull+
        ((slot_%hz_)*1000000000ull)/hz_;
    // Flooring a rational deadline can coincide with `now`.
    if(deadline_<=now) {++slot_;++skipped_;deadline_=epoch_+(slot_/hz_)*1000000000ull+
        ((slot_%hz_)*1000000000ull)/hz_;}
  }
  void ObserveSleep(uint64_t expected,uint64_t actual) {
    oversleep_[cursor_]=actual>expected?std::min(actual-expected,uint64_t(1000000)):0;
    cursor_=(cursor_+1)%oversleep_.size();samples_=std::min(samples_+1,oversleep_.size());
  }
  uint64_t WakeMargin() const {
    if(!samples_) return 100000;
    auto sorted=oversleep_;std::sort(sorted.begin(),sorted.begin()+samples_);
    return std::clamp(sorted[(samples_-1)*95/100]+50000,uint64_t(100000),uint64_t(1000000));
  }
  uint64_t deadline() const {return deadline_;}
  uint64_t skipped() const {return skipped_;}
 private:
  uint32_t hz_=0;
  uint64_t epoch_=0,slot_=0,deadline_=0,skipped_=0;
  bool initialized_=false;
  std::array<uint64_t,32> oversleep_{};
  size_t samples_=0,cursor_=0;
};

inline bool PresentationLegacyMode() {
  static const bool legacy=[] {const char* mode=std::getenv("SR_PRESENTATION_LEGACY");return mode && std::string_view(mode)=="1";}();
  return legacy;
}
inline uint64_t PresentationNowNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

class PresentationCsv {
 public:
  explicit PresentationCsv(std::ostream& stream):out_(stream) {
    out_<<"clock_domain,backend,call,source_serial,kind,entry_ns,return_ns,api_ns,accepted_interval_ns,result,accepted,skipped_slots\n";
  }
  void Record(const char* backend,uint64_t source,uint64_t skipped,const char* kind,
              uint64_t begin,uint64_t end,int64_t result,bool accepted) {
    const uint64_t interval=have_accepted_ && end>=last_accepted_?end-last_accepted_:0;
    out_<<"steady_cpu_api_return,"<<backend<<','<<++calls_<<','<<source<<','<<kind<<','
        <<begin<<','<<end<<','<<(end>=begin?end-begin:0)<<','<<interval<<','<<result<<','
        <<(accepted?1:0)<<','<<skipped<<'\n';
    if(accepted) {last_accepted_=end;have_accepted_=true;}
    // The title benchmark exits with std::_Exit: destructors never run.
    // Persist every diagnostic row; the sink is opt-in and outside GPU locks.
    out_.flush();
  }
 private:
  std::ostream& out_;
  uint64_t calls_=0,last_accepted_=0;
  bool have_accepted_=false;
};

// One instance belongs to one existing presenter thread. The environment path
// is opt-in and each benchmark run must choose a distinct output file.
class PresentationTiming {
 public:
  PresentationTiming() {
    const char* path=std::getenv("SR_PRESENTATION_CSV");
    if(path && *path) {file_.open(path,std::ios::out|std::ios::trunc);if(file_) csv_=std::make_unique<PresentationCsv>(file_);}
  }
  void Wait(uint32_t hz) {
    if(PresentationLegacyMode()) return;
    uint64_t now=PresentationNowNs();deadline_.Configure(hz,now);
    for(uint64_t remaining=deadline_.Remaining(now);remaining;remaining=deadline_.Remaining(now)) {
      const uint64_t margin=deadline_.WakeMargin();
      if(remaining>margin+100000) {
        const uint64_t expected=now+remaining-margin;
        std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(expected)));
        now=PresentationNowNs();deadline_.ObserveSleep(expected,now);
      } else {std::this_thread::yield();now=PresentationNowNs();}
    }
    // Advance at paint start, allowing recording/Present to consume this slot.
    // A blocked FIFO/interval-1 Present skips stale slots at the next call.
    deadline_.Complete(now);
  }
  void Record(const char* backend,uint64_t source,const char* kind,uint64_t begin,
              uint64_t end,int64_t result,bool accepted) {
    if(csv_) csv_->Record(backend,source,deadline_.skipped(),kind,begin,end,result,accepted);
  }
 private:
  PresentationDeadline deadline_;
  std::ofstream file_;
  std::unique_ptr<PresentationCsv> csv_;
};
inline PresentationTiming& ThreadPresentationTiming() {
  static thread_local PresentationTiming timing;
  return timing;
}
} // namespace superman_returns::graphics
