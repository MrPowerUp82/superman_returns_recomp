#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace superman_returns::native {
struct CpuBundleOptions {
  bool tracking_on = false, bounded_reads = false, shared_staging = false, resolve_identity = false;
  uint32_t srv_capacity = 0;
};
inline CpuBundleOptions SelectCpuBundle(bool enabled, bool d3d12, bool bounded,
    bool staging, bool identity, uint32_t capacity) {
  const bool bundle = enabled && d3d12;
  return {bundle, d3d12 && (bundle || bounded), d3d12 && (bundle || staging),
          d3d12 && (bundle || identity), d3d12 ? std::min(8192u, bundle ? std::max(4096u, capacity) : capacity) : 0u};
}

using GammaUploadBlock = std::array<uint32_t, 260>;
inline GammaUploadBlock BuildGammaUpload(std::span<const uint32_t> ramp, bool enabled) {
  GammaUploadBlock block{};
  if (ramp.size() != 256) return block;
  std::copy(ramp.begin(), ramp.end(), block.begin());
  block[256] = enabled && std::any_of(ramp.begin(), ramp.end(), [](uint32_t v) { return v != 0; });
  return block;
}
} // namespace superman_returns::native
