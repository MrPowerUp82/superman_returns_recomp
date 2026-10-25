#pragma once
#include <cstdint>
namespace superman_returns::native {
// Isolated yields stay immediate. Only a thread repeatedly polling without
// a millisecond of intervening work requests a short host wait.
class IdleYieldBackoff {
public:
  bool Observe(uint64_t now_ns) {
    if(!have_time_ || now_ns<last_ns_ || now_ns-last_ns_>=1000000) calls_=0;
    have_time_=true;last_ns_=now_ns;
    if(calls_<8) ++calls_;
    return calls_==8;
  }
private:
  uint64_t last_ns_=0;
  uint32_t calls_=0;
  bool have_time_=false;
};

// Retain the intended vblank phase under ordinary scheduling jitter. If the
// guest interrupt takes longer than a vblank, skip missed deadlines rather
// than dispatching a burst of catch-up interrupts.
class VblankPacer {
public:
  VblankPacer(uint64_t start, uint64_t interval)
      : interval_(interval ? interval : 1), deadline_(start + interval_) {}
  uint64_t Remaining(uint64_t now) const {
    return now < deadline_ ? deadline_ - now : 0;
  }
  void Complete(uint64_t now) {
    deadline_ += interval_;
    if (now >= deadline_) deadline_ = now + interval_;
  }
private:
  uint64_t interval_;
  uint64_t deadline_;
};
}
