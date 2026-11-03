#pragma once
#include <cstdint>

namespace superman_returns::coop {
// Development observer only; does not change a camera, a packet or scheduling.
void NoteCameraProbePresent();
void NoteCameraProbeGroup(uint32_t group, uint32_t subgroup, uint32_t caller);
}
