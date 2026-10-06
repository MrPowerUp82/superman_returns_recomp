// Built with -mavx2 (port/CMakeLists.txt, tests/native/CMakeLists.txt). Nothing else may live in this file: any
// inline function emitted here could be picked by the linker for the whole program.
#define XXH_INLINE_ALL
#include <xxhash.h>
#include "xxh3_avx2.h"
namespace superman_returns::native {
uint64_t Xxh3Avx2(const void* data, size_t size, uint64_t seed) { return uint64_t(XXH3_64bits_withSeed(data, size, seed)); }
}  // namespace superman_returns::native
