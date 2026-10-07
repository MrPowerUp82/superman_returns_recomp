#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace superman_returns::native {
using SharedConstantBlock = std::array<uint32_t, 1024>;

// Stores an immutable upload allocation, never mapped GPU memory. The caller
// resets it when the upload ring is recycled and commits only after uploading.
class SharedConstantsCache {
 public:
  uint64_t Find(const SharedConstantBlock& block) const {
    return gpu_ && std::memcmp(block.data(), block_.data(), sizeof(block)) == 0 ? gpu_ : 0;
  }
  void Commit(const SharedConstantBlock& block, uint64_t gpu) {
    block_ = block;
    gpu_ = gpu;
  }
  void Reset() { gpu_ = 0; }

 private:
  SharedConstantBlock block_{};
  uint64_t gpu_ = 0;
};
}  // namespace superman_returns::native
