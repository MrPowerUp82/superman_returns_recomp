// Ported from crazyriddler/rexglue-native-kit @136bc6c4,
// reference/conan/port/src/native/texture_decode.cpp (Conan native renderer).
// The kit ships no license file at that revision; parts derived from Xenia /
// ReXGlue keep their BSD license. Changes for Superman Returns are listed in
// docs/native-port-plan.md section 2.
//
#include "texture_decode.h"
#include "../graphics/guest/texture_layout.h"
#include "xenos_tiling.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/xenos.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

namespace superman_returns::native {

namespace xenos = rex::graphics::xenos;
using rex::graphics::FormatInfo;
using rex::graphics::TextureExtent;
using rex::graphics::TextureInfo;

namespace {

struct HostFormat {
  DXGI_FORMAT format;
  // Guest data needs per-block conversion before upload (unsupported for now).
  bool supported;
};

HostFormat MapFormat(xenos::TextureFormat f) {
  using TF = xenos::TextureFormat;
  switch (f) {
    case TF::k_8:
    case TF::k_8_A:
    case TF::k_8_B:
      return {DXGI_FORMAT_R8_UNORM, true};
    case TF::k_8_8:
      return {DXGI_FORMAT_R8G8_UNORM, true};
    case TF::k_8_8_8_8:
    case TF::k_8_8_8_8_A:
      return {DXGI_FORMAT_R8G8B8A8_UNORM, true};
    case TF::k_2_10_10_10:
      return {DXGI_FORMAT_R10G10B10A2_UNORM, true};
    case TF::k_5_6_5:
      return {DXGI_FORMAT_B5G6R5_UNORM, true};
    case TF::k_1_5_5_5:
      return {DXGI_FORMAT_B5G5R5A1_UNORM, true};
    case TF::k_4_4_4_4:
      return {DXGI_FORMAT_B4G4R4A4_UNORM, true};
    case TF::k_DXT1:
      return {DXGI_FORMAT_BC1_UNORM, true};
    case TF::k_DXT2_3:
      return {DXGI_FORMAT_BC2_UNORM, true};
    case TF::k_DXT4_5:
      return {DXGI_FORMAT_BC3_UNORM, true};
    case TF::k_DXN:
      return {DXGI_FORMAT_BC5_UNORM, true};
    case TF::k_16:
      return {DXGI_FORMAT_R16_UNORM, true};
    case TF::k_16_16:
      return {DXGI_FORMAT_R16G16_UNORM, true};
    case TF::k_16_16_16_16:
      return {DXGI_FORMAT_R16G16B16A16_UNORM, true};
    // _EXPAND formats hold 16-bit floats (Xenia maps them the same way).
    case TF::k_16_EXPAND:
      return {DXGI_FORMAT_R16_FLOAT, true};
    case TF::k_16_16_EXPAND:
      return {DXGI_FORMAT_R16G16_FLOAT, true};
    case TF::k_16_16_16_16_EXPAND:
      return {DXGI_FORMAT_R16G16B16A16_FLOAT, true};
    case TF::k_16_FLOAT:
      return {DXGI_FORMAT_R16_FLOAT, true};
    case TF::k_16_16_FLOAT:
      return {DXGI_FORMAT_R16G16_FLOAT, true};
    case TF::k_16_16_16_16_FLOAT:
      return {DXGI_FORMAT_R16G16B16A16_FLOAT, true};
    case TF::k_32_FLOAT:
      return {DXGI_FORMAT_R32_FLOAT, true};
    case TF::k_32_32_FLOAT:
      return {DXGI_FORMAT_R32G32_FLOAT, true};
    case TF::k_32_32_32_32_FLOAT:
      return {DXGI_FORMAT_R32G32B32A32_FLOAT, true};
    case TF::k_24_8:
    case TF::k_24_8_FLOAT:
      // Depth textures sampled by the game (shadow maps) - bit layout differs
      // from D24S8; handled via resolve targets, not guest memory, for now.
      return {DXGI_FORMAT_R32_UINT, false};
    default:
      return {DXGI_FORMAT_UNKNOWN, false};
  }
}

}  // namespace

bool GetTextureBaseRange(const uint32_t fetch_dwords[6], uint32_t& base, uint32_t& size) {
  xenos::xe_gpu_texture_fetch_t fetch;
  std::memcpy(&fetch, fetch_dwords, sizeof(fetch));
  if (fetch.type != xenos::FetchConstantType::kTexture) return false;
  TextureInfo info;
  if (!TextureInfo::Prepare(fetch, &info)) return false;
  const FormatInfo* fi = info.format_info();
  TextureExtent src_extent = info.GetMipExtent(0, true);
  uint32_t slice = src_extent.block_pitch_h * src_extent.block_pitch_v * fi->bytes_per_block();
  if (info.is_tiled) slice = (slice + 4095) & ~4095u;
  base = info.memory.base_address;
  size = slice * (info.depth + 1);
  return base && size;
}

bool DecodeTexture(const uint32_t fetch_dwords[6], DecodedTexture& out, const char** reason) {
  graphics::guest::LinearTexture linear;
  std::string error;
  auto* memory = REX_KERNEL_MEMORY();
  if (!graphics::guest::DecodeTextureLayoutUsing(std::span<const uint32_t,6>(fetch_dwords,6),
        [memory](uint32_t address,uint32_t size) -> std::span<const uint8_t> {
          return {memory->TranslatePhysical<const uint8_t*>(address-0xa0000000u),size};
        },linear,error)) {
    // Preserve the adapter's legacy const-char diagnostics lifetime.
    thread_local std::string last_error;
    last_error=std::move(error);
    if(reason) *reason=last_error.c_str();
    return false;
  }
  DecodedTexture decoded;
  decoded.width=linear.width; decoded.height=linear.height; decoded.depth=linear.depth;
  decoded.mip_levels=linear.mip_levels; decoded.dimension=linear.dimension;
  decoded.guest_format=linear.guest_format; decoded.base_address=linear.base_address;
  decoded.base_size=linear.base_size;
  decoded.format=MapFormat(static_cast<xenos::TextureFormat>(linear.guest_format)).format;
  uint32_t mapping=1u<<12;
  for(uint32_t i=0;i<4;++i) mapping|=linear.swizzle[i]<<(3*i);
  decoded.component_mapping=mapping;
  for(const auto& l:linear.levels) decoded.levels.push_back({l.width,l.height,l.row_pitch,l.rows,l.offset});
  decoded.data=std::move(linear.data);
  out=std::move(decoded);
  return true;
}

bool WriteDds(const DecodedTexture& tex, const char* path) {
  std::FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  uint32_t header[32] = {};
  header[0] = 124;                           // dwSize
  header[1] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000;  // CAPS|HEIGHT|WIDTH|PIXELFORMAT|MIPMAPCOUNT
  header[2] = tex.height;
  header[3] = tex.width;
  header[6] = tex.mip_levels;
  header[18] = 32;                           // ddspf.dwSize
  header[19] = 0x4;                          // DDPF_FOURCC
  header[20] = '0' << 24 | '1' << 16 | 'X' << 8 | 'D';  // "DX10"
  header[26] = 0x1000;                       // DDSCAPS_TEXTURE
  uint32_t dx10[5] = {uint32_t(tex.format), 3 /*TEXTURE2D*/, 0, tex.depth, 0};
  std::fwrite("DDS ", 1, 4, f);
  std::fwrite(header + 1 - 1, 4, 31, f);
  std::fwrite(dx10, 4, 5, f);
  // DDS order: slice-major, then mips. Our levels are mip-major (slice inner).
  for (uint32_t slice = 0; slice < tex.depth; ++slice) {
    for (uint32_t mip = 0; mip < tex.mip_levels; ++mip) {
      const auto& l = tex.levels[size_t(mip) * tex.depth + slice];
      std::fwrite(tex.data.data() + l.offset, 1, size_t(l.row_pitch) * l.rows, f);
    }
  }
  std::fclose(f);
  return true;
}

}  // namespace superman_returns::native
