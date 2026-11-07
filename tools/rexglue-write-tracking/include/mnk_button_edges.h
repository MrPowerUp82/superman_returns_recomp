#pragma once
// Preserve digital button presses between guest polls. Resolve key bindings
// before calling ObserveButtonEdges, while the actual chord is still held.
#include <cstdint>

namespace sr::mnk {
inline void ObserveButtonEdges(uint16_t& pending, uint16_t resolved, bool accepting) {
  pending = accepting ? static_cast<uint16_t>(pending | resolved) : 0;
}

inline uint16_t ConsumeButtonEdges(uint16_t& pending, uint16_t current, bool accepting) {
  const uint16_t result = accepting ? static_cast<uint16_t>(current | pending) : 0;
  pending = 0;
  return result;
}
}  // namespace sr::mnk
