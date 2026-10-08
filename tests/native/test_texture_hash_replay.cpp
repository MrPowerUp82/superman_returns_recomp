#include "texture_hash_replay.h"
#include "test_main.h"

using namespace superman_returns::native;
namespace {
uint64_t Hash(const uint8_t* bytes, size_t size, uint64_t seed) {
  // Include request boundaries so the policies deliberately produce different digests.
  seed ^= size;
  for (size_t i = 0; i < size; ++i) seed = (seed ^ bytes[i]) * 1099511628211ull;
  return seed;
}
}
SR_TEST(texture_hash_replay_checks_each_policy_against_its_own_digest) {
  const std::array<uint8_t, 8> bytes{1,2,3,4,5,6,7,8};
  const std::array<TextureHashReplayCase, 1> cases{{
    {{std::span(bytes).first(4), std::span(bytes).subspan(2,4), std::span(bytes).last(2)},
     {std::span(bytes)}}}};
  const auto result = ReplayTextureHashes(cases, Hash, 3, 4);
  SR_CHECK(result.stable);
  SR_CHECK(result.level_digest != result.union_digest);
  SR_CHECK_EQ(result.level_digest, TextureHashReplayDigest(cases, false, Hash));
  SR_CHECK_EQ(result.union_digest, TextureHashReplayDigest(cases, true, Hash));
}
SR_TEST(texture_hash_replay_detects_inputs_changing_during_timing) {
  std::array<uint8_t, 4> bytes{1,2,3,4};
  const std::array<TextureHashReplayCase, 1> cases{{{{std::span(bytes)}, {std::span(bytes)}}}};
  auto changing_hash = [&](const uint8_t* data, size_t size, uint64_t seed) {
    const auto value = Hash(data, size, seed);
    ++bytes[0];
    return value;
  };
  SR_CHECK(!ReplayTextureHashes(cases, changing_hash, 2, 3).stable);
}
SR_TEST(texture_hash_replay_rejects_empty_inputs_and_invalid_trial_counts) {
  const std::array<TextureHashReplayCase, 1> empty{};
  SR_CHECK(!ReplayTextureHashes(empty, Hash).stable);
  SR_CHECK(!ReplayTextureHashes({}, Hash).stable);
  const std::array<uint8_t, 1> byte{7};
  const std::array<TextureHashReplayCase, 1> cases{{{{std::span(byte)}, {std::span(byte)}}}};
  SR_CHECK(!ReplayTextureHashes(cases, Hash, 0, 3).stable);
  SR_CHECK(!ReplayTextureHashes(cases, Hash, 2, 2).stable);
  SR_CHECK(!ReplayTextureHashes(cases, Hash, 2, 16).stable);
}
