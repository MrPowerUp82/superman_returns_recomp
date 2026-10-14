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
}
