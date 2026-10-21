#pragma once
#include "parallel_replay.h"
#include <iosfwd>
namespace superman_returns::graphics::guest {
struct ReplayBatchSample {
  uint64_t sequence=0,closing_swap=0;
  size_t commands=0,draws=0;
  ReplayOptions options;
  uint64_t direct_decode_ns=0,direct_arena_ns=0;
  ReplayBatchProbe prepared;
};
class ReplayTraceWriter {
public:
  ReplayTraceWriter(std::ostream&,uint64_t clock_anchor_ns,int64_t clock_anchor_utc_ns);
  bool Write(const ReplayBatchSample&);
  bool good() const;
private:
  std::ostream& output_;
  uint64_t clock_anchor_ns_;
  int64_t clock_anchor_utc_ns_;
};
} // namespace superman_returns::graphics::guest
