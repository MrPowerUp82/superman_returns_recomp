#pragma once
#include "captured_batch.h"
#include "pm4_capture.h"
#include <array>

namespace superman_returns::graphics::guest {
enum class LinearFormat {
  kUnknown,
  kR8Unorm,
  kRG8Unorm,
  kRGBA8Unorm,
  kRGB10A2Unorm,
  kBGR565Unorm,
  kBGR5A1Unorm,
  kBGRA4Unorm,
  kBC1,
  kBC2,
  kBC3,
  kBC5,
  kR16Unorm,
  kRG16Unorm,
  kRGBA16Unorm,
  kR16Float,
  kRG16Float,
  kRGBA16Float,
  kR32Float,
  kRG32Float,
  kRGBA32Float
};
struct LinearTexture {
  uint32_t width = 0, height = 0, depth = 1, mip_levels = 1, dimension = 1;
  LinearFormat format = LinearFormat::kUnknown;
  std::array<uint32_t, 4> swizzle = {0, 1, 2, 3};
  uint32_t guest_format = 0, base_address = 0, base_size = 0;
  struct Level {
    uint32_t width, height, row_pitch, rows;
    size_t offset;
  };
  // Mip-major, then array slice (or z slice for volume base level).
  std::vector<Level> levels;
  std::vector<uint8_t> data;
};
struct TextureRange {
  uint32_t address, length;
};
// Addresses are physical. CapturedMemory reads use the guest cached view.
bool DescribeTextureRanges(std::span<const uint32_t, 6>,
                           std::vector<TextureRange> &, std::string &);
bool DecodeTextureLayout(std::span<const uint32_t, 6>, const CapturedMemory &,
                         LinearTexture &, std::string &);
// Frontend/D3D adapter only. Vulkan consumes the CapturedMemory overload.
bool DecodeTextureLayoutUsing(std::span<const uint32_t, 6>,
                              const GuestMemoryReader &, LinearTexture &,
                              std::string &);
} // namespace superman_returns::graphics::guest
