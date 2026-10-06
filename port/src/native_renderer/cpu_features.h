#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace superman_returns::native {
// True when the CPU and the OS run AVX2. Deliberately outside xxh3_avx2.cpp: that file is built with -mavx2.
inline bool Avx2Available() { return IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != 0; }
}  // namespace superman_returns::native
