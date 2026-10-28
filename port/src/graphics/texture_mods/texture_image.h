#pragma once
// Texture mods: decoded guest textures <-> RGBA8 images and PNG files
// (docs/superpowers/specs/2026-10-28-texture-replacement-kingdom-come-design.md).
#include "../guest/texture_layout.h"
#include <cstdint>
#include <filesystem>
#include <vector>

namespace superman_returns::graphics::texture_mods {

// RGBA8, rows contiguous.
struct Image {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgba;
};

// Stable across runs and backends: decoded content plus shape, never the
// guest address it was loaded from.
uint64_t TextureKey(const guest::LinearTexture& texture);

// Level 0, slice 0 as RGBA8, channels as the GPU reads the original format
// (before the fetch swizzle, which keeps applying to a replacement).
// BC1/2/3/5, RGBA8, BGR565, BGR5A1, BGRA4, R8, RG8; other formats -> false.
bool BaseLevelToImage(const guest::LinearTexture& texture, Image& out);

// Replaces the content with `image` as RGBA8 with a box-filtered mip chain of
// the original length. Keeps swizzle and the guest identity fields.
void ImageToTexture(const Image& image, guest::LinearTexture& inout);

// Uncompressed PNG (stored deflate blocks).
bool WritePng(const std::filesystem::path& path, const Image& image);
// Any PNG, forced to 4 channels.
bool ReadPng(const std::filesystem::path& path, Image& out);

}  // namespace superman_returns::graphics::texture_mods
