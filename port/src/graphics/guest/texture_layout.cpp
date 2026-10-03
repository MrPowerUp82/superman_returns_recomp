// Ported from crazyriddler/rexglue-native-kit @136bc6c4,
// reference/conan/port/src/native/texture_decode.cpp (Conan native renderer).
// The kit ships no license file at that revision; parts derived from Xenia /
// ReXGlue keep their BSD license. Changes for Superman Returns are listed in
// docs/native-port-plan.md section 2.
//
#include "texture_layout.h"
#include "../../native_renderer/xenos_tiling.h"
#include <exception>
#include <stdexcept>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/xenos.h>

namespace superman_returns::graphics::guest {
using native::EncodeSwizzleMapping;
using native::TiledAddress2D;
using native::TiledAddress3D;

namespace xenos = rex::graphics::xenos;
using rex::graphics::FormatInfo;
using rex::graphics::TextureExtent;
using rex::graphics::TextureInfo;

namespace {

struct HostFormat {
  LinearFormat format;
  // Guest data needs per-block conversion before upload (unsupported for now).
  bool supported;
};

HostFormat MapFormat(xenos::TextureFormat f) {
  using TF = xenos::TextureFormat;
  switch (f) {
  case TF::k_8:
  case TF::k_8_A:
  case TF::k_8_B:
    return {LinearFormat::kR8Unorm, true};
  case TF::k_8_8:
    return {LinearFormat::kRG8Unorm, true};
  case TF::k_8_8_8_8:
  case TF::k_8_8_8_8_A:
    return {LinearFormat::kRGBA8Unorm, true};
  case TF::k_2_10_10_10:
    return {LinearFormat::kRGB10A2Unorm, true};
  case TF::k_5_6_5:
    return {LinearFormat::kBGR565Unorm, true};
  case TF::k_1_5_5_5:
    return {LinearFormat::kBGR5A1Unorm, true};
  case TF::k_4_4_4_4:
    return {LinearFormat::kBGRA4Unorm, true};
  case TF::k_DXT1:
    return {LinearFormat::kBC1, true};
  case TF::k_DXT2_3:
    return {LinearFormat::kBC2, true};
  case TF::k_DXT4_5:
    return {LinearFormat::kBC3, true};
  case TF::k_DXN:
    return {LinearFormat::kBC5, true};
  case TF::k_16:
    return {LinearFormat::kR16Unorm, true};
  case TF::k_16_16:
    return {LinearFormat::kRG16Unorm, true};
  case TF::k_16_16_16_16:
    return {LinearFormat::kRGBA16Unorm, true};
  // _EXPAND formats hold 16-bit floats (Xenia maps them the same way).
  case TF::k_16_EXPAND:
    return {LinearFormat::kR16Float, true};
  case TF::k_16_16_EXPAND:
    return {LinearFormat::kRG16Float, true};
  case TF::k_16_16_16_16_EXPAND:
    return {LinearFormat::kRGBA16Float, true};
  case TF::k_16_FLOAT:
    return {LinearFormat::kR16Float, true};
  case TF::k_16_16_FLOAT:
    return {LinearFormat::kRG16Float, true};
  case TF::k_16_16_16_16_FLOAT:
    return {LinearFormat::kRGBA16Float, true};
  case TF::k_32_FLOAT:
    return {LinearFormat::kR32Float, true};
  case TF::k_32_32_FLOAT:
    return {LinearFormat::kRG32Float, true};
  case TF::k_32_32_32_32_FLOAT:
    return {LinearFormat::kRGBA32Float, true};
  case TF::k_24_8:
  case TF::k_24_8_FLOAT:
    // Depth textures sampled by the game (shadow maps) - bit layout differs
    // from D24S8; handled via resolve targets, not guest memory, for now.
    return {LinearFormat::kUnknown, false};
  default:
    return {LinearFormat::kUnknown, false};
  }
}

uint32_t ComponentCount(LinearFormat f) {
  switch (f) {
  case LinearFormat::kR8Unorm:
  case LinearFormat::kR16Unorm:
  case LinearFormat::kR16Float:
  case LinearFormat::kR32Float:
    return 1;
  case LinearFormat::kRG8Unorm:
  case LinearFormat::kRG16Unorm:
  case LinearFormat::kRG16Float:
  case LinearFormat::kRG32Float:
  case LinearFormat::kBC5:
    return 2;
  case LinearFormat::kBGR565Unorm:
    return 3;
  default:
    return 4;
  }
}

} // namespace

bool DecodeTextureLayoutUsing(std::span<const uint32_t, 6> fetch_dwords,
                              const GuestMemoryReader &read,
                              LinearTexture &result, std::string &error) {
  error.clear();
  try {
    LinearTexture out;
    auto fail = [&](const char *why) {
      error = why;
      return false;
    };
    xenos::xe_gpu_texture_fetch_t fetch;
    std::memcpy(&fetch, fetch_dwords.data(), sizeof(fetch));
    if (fetch.type != xenos::FetchConstantType::kTexture) {
      return fail("not a texture fetch constant");
    }
    if (!MapFormat(fetch.format).supported ||
        (fetch.dimension == xenos::DataDimension::kCube &&
         fetch.size_2d.stack_depth != 5) ||
        (fetch.stacked &&
         fetch.dimension != xenos::DataDimension::k2DOrStacked))
      return fail("invalid texture fetch layout");
    TextureInfo info;
    if (!TextureInfo::Prepare(fetch, &info)) {
      return fail("TextureInfo::Prepare failed");
    }
    if (info.dimension != xenos::DataDimension::k2DOrStacked &&
        info.dimension != xenos::DataDimension::kCube &&
        info.dimension != xenos::DataDimension::k3D) {
      return fail("1D texture");
    }
    bool volume = info.dimension == xenos::DataDimension::k3D;
    HostFormat host = MapFormat(info.format);
    if (!host.supported) {
      return fail("unsupported format");
    }
    const FormatInfo *fi = info.format_info();
    const uint32_t bpb = fi->bytes_per_block();
    std::vector<TextureRange> ranges;
    if (!DescribeTextureRanges(fetch_dwords, ranges, error))
      return false;

    out = LinearTexture{};
    out.width = info.width + 1;
    out.height = info.height + 1;
    out.depth = info.depth + 1;
    out.dimension = uint32_t(info.dimension);
    out.format = host.format;
    out.guest_format = uint32_t(info.format);
    out.base_address = info.memory.base_address;
    auto mapping =
        EncodeSwizzleMapping(fetch.swizzle, ComponentCount(host.format));
    for (uint32_t i = 0; i < 4; ++i)
      out.swizzle[i] = (mapping >> (i * 3)) & 7;
    // Only levels actually backed by memory.
    uint32_t levels = info.mip_levels();
    if (!info.memory.mip_address || volume) {
      levels = 1; // 3D: base level only for now
    }
    out.mip_levels = levels;

    auto copy_swap = [endian = info.endianness](void *o, const void *i,
                                                size_t len) {
      rex::graphics::texture_conversion::CopySwapBlock(endian, o, i, len);
    };

    for (uint32_t mip = 0; mip < levels; ++mip) {
      uint32_t offset_x = 0, offset_y = 0;
      uint32_t address = info.GetMipLocation(mip, &offset_x, &offset_y, true);
      if (mip == 0 && std::min(out.width, out.height) > 16) {
        // The packed mip tail only holds levels of 16 texels or less; the base
        // level of a larger texture always starts at the base address.
        // (Defensive: TextureInfo::GetMipLocation(0) asks for a packed-tile
        // offset whenever the texture has packed mips.)
        offset_x = offset_y = 0;
      }
      if (!address) {
        out.mip_levels = mip;
        break;
      }
      TextureExtent src_extent = info.GetMipExtent(mip, true);
      TextureExtent dst_extent = info.GetMipExtent(mip, false);
      uint32_t mip_w = std::max(1u, out.width >> mip);
      uint32_t mip_h = std::max(1u, out.height >> mip);
      uint32_t blocks_w = (mip_w + fi->block_width - 1) / fi->block_width;
      uint32_t blocks_h = (mip_h + fi->block_height - 1) / fi->block_height;
      uint32_t dst_pitch = blocks_w * bpb;
      // Slice stride in guest memory (stacked 2D arrays).
      uint32_t src_slice_bytes =
          src_extent.block_pitch_h * src_extent.block_pitch_v * bpb;
      if (info.is_tiled) {
        // Tiled array slices / cube faces are 4 KB aligned.
        src_slice_bytes = (src_slice_bytes + 4095) & ~4095u;
      }
      if (mip == 0) {
        out.base_size = src_slice_bytes * out.depth;
      }
      auto source = read(0xa0000000u + (address&0x1fffffffu), ranges[mip].length);
      if (source.size() < ranges[mip].length)
        return fail("texture source is truncated");
      if (volume) {
        const uint8_t *src = source.data();
        uint32_t bpb_log2 = bpb == 16  ? 4
                            : bpb == 8 ? 3
                            : bpb == 4 ? 2
                            : bpb == 2 ? 1
                                       : 0;
        uint32_t pitch_blocks = src_extent.block_pitch_h;
        uint32_t height_blocks = src_extent.block_pitch_v;
        for (uint32_t z = 0; z < out.depth; ++z) {
          LinearTexture::Level level{mip_w, mip_h, dst_pitch, blocks_h,
                                     out.data.size()};
          out.data.resize(out.data.size() + size_t(dst_pitch) * blocks_h);
          uint8_t *dst = out.data.data() + level.offset;
          for (uint32_t y = 0; y < blocks_h; ++y) {
            uint8_t *row = dst + size_t(y) * dst_pitch;
            for (uint32_t x = 0; x < blocks_w; ++x) {
              size_t off =
                  info.is_tiled
                      ? size_t(TiledAddress3D(int32_t(x), int32_t(y),
                                              int32_t(z), pitch_blocks,
                                              height_blocks, bpb_log2))
                      : (size_t(z) * height_blocks + y) * pitch_blocks * bpb +
                            x * bpb;
              if (off > source.size() || bpb > source.size()-off)
                return fail("volume block exceeds captured source");
              copy_swap(row + size_t(x) * bpb, src + off, bpb);
            }
          }
          out.levels.push_back(level);
        }
        continue;
      }
      for (uint32_t slice = 0; slice < out.depth; ++slice) {
        const uint8_t *src = source.data() + size_t(slice) * src_slice_bytes;
        LinearTexture::Level level{mip_w, mip_h, dst_pitch, blocks_h,
                                   out.data.size()};
        out.data.resize(out.data.size() + size_t(dst_pitch) * blocks_h);
        uint8_t *dst = out.data.data() + level.offset;
        if (!info.is_tiled) {
          uint32_t src_pitch = src_extent.block_pitch_h * bpb;
          const uint8_t *s =
              src + size_t(offset_y) * src_pitch + size_t(offset_x) * bpb;
          for (uint32_t y = 0; y < blocks_h; ++y) {
            if (size_t(s - source.data()) + size_t(y) * src_pitch + dst_pitch >
                source.size())
              return fail("linear row exceeds captured source");
            copy_swap(dst + size_t(y) * dst_pitch, s + size_t(y) * src_pitch,
                      dst_pitch);
          }
        } else {
          // Xenos 2D tiling (32x32-block macro tiles), same addressing as the
          // SDK's GPU texture load path.
          uint32_t pitch_blocks = src_extent.block_pitch_h;
          uint32_t bpb_log2 = bpb == 16  ? 4
                              : bpb == 8 ? 3
                              : bpb == 4 ? 2
                              : bpb == 2 ? 1
                                         : 0;
          for (uint32_t y = 0; y < blocks_h; ++y) {
            uint8_t *row = dst + size_t(y) * dst_pitch;
            for (uint32_t x = 0; x < blocks_w; ++x) {
              int32_t off =
                  TiledAddress2D(int32_t(x + offset_x), int32_t(y + offset_y),
                                 (pitch_blocks + 31) >> 5, bpb_log2);
              if (off < 0 || size_t(src - source.data()) + size_t(off) + bpb >
                                 source.size())
                return fail("tiled block exceeds captured source");
              copy_swap(row + size_t(x) * bpb, src + off, bpb);
            }
          }
        }
        (void)dst_extent;
        out.levels.push_back(level);
      }
    }
    if (out.levels.empty()) {
      return fail("no levels");
    }
    result = std::move(out);
    return true;
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
}

bool DescribeTextureRanges(std::span<const uint32_t, 6> words,
                           std::vector<TextureRange> &result,
                           std::string &error) {
  error.clear();
  xenos::xe_gpu_texture_fetch_t fetch{};
  std::memcpy(&fetch, words.data(), sizeof(fetch));
  if (fetch.type != xenos::FetchConstantType::kTexture ||
      !MapFormat(fetch.format).supported ||
      (fetch.dimension == xenos::DataDimension::kCube &&
       fetch.size_2d.stack_depth != 5) ||
      (fetch.stacked &&
       fetch.dimension != xenos::DataDimension::k2DOrStacked)) {
    error = "invalid or unsupported texture fetch";
    return false;
  }
  TextureInfo info;
  if (!TextureInfo::Prepare(fetch, &info)) {
    error = "TextureInfo::Prepare failed";
    return false;
  }
  uint32_t levels =
      (!info.memory.mip_address || info.dimension == xenos::DataDimension::k3D)
          ? 1
          : info.mip_levels();
  std::vector<TextureRange> ranges;
  for (uint32_t mip = 0; mip < levels; ++mip) {
    uint32_t x = 0, y = 0;
    uint32_t address = info.GetMipLocation(mip, &x, &y, true);
    if (!address) {
      error = "texture mip has no memory";
      return false;
    }
    // Match the SDK TranslatePhysical mask used by the reference decoder.
    address &= 0x1fffffffu;
    auto extent = info.GetMipExtent(mip, true);
    uint64_t slice = uint64_t(extent.block_pitch_h) * extent.block_pitch_v *
                     info.format_info()->bytes_per_block();
    if (info.is_tiled)
      slice = (slice + 4095) & ~uint64_t(4095);
    uint64_t depth = info.depth + 1;
    if (info.is_tiled && info.dimension == xenos::DataDimension::k3D)
      depth = (depth + 3) & ~uint64_t(3);
    uint64_t size = slice * depth;
    if (!size || size > UINT32_MAX ||
        uint64_t(address) + size > 0x20000000ull) {
      error = "texture source exceeds physical memory";
      return false;
    }
    ranges.push_back({address, uint32_t(size)});
  }
  result = std::move(ranges);
  return true;
}
bool DecodeTextureLayout(std::span<const uint32_t, 6> words,
                         const CapturedMemory &memory, LinearTexture &out,
                         std::string &error) {
  return DecodeTextureLayoutUsing(
      words, [&](uint32_t a, uint32_t n) { return memory.Read(a, n); }, out,
      error);
}
} // namespace superman_returns::graphics::guest
