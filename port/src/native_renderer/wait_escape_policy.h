#pragma once
#include <cstdint>

namespace superman_returns::native {
// The game's present throttle (sub_82111EC8) makes the GPU raise scratch+4
// and then wait for it to be 0 again. Only the game's interrupt handlers clear
// it: the GPU interrupt callback (sub_82111E38) or, when it defers, the vblank
// handler (sub_820FDBA0). If that release never comes, the game freezes
// (issue #10). The wait only paces presents, so it is released after a second.
constexpr double kPresentThrottleEscapeSeconds = 1.0;

// info: the WAIT_REG_MEM info dword (bit 4 = memory poll, bits 0-2 = function).
// poll keeps the endian bits in its low two bits.
inline bool IsPresentThrottleWait(uint32_t info, uint32_t poll, uint32_t ref, uint32_t mask,
                                  uint32_t scratch_address) {
  constexpr uint32_t kMemoryPoll = 0x10, kFunctionEqual = 3;
  return scratch_address && (info & kMemoryPoll) && (info & 7) == kFunctionEqual && ref == 0 &&
         mask == 0xFFFFFFFFu && (poll & ~3u) == scratch_address + 4;
}

inline bool ShouldEscapePresentThrottle(double waited_seconds) {
  return waited_seconds >= kPresentThrottleEscapeSeconds;
}
}  // namespace superman_returns::native
