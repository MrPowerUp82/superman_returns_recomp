#pragma once
#include "texture_layout.h"
#include <memory>

namespace superman_returns::graphics::guest {
struct TextureCapture {
  std::array<uint32_t, 6> fetch{};
  uint64_t version = 0;
  std::vector<TextureRange> ranges;
  CapturedMemory memory;
};
bool CaptureTexture(std::span<const uint32_t, 6>, uint64_t version,
                    const GuestMemoryReader &,
                    std::shared_ptr<const TextureCapture> &,
                    std::string &error);
} // namespace superman_returns::graphics::guest
