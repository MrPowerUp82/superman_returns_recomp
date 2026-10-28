#include "texture_image.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>

#define XXH_INLINE_ALL
#include <xxhash.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb/stb_image.h>

namespace superman_returns::graphics::texture_mods {
namespace {
using guest::LinearFormat;

uint8_t Expand5(uint32_t v) { return uint8_t((v << 3) | (v >> 2)); }
uint8_t Expand6(uint32_t v) { return uint8_t((v << 2) | (v >> 4)); }
uint8_t Expand4(uint32_t v) { return uint8_t(v * 17); }
uint16_t Load16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

void Rgb565(uint16_t c, uint8_t out[3]) {
  out[0] = Expand5(c >> 11);
  out[1] = Expand6((c >> 5) & 63);
  out[2] = Expand5(c & 31);
}

// Writes the 4x4 block's colors into `block` (16 RGBA texels).
void DecodeBc1Colors(const uint8_t* src, uint8_t block[16][4], bool allow_punch_through) {
  const uint16_t c0 = Load16(src), c1 = Load16(src + 2);
  uint8_t palette[4][4]{};
  Rgb565(c0, palette[0]);
  Rgb565(c1, palette[1]);
  palette[0][3] = palette[1][3] = 255;
  if (c0 > c1 || !allow_punch_through) {
    for (int ch = 0; ch < 3; ++ch) {
      palette[2][ch] = uint8_t((2 * palette[0][ch] + palette[1][ch]) / 3);
      palette[3][ch] = uint8_t((palette[0][ch] + 2 * palette[1][ch]) / 3);
    }
    palette[2][3] = palette[3][3] = 255;
  } else {
    for (int ch = 0; ch < 3; ++ch) palette[2][ch] = uint8_t((palette[0][ch] + palette[1][ch]) / 2);
    palette[2][3] = 255;
    palette[3][3] = 0;  // transparent black
  }
  const uint32_t indices = uint32_t(src[4]) | (uint32_t(src[5]) << 8) | (uint32_t(src[6]) << 16) |
                           (uint32_t(src[7]) << 24);
  for (int i = 0; i < 16; ++i) std::memcpy(block[i], palette[(indices >> (2 * i)) & 3], 4);
}

// BC3 alpha / BC5 channel block: 8 bytes -> 16 values.
void DecodeInterpolated(const uint8_t* src, uint8_t out[16]) {
  const uint32_t a0 = src[0], a1 = src[1];
  uint32_t values[8] = {a0, a1};
  if (a0 > a1) {
    for (int i = 1; i < 7; ++i) values[i + 1] = ((7 - i) * a0 + i * a1) / 7;
  } else {
    for (int i = 1; i < 5; ++i) values[i + 1] = ((5 - i) * a0 + i * a1) / 5;
    values[6] = 0;
    values[7] = 255;
  }
  uint64_t bits = 0;
  for (int i = 0; i < 6; ++i) bits |= uint64_t(src[2 + i]) << (8 * i);
  for (int i = 0; i < 16; ++i) out[i] = uint8_t(values[(bits >> (3 * i)) & 7]);
}

bool DecodeBlockLevel(const guest::LinearTexture& t, const guest::LinearTexture::Level& level,
                      Image& out) {
  const size_t block_bytes = t.format == LinearFormat::kBC1 ? 8 : 16;
  const uint32_t blocks_x = (level.width + 3) / 4, blocks_y = (level.height + 3) / 4;
  if (level.row_pitch < blocks_x * block_bytes || level.rows < blocks_y ||
      level.offset + size_t(level.row_pitch) * blocks_y > t.data.size())
    return false;
  for (uint32_t by = 0; by < blocks_y; ++by) {
    for (uint32_t bx = 0; bx < blocks_x; ++bx) {
      const uint8_t* src = t.data.data() + level.offset + size_t(by) * level.row_pitch + bx * block_bytes;
      uint8_t block[16][4]{};
      switch (t.format) {
        case LinearFormat::kBC1:
          DecodeBc1Colors(src, block, true);
          break;
        case LinearFormat::kBC2:
          DecodeBc1Colors(src + 8, block, false);
          for (int i = 0; i < 16; ++i) block[i][3] = Expand4((src[i / 2] >> (4 * (i & 1))) & 15);
          break;
        case LinearFormat::kBC3: {
          DecodeBc1Colors(src + 8, block, false);
          uint8_t alpha[16];
          DecodeInterpolated(src, alpha);
          for (int i = 0; i < 16; ++i) block[i][3] = alpha[i];
          break;
        }
        case LinearFormat::kBC5: {
          uint8_t red[16], green[16];
          DecodeInterpolated(src, red);
          DecodeInterpolated(src + 8, green);
          for (int i = 0; i < 16; ++i) {
            block[i][0] = red[i]; block[i][1] = green[i]; block[i][2] = 0; block[i][3] = 255;
          }
          break;
        }
        default:
          return false;
      }
      for (int i = 0; i < 16; ++i) {
        const uint32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
        if (x < out.width && y < out.height) std::memcpy(&out.rgba[(size_t(y) * out.width + x) * 4], block[i], 4);
      }
    }
  }
  return true;
}

bool DecodeTexelLevel(const guest::LinearTexture& t, const guest::LinearTexture::Level& level, Image& out) {
  uint32_t texel = 0;
  switch (t.format) {
    case LinearFormat::kRGBA8Unorm: texel = 4; break;
    case LinearFormat::kBGR565Unorm:
    case LinearFormat::kBGR5A1Unorm:
    case LinearFormat::kBGRA4Unorm:
    case LinearFormat::kRG8Unorm: texel = 2; break;
    case LinearFormat::kR8Unorm: texel = 1; break;
    default: return false;
  }
  if (level.row_pitch < level.width * texel || level.rows < level.height ||
      level.offset + size_t(level.row_pitch) * level.height > t.data.size())
    return false;
  for (uint32_t y = 0; y < level.height; ++y) {
    const uint8_t* row = t.data.data() + level.offset + size_t(y) * level.row_pitch;
    for (uint32_t x = 0; x < level.width; ++x) {
      const uint8_t* s = row + size_t(x) * texel;
      uint8_t* d = &out.rgba[(size_t(y) * out.width + x) * 4];
      const uint16_t v = texel == 2 ? Load16(s) : 0;
      switch (t.format) {
        case LinearFormat::kRGBA8Unorm: std::memcpy(d, s, 4); break;
        // DXGI B5G6R5 / B5G5R5A1 / B4G4R4A4: blue in the low bits.
        case LinearFormat::kBGR565Unorm:
          d[0] = Expand5(v >> 11); d[1] = Expand6((v >> 5) & 63); d[2] = Expand5(v & 31); d[3] = 255; break;
        case LinearFormat::kBGR5A1Unorm:
          d[0] = Expand5((v >> 10) & 31); d[1] = Expand5((v >> 5) & 31); d[2] = Expand5(v & 31);
          d[3] = (v >> 15) ? 255 : 0; break;
        case LinearFormat::kBGRA4Unorm:
          d[0] = Expand4((v >> 8) & 15); d[1] = Expand4((v >> 4) & 15); d[2] = Expand4(v & 15);
          d[3] = Expand4(v >> 12); break;
        case LinearFormat::kRG8Unorm: d[0] = s[0]; d[1] = s[1]; d[2] = 0; d[3] = 255; break;
        case LinearFormat::kR8Unorm: d[0] = s[0]; d[1] = 0; d[2] = 0; d[3] = 255; break;
        default: return false;
      }
    }
  }
  return true;
}

// PNG helpers.
uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0) {
  static const auto table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[n] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}
void PutBe32(std::vector<uint8_t>& out, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(v >> s));
}
void Chunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& body) {
  PutBe32(out, uint32_t(body.size()));
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), body.begin(), body.end());
  PutBe32(out, Crc32(out.data() + start, out.size() - start));
}
}  // namespace

