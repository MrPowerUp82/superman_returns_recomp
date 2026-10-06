#pragma once
#include <cstddef>
#include <cstdint>
namespace superman_returns::native {
// XXH3_64bits_withSeed built with AVX2 (xxh3_avx2.cpp, the only file compiled with -mavx2). The value is the same
// as the baseline XXH3's; only the speed changes. Call it only when Avx2Available() (cpu_features.h).
uint64_t Xxh3Avx2(const void* data, size_t size, uint64_t seed);
}  // namespace superman_returns::native
