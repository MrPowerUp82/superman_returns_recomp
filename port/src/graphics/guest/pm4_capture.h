#pragma once
#include "captured_batch.h"
#include <functional>
namespace superman_returns::native {
class Pm4Mirror;
}

namespace superman_returns::graphics::guest {
using GuestMemoryReader =
    std::function<std::span<const uint8_t>(uint32_t, uint32_t)>;
// Runs the existing PM4 interpreter on the frontend solely to capture its
// memory dependencies. No register state from this temporary scan is consumed.
// The caller sets command range_count after adding its remaining captures.
bool CapturePm4Dependencies(WorkBatch &, const WorkCmd &,
                            const GuestMemoryReader &, std::string &error,
                            native::Pm4Mirror *mirror = nullptr);
} // namespace superman_returns::graphics::guest
