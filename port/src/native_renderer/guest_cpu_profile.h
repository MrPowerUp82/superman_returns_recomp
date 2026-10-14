#pragma once
#include <chrono>
namespace superman_returns::native {
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
