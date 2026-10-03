// Xenos texture addressing and swizzle (port/src/native_renderer/xenos_tiling.h).
//
// The kit ported TiledAddress2D from Xenia's shader-side
// XenosTextureTiledAddress2D. The SDK's CPU-side GetTiledOffset2D (below,
// copied from ReXGlue v0.10.0 src/graphics/pipeline/texture/util.cpp, Xenia,
// BSD license, derived from UModel) is an independent formulation of the same
// Xenos layout, so agreement over many sizes checks the port.

#include <cstdint>
#include <set>

#include "test_main.h"
#include "xenos_tiling.h"
#include "texture_binding.h"

SR_TEST(texture_fetch_can_be_bound_without_an_xdk_texture_object) {
  using superman_returns::native::IsTextureBound;
  // The movie player writes Y/U/V fetch constants directly into PM4, while
  // the device's texture-object slots remain null.
  SR_CHECK(IsTextureBound(0x0A020002));
  SR_CHECK(IsTextureBound(0x06000002));
  SR_CHECK(IsTextureBound(0x8A024802));
  SR_CHECK(!IsTextureBound(0));
  SR_CHECK(!IsTextureBound(3));
}

namespace {

using superman_returns::native::BytesPerBlockLog2;
using superman_returns::native::EncodeSwizzleMapping;
using superman_returns::native::TiledAddress2D;
using superman_returns::native::TiledAddress3D;

// Copyright 2022 Ben Vanik. All rights reserved. Released under the BSD license.
// (Xenia; modified by Tom Clay 2026 for ReXGlue: rex::align inlined here.)
int32_t ReferenceTiledOffset2D(int32_t x, int32_t y, uint32_t pitch, uint32_t bytes_per_block_log2) {
  pitch = (pitch + 31) & ~31u;
  int32_t macro = ((x >> 5) + (y >> 5) * int32_t(pitch >> 5)) << (bytes_per_block_log2 + 7);
  int32_t micro = ((x & 7) + ((y & 0xE) << 2)) << bytes_per_block_log2;
  int32_t offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F);
}

}  // namespace

SR_TEST(tiled_2d_matches_sdk_cpu_formula) {
  const uint32_t pitches[] = {1, 8, 31, 32, 33, 64, 80, 160, 320, 1280};
  for (uint32_t bpb_log2 = 0; bpb_log2 <= 4; ++bpb_log2) {
    for (uint32_t pitch : pitches) {
      const uint32_t macro_tiles = (pitch + 31) >> 5;
      for (int32_t y = 0; y < 96; ++y) {
        for (int32_t x = 0; x < int32_t(macro_tiles * 32); ++x) {
          const int32_t kit = TiledAddress2D(x, y, macro_tiles, bpb_log2);
          const int32_t sdk = ReferenceTiledOffset2D(x, y, pitch, bpb_log2);
          if (kit != sdk) {
            SR_CHECK_EQ(kit, sdk);
            return;
          }
        }
      }
    }
  }
}

// Bank/pipe bits interleave neighbouring macro tiles, so a single tile is not
// contiguous; a region of whole macro tiles (4 x 2 here) maps one-to-one onto
// block-aligned offsets filling exactly its byte size.
SR_TEST(tiled_2d_region_is_a_permutation) {
  for (uint32_t bpb_log2 = 0; bpb_log2 <= 4; ++bpb_log2) {
    std::set<int32_t> seen;
    const int32_t w = 4 * 32, h = 2 * 32;
    const int32_t bytes = (w * h) << bpb_log2;
    bool in_range = true, aligned = true;
    for (int32_t y = 0; y < h; ++y) {
      for (int32_t x = 0; x < w; ++x) {
        const int32_t a = TiledAddress2D(x, y, 4, bpb_log2);
        in_range &= a >= 0 && a < bytes;
        aligned &= (a & ((1 << bpb_log2) - 1)) == 0;
        seen.insert(a);
      }
    }
    SR_CHECK(in_range);
    SR_CHECK(aligned);
    SR_CHECK_EQ(seen.size(), size_t(w * h));
  }
}

SR_TEST(tiled_3d_is_injective_on_a_small_volume) {
  for (uint32_t bpb_log2 : {0u, 2u, 4u}) {
    std::set<int32_t> seen;
    const uint32_t w = 64, h = 32, d = 8;
    for (int32_t z = 0; z < int32_t(d); ++z) {
      for (int32_t y = 0; y < int32_t(h); ++y) {
        for (int32_t x = 0; x < int32_t(w); ++x) {
          const int32_t a = TiledAddress3D(x, y, z, w, h, bpb_log2);
          SR_CHECK(a >= 0);
          seen.insert(a);
        }
      }
    }
    SR_CHECK_EQ(seen.size(), size_t(w * h * d));
  }
}

SR_TEST(tiled_origin_is_zero) {
  for (uint32_t bpb_log2 = 0; bpb_log2 <= 4; ++bpb_log2) {
    SR_CHECK_EQ(TiledAddress2D(0, 0, 4, bpb_log2), 0);
    SR_CHECK_EQ(TiledAddress3D(0, 0, 0, 64, 64, bpb_log2), 0);
  }
}

SR_TEST(bytes_per_block_log2) {
  SR_CHECK_EQ(BytesPerBlockLog2(1), 0u);
  SR_CHECK_EQ(BytesPerBlockLog2(2), 1u);
  SR_CHECK_EQ(BytesPerBlockLog2(4), 2u);
  SR_CHECK_EQ(BytesPerBlockLog2(8), 3u);
  SR_CHECK_EQ(BytesPerBlockLog2(16), 4u);
}

namespace {
// D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING, spelled out (d3d12.h is not
// available here; texture_decode.cpp static_asserts the same equality with it).
constexpr uint32_t Encode(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
  return r | (g << 3) | (b << 6) | (a << 9) | (1u << 12);
}
constexpr uint32_t Swizzle(uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
  return x | (y << 3) | (z << 6) | (w << 9);
}
}  // namespace

SR_TEST(swizzle_identity_and_constants) {
  SR_CHECK_EQ(EncodeSwizzleMapping(Swizzle(0, 1, 2, 3), 4), Encode(0, 1, 2, 3));
  SR_CHECK_EQ(EncodeSwizzleMapping(Swizzle(2, 1, 0, 3), 4), Encode(2, 1, 0, 3));
  // 4 = constant 0, 5 = constant 1 (D3D12 FORCE_VALUE_0 / FORCE_VALUE_1).
  SR_CHECK_EQ(EncodeSwizzleMapping(Swizzle(0, 4, 5, 5), 4), Encode(0, 4, 5, 5));
  // 6 and 7 also read as 1, like the kit.
  SR_CHECK_EQ(EncodeSwizzleMapping(Swizzle(6, 7, 0, 0), 4), Encode(5, 5, 0, 0));
}

SR_TEST(swizzle_replicates_last_component_of_small_formats) {
  // One-component format: every source channel reads component 0.
  SR_CHECK_EQ(EncodeSwizzleMapping(Swizzle(0, 1, 2, 3), 1), Encode(0, 0, 0, 0));
  // Two components: z and w read y.
  SR_CHECK_EQ(EncodeSwizzleMapping(Swizzle(0, 1, 2, 3), 2), Encode(0, 1, 1, 1));
  // Constants are not clamped.
  SR_CHECK_EQ(EncodeSwizzleMapping(Swizzle(3, 4, 5, 0), 1), Encode(0, 4, 5, 0));
}
