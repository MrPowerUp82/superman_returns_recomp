#ifdef _WIN32
#include "cpu_features.h"
#include "xxh3_avx2.h"
#include "test_main.h"
#include <cstdio>
#include <vector>
uint64_t Xxh3Reference(const void* data, size_t size, uint64_t seed);  // xxh3_reference.cpp
using namespace superman_returns::native;
SR_TEST(avx2_xxh3_matches_the_baseline_xxh3_for_every_length_class_and_seed) {
  if (!Avx2Available()) { std::puts("skip: this CPU has no AVX2"); return; }
  std::vector<uint8_t> bytes((1u << 20) + 16);
  uint32_t state = 12345;
  for (auto& b : bytes) { state = state * 1664525u + 1013904223u; b = uint8_t(state >> 24); }
  // XXH3 switches algorithm at 16, 128, 240 bytes, and its long path works in 1 KiB stripes.
  for (size_t size : {0, 1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 128, 129, 239, 240, 241, 255, 256, 257,
                      1023, 1024, 1025, 4096, 4097, 100003, 262144, 1 << 20}) {
    for (uint64_t seed : {uint64_t(0), uint64_t(0xcbf29ce484222325ull), ~uint64_t(0)}) {
      SR_CHECK_EQ(Xxh3Avx2(bytes.data(), size, seed), Xxh3Reference(bytes.data(), size, seed));
    }
  }
  for (size_t offset : {1, 3, 7, 13}) {  // unaligned starts
    SR_CHECK_EQ(Xxh3Avx2(bytes.data() + offset, 100000, 5), Xxh3Reference(bytes.data() + offset, 100000, 5));
  }
}
#endif
