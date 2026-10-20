#pragma once
#include <chrono>
#include <rex/ppc/func.h>
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
