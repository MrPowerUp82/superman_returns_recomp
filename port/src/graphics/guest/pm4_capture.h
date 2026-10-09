#pragma once
#include "captured_batch.h"
#include <cstdint>
#include <functional>
namespace superman_returns::native {
class Pm4Mirror;
}

namespace superman_returns::graphics::guest {
using GuestMemoryReader =
    std::function<std::span<const uint8_t>(uint32_t, uint32_t)>;
// Optional nanosecond counters filled by CapturePm4Dependencies (diagnostics only).
struct Pm4CaptureProbe {
  uint64_t primary_ns = 0, read_ns = 0, copy_ns = 0, reads = 0, bytes = 0;
};
// Runs the existing PM4 interpreter on the frontend solely to capture its
// memory dependencies. No register state from this temporary scan is consumed.
// The caller sets command range_count after adding its remaining captures.
bool CapturePm4Dependencies(WorkBatch &, const WorkCmd &,
                            const GuestMemoryReader &, std::string &error,
                            native::Pm4Mirror *mirror = nullptr,
                            Pm4CaptureProbe *probe = nullptr);
} // namespace superman_returns::graphics::guest
