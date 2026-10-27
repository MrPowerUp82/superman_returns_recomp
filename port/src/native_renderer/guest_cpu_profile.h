#pragma once
#include <chrono>
#include "guest_sleep_profile.h"
#include <rex/ppc/func.h>
// SR_NATIVE_PRESENT_INTERVAL_PROBE_TRIGGER_PATH=<file>: independent diagnostic.
// Logs the first eight interval setters, then up to 128 pre-Swap calls and
// 128 Swap-owned fence waits once the file exists (trigger remains latched).
// Disabled by default; wrappers always execute their original exactly once.
namespace superman_returns::native {
// Diagnostic wrapper: always calls the original exactly once. Uses the same
// opt-in switch as the sampled guest functions; timings are wall time.
void ProfileGuestGraphicsOriginal(uint32_t address,PPCFunc* original,PPCContext& ctx,uint8_t* base);
// Diagnostic only. The outer capture span is attributed to the active guest
// scope; nested capture scopes must not count the same time twice.
class GuestNativeCaptureProfile {
public:
  GuestNativeCaptureProfile();
  ~GuestNativeCaptureProfile();
  GuestNativeCaptureProfile(const GuestNativeCaptureProfile&)=delete;
  GuestNativeCaptureProfile& operator=(const GuestNativeCaptureProfile&)=delete;
private:
  bool enabled_=false,outer_=false;
  std::chrono::steady_clock::time_point begin_{};
};
}
