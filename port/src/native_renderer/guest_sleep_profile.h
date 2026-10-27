#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace superman_returns::native {
// Fixed storage for the opt-in sleep diagnostic. Slot zero is reserved for
// the conditional movie-path Sleep(33), even when other callers fill the table.
// The runtime owns synchronization; these totals may span multiple threads.
struct GuestSleepCounter {
  uint32_t caller=0,requested_ms=0;
  uint64_t calls=0,elapsed_ns=0,max_ns=0;
};
struct GuestSleepWindow {
  std::array<GuestSleepCounter,32> callers{{{0x824822C4,33}}};
  uint64_t calls=0,elapsed_ns=0,overflow_calls=0,overflow_ns=0;
  void Record(uint32_t caller,uint32_t requested_ms,uint64_t duration_ns) {
    ++calls;elapsed_ns+=duration_ns;
    for(auto& row:callers) {
      if((row.caller==caller && row.requested_ms==requested_ms) || row.caller==0) {
        row.caller=caller;row.requested_ms=requested_ms;
        ++row.calls;row.elapsed_ns+=duration_ns;row.max_ns=std::max(row.max_ns,duration_ns);
        return;
      }
    }
    ++overflow_calls;overflow_ns+=duration_ns;
  }
};
}
