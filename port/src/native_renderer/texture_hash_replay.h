#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

namespace superman_returns::native {
struct TextureHashReplayCase {
  // Views into an immutable owned snapshot, resolved before timing.
  std::vector<std::span<const uint8_t>> levels, united;
};
struct TextureHashReplayResult {
  bool stable = false;
  double level_ns = 0, union_ns = 0; // Median nanoseconds per whole corpus pass.
  uint64_t level_digest = 0, union_digest = 0;
};
template <typename Hash>
uint64_t TextureHashReplayDigest(std::span<const TextureHashReplayCase> cases,
                                 bool united, Hash&& hash) {
  uint64_t digest = 0;
  for (const auto& sample : cases) {
    uint64_t value = 0;
    for (const auto bytes : united ? sample.united : sample.levels)
      value = hash(bytes.data(), bytes.size(), value);
    digest += value;
  }
  return digest;
}
template <typename Hash>
TextureHashReplayResult ReplayTextureHashes(std::span<const TextureHashReplayCase> cases,
    Hash&& hash, uint32_t repetitions = 4, uint32_t trials = 9) {
  TextureHashReplayResult result;
  if (cases.empty() || !repetitions || trials < 3 || trials > 15) return result;
  for (const auto& sample : cases) {
    if (sample.levels.empty() || sample.united.empty()) return result;
    for (bool united : {false, true})
      for (auto bytes : united ? sample.united : sample.levels)
        if (bytes.empty()) return result;
  }
  // Each policy has its own expected digest: changing range boundaries changes
  // a chained hash. Equality between policies is neither expected nor required.
  result.level_digest = TextureHashReplayDigest(cases, false, hash);
  result.union_digest = TextureHashReplayDigest(cases, true, hash);
  result.stable = true;
  std::array<double, 15> level_times{}, union_times{};
  for (uint32_t trial = 0; trial < trials; ++trial) {
    // Alternate AB/BA, starting from a data-dependent order. Both policies
    // traverse exactly the same immutable corpus on every trial.
    for (uint32_t position = 0; position < 2; ++position) {
      const bool united = ((trial + position + (result.level_digest & 1)) & 1) != 0;
      const auto start = std::chrono::steady_clock::now();
      uint64_t digest = 0;
      for (uint32_t repeat = 0; repeat < repetitions; ++repeat)
        digest += TextureHashReplayDigest(cases, united, hash);
      const auto elapsed = std::chrono::steady_clock::now() - start;
      (united ? union_times : level_times)[trial] =
          std::chrono::duration<double, std::nano>(elapsed).count() / repetitions;
      // Consume and check the timed output, keeping the hash work observable.
      const auto expected = united ? result.union_digest : result.level_digest;
      result.stable = result.stable && digest == expected * repetitions;
    }
  }
  auto median = [trials](auto values) {
    std::sort(values.begin(), values.begin() + trials);
    return (trials & 1) ? values[trials / 2]
        : (values[trials / 2 - 1] + values[trials / 2]) / 2;
  };
  result.level_ns = median(level_times);
  result.union_ns = median(union_times);
  return result;
}
}  // namespace superman_returns::native
