#pragma once

#include <cstdint>

// Called once per guest present with the running frame count.
void OnGuestFrameForCapture(uint64_t frame);
