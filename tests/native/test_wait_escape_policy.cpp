#include "wait_escape_policy.h"
#include "test_main.h"

using namespace superman_returns::native;

namespace {
constexpr uint32_t kScratch = 0x1FCA3000;
constexpr uint32_t kMemoryEqual = 0x13;  // memory poll, func 3 (equal)
}  // namespace

SR_TEST(wait_escape_matches_only_the_present_throttle) {
  // The issue #10 wait: memory scratch+4 (k8in32 endian bits), equal 0, full mask.
  SR_CHECK(IsPresentThrottleWait(kMemoryEqual, kScratch + 4 + 2, 0, 0xFFFFFFFF, kScratch));
  SR_CHECK(!IsPresentThrottleWait(kMemoryEqual, kScratch + 4 + 2, 1, 0xFFFFFFFF, kScratch));  // waits for 1
  SR_CHECK(!IsPresentThrottleWait(kMemoryEqual, kScratch + 0 + 2, 0, 0xFFFFFFFF, kScratch));  // scratch+0
  SR_CHECK(!IsPresentThrottleWait(0x03, kScratch + 4 + 2, 0, 0xFFFFFFFF, kScratch));          // register poll
  SR_CHECK(!IsPresentThrottleWait(0x14, kScratch + 4 + 2, 0, 0xFFFFFFFF, kScratch));          // not "equal"
  SR_CHECK(!IsPresentThrottleWait(kMemoryEqual, kScratch + 4 + 2, 0, 0x0000FFFF, kScratch));  // partial mask
  SR_CHECK(!IsPresentThrottleWait(kMemoryEqual, kScratch + 4 + 2, 0, 0xFFFFFFFF, 0));         // no scratch yet
}

SR_TEST(wait_escape_after_one_second) {
  SR_CHECK(!ShouldEscapePresentThrottle(0.99));
  SR_CHECK(ShouldEscapePresentThrottle(kPresentThrottleEscapeSeconds));
  SR_CHECK(ShouldEscapePresentThrottle(30.0));
}
