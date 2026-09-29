// Xenos texture addressing and swizzle helpers, moved out of the kit's
// texture_decode.cpp (crazyriddler/rexglue-native-kit @136bc6c4,
// reference/conan/port/src/native/texture_decode.cpp) into a pure header so
// tests/native checks them without the game or the SDK. The math is
// unchanged; it ports Xenia's (BSD) XenosTextureTiledAddress2D and
// GetTiledOffset3D.
#pragma once

#include <algorithm>
#include <cstdint>

namespace superman_returns::native {

// Xenos 2D tiled address (port of Xenia's XenosTextureTiledAddress2D from
// texture_address.xesli, the path the GPU texture loads use). Coordinates in
// blocks, pitch in 32-block macro tiles, result in bytes.
constexpr int32_t TiledAddress2D(int32_t x, int32_t y, uint32_t pitch_macro_tiles, uint32_t bpb_log2) {
  int32_t outer_blocks = ((y >> 5) * int32_t(pitch_macro_tiles) + (x >> 5)) << 6;
  int32_t inner_blocks = (((y >> 1) & 0x7) << 3) | (x & 0x7);
  int32_t outer_inner_bytes = (outer_blocks | inner_blocks) << bpb_log2;
  int32_t bank = (y >> 4) & 0x1;
  int32_t pipe = ((x >> 3) & 0x3) ^ (((y >> 3) & 0x1) << 1);
  return ((y & 1) << 4) | (pipe << 6) | (bank << 11) | (outer_inner_bytes & 0xF) |
         (((outer_inner_bytes >> 4) & 0x1) << 5) | (((outer_inner_bytes >> 5) & 0x7) << 8) |
         (outer_inner_bytes >> 8 << 12);
}

// Xenos 3D tiled address (port of Xenia's GetTiledOffset3D, reconstructed
// from XGRAPHICS::TileVolume). Coordinates in blocks, result in bytes.
constexpr int32_t TiledAddress3D(int32_t x, int32_t y, int32_t z, uint32_t pitch, uint32_t height,
                              uint32_t bpb_log2) {
  pitch = (pitch + 31) & ~31u;
  height = (height + 31) & ~31u;
  int32_t macro_outer = ((y >> 4) + (z >> 2) * int32_t(height >> 4)) * int32_t(pitch >> 5);
  int32_t macro = ((((x >> 5) + macro_outer) << (bpb_log2 + 6)) & 0xFFFFFFF) << 1;
  int32_t micro = (((x & 7) + ((y & 6) << 2)) << (bpb_log2 + 6)) >> 6;
  int32_t offset_outer = ((y >> 3) + (z >> 2)) & 1;
  int32_t offset1 = offset_outer + ((((x >> 3) + (offset_outer << 1)) & 3) << 1);
  int32_t offset2 = ((macro + (micro & ~15)) << 1) + (micro & 15) +
                    ((z & 3) << (bpb_log2 + 6)) + ((y & 1) << 4);
  int32_t address = (offset1 & 1) << 3;
  address += (offset2 >> 6) & 7;
  address <<= 3;
  address += offset1 & ~1;
  address <<= 2;
  address += offset2 & ~511;
  address <<= 3;
  address += offset2 & 63;
  return address;
}

// log2 of the bytes per block (1, 2, 4, 8 or 16).
constexpr uint32_t BytesPerBlockLog2(uint32_t bytes_per_block) {
  return bytes_per_block == 16 ? 4 : bytes_per_block == 8 ? 3 : bytes_per_block == 4 ? 2
                                   : bytes_per_block == 2 ? 1 : 0;
}

// Xenos swizzle: 3 bits per destination component, 0-3 = source XYZW, 4 = 0,
// 5 = 1. D3D12 component mapping: 0-3 = source RGBA, 4 = force 0, 5 = force 1
// (D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0/1). For single/dual-component
// formats the Xenos texture unit replicates the last component into the
// missing ones before swizzling, emulated by clamping the source index.
// Returns D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING of the four components
// (texture_decode.cpp static_asserts the encoding against the D3D12 macro).
constexpr uint32_t EncodeSwizzleMapping(uint32_t swizzle, uint32_t component_count) {
  uint32_t mapping[4] = {};
  for (int i = 0; i < 4; ++i) {
    uint32_t s = (swizzle >> (3 * i)) & 7;
    if (s < 4) {
      s = std::min(s, component_count - 1);
    } else if (s == 4) {
      s = 4;
    } else {
      s = 5;
    }
    mapping[i] = s;
  }
  return (mapping[0] & 7) | ((mapping[1] & 7) << 3) | ((mapping[2] & 7) << 6) |
         ((mapping[3] & 7) << 9) | (1u << 12);
}

}  // namespace superman_returns::native
