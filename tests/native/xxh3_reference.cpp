// Baseline build of the library's XXH3 (no -mavx2): what the game used before the AVX2 build.
#define XXH_INLINE_ALL
#include <xxhash.h>
#include <cstddef>
#include <cstdint>
uint64_t Xxh3Reference(const void* data, size_t size, uint64_t seed) {
  return uint64_t(XXH3_64bits_withSeed(data, size, seed));
}