uint64_t TextureKey(const guest::LinearTexture& t) {
  const uint32_t shape[6] = {t.width, t.height, t.depth, t.mip_levels, t.dimension, uint32_t(t.format)};
  const uint64_t seed = XXH3_64bits(shape, sizeof(shape));
  return XXH3_64bits_withSeed(t.data.data(), t.data.size(), seed);
}

bool BaseLevelToImage(const guest::LinearTexture& t, Image& out) {
  if (t.levels.empty() || !t.width || !t.height) return false;
  const auto& level = t.levels[0];
  out.width = level.width;
  out.height = level.height;
  out.rgba.assign(size_t(out.width) * out.height * 4, 0);
  switch (t.format) {
    case LinearFormat::kBC1:
    case LinearFormat::kBC2:
    case LinearFormat::kBC3:
    case LinearFormat::kBC5:
      return DecodeBlockLevel(t, level, out);
    default:
      return DecodeTexelLevel(t, level, out);
  }
}

void ImageToTexture(const Image& image, guest::LinearTexture& t) {
  const uint32_t mips = std::max(1u, t.mip_levels);
  t.format = LinearFormat::kRGBA8Unorm;
  t.width = image.width;
  t.height = image.height;
  t.depth = 1;
  t.levels.clear();
  t.data = image.rgba;
  t.levels.push_back({image.width, image.height, image.width * 4, image.height, 0});
  for (uint32_t m = 1; m < mips; ++m) {
    const auto previous = t.levels.back();
    const uint32_t w = std::max(1u, previous.width / 2), h = std::max(1u, previous.height / 2);
    const size_t offset = t.data.size();
    t.data.resize(offset + size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
      for (uint32_t x = 0; x < w; ++x) {
        for (int c = 0; c < 4; ++c) {
          uint32_t sum = 0;
          for (uint32_t dy = 0; dy < 2; ++dy)
            for (uint32_t dx = 0; dx < 2; ++dx) {
              const uint32_t sx = std::min(x * 2 + dx, previous.width - 1);
              const uint32_t sy = std::min(y * 2 + dy, previous.height - 1);
              sum += t.data[previous.offset + (size_t(sy) * previous.width + sx) * 4 + c];
            }
          t.data[offset + (size_t(y) * w + x) * 4 + c] = uint8_t((sum + 2) / 4);
        }
      }
    }
    t.levels.push_back({w, h, w * 4, h, offset});
  }
  t.mip_levels = uint32_t(t.levels.size());
}

