#pragma once
#include <algorithm>
#include <cstdint>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace superman_returns::graphics {

enum class CadenceMode : uint32_t {
  kUnlocked = 0,
  kExact1to1 = 1,
  kIntegerDivisor = 2,
  kFractionalVRR = 3,
};

inline const char* CadenceModeToString(CadenceMode mode) {
  switch (mode) {
    case CadenceMode::kExact1to1: return "exact_1to1";
    case CadenceMode::kIntegerDivisor: return "integer_divisor";
    case CadenceMode::kFractionalVRR: return "fractional_vrr_pulldown";
    case CadenceMode::kUnlocked:
    default: return "unlocked";
  }
}

struct DisplayCadence {
  uint32_t target_fps = 0;
  uint32_t display_hz = 0;
  CadenceMode mode = CadenceMode::kUnlocked;
  uint32_t divisor = 0;
  uint64_t target_period_ns = 0;
  uint64_t display_period_ns = 0;
  double ratio = 0.0;
  bool allows_tearing = false;
};

inline uint32_t QueryHostDisplayRefreshRate() {
#if defined(_WIN32)
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency >= 24) {
    return (std::clamp)(uint32_t(dm.dmDisplayFrequency), 24u, 1000u);
  }
#endif
  return 60u;
}

inline DisplayCadence EvaluateDisplayCadence(uint32_t target_fps, uint32_t display_hz,
                                             bool allows_tearing = true) {
  DisplayCadence cadence;
  cadence.target_fps = (std::min)(target_fps, 1000u);
  cadence.display_hz = (std::clamp)(display_hz ? display_hz : 60u, 24u, 1000u);
  cadence.allows_tearing = allows_tearing;
  cadence.display_period_ns = 1000000000ull / cadence.display_hz;

  if (!cadence.target_fps) {
    cadence.mode = CadenceMode::kUnlocked;
    cadence.divisor = 0;
    cadence.target_period_ns = 0;
    cadence.ratio = 0.0;
    return cadence;
  }

  cadence.target_period_ns = 1000000000ull / cadence.target_fps;
  cadence.ratio = double(cadence.display_hz) / double(cadence.target_fps);

  if (cadence.display_hz == cadence.target_fps) {
    cadence.mode = CadenceMode::kExact1to1;
    cadence.divisor = 1;
  } else if (cadence.display_hz % cadence.target_fps == 0) {
    cadence.mode = CadenceMode::kIntegerDivisor;
    cadence.divisor = cadence.display_hz / cadence.target_fps;
  } else {
    cadence.mode = CadenceMode::kFractionalVRR;
    cadence.divisor = 0;
  }

  return cadence;
}

} // namespace superman_returns::graphics