bool WritePng(const std::filesystem::path& path, const Image& image) {
  if (!image.width || !image.height || image.rgba.size() != size_t(image.width) * image.height * 4) return false;
  std::vector<uint8_t> raw;
  raw.reserve(image.rgba.size() + image.height);
  for (uint32_t y = 0; y < image.height; ++y) {
    raw.push_back(0);  // filter: none
    const auto* row = image.rgba.data() + size_t(y) * image.width * 4;
    raw.insert(raw.end(), row, row + size_t(image.width) * 4);
  }
  std::vector<uint8_t> zlib = {0x78, 0x01};
  for (size_t pos = 0; pos < raw.size() || pos == 0;) {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    const bool last = pos + n == raw.size();
    zlib.push_back(last ? 1 : 0);
    zlib.push_back(uint8_t(n)); zlib.push_back(uint8_t(n >> 8));
    zlib.push_back(uint8_t(~n)); zlib.push_back(uint8_t(~n >> 8));
    zlib.insert(zlib.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
    if (last) break;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
  PutBe32(zlib, (b << 16) | a);
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> header;
  PutBe32(header, image.width);
  PutBe32(header, image.height);
  header.insert(header.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA
  Chunk(png, "IHDR", header);
  Chunk(png, "IDAT", zlib);
  Chunk(png, "IEND", {});
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
  return bool(out.flush());
}

bool ReadPng(const std::filesystem::path& path, Image& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (bytes.empty() || bytes.size() > 0x7FFFFFFF) return false;
  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &channels, 4);
  if (!pixels) return false;
  out.width = uint32_t(w);
  out.height = uint32_t(h);
  out.rgba.assign(pixels, pixels + size_t(w) * h * 4);
  stbi_image_free(pixels);
  return true;
}

}  // namespace superman_returns::graphics::texture_mods
