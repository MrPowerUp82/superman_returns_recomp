#include "../../port/src/graphics/guest/texture_capture.h"
#include "../../port/src/graphics/guest/texture_layout.h"
#include "../../port/src/native_renderer/xenos_tiling.h"
#include "../../port/src/native_renderer/texture_content.h"
#include "test_main.h"
#include <algorithm>
#include <cstring>
#include <rex/graphics/xenos.h>
using namespace superman_returns::graphics::guest;
namespace xenos = rex::graphics::xenos;
namespace {
std::array<uint32_t, 6> Fetch(uint32_t width, uint32_t height,
                              bool tiled = false) {
  xenos::xe_gpu_texture_fetch_t f{};
  f.type = xenos::FetchConstantType::kTexture;
  f.dimension = xenos::DataDimension::k2DOrStacked;
  f.format = xenos::TextureFormat::k_8_8_8_8;
  f.size_2d.width = width - 1;
  f.size_2d.height = height - 1;
  f.pitch = (width + 31) / 32;
  f.tiled = tiled;
  f.base_address = 1;
  f.swizzle = 0 | (1 << 3) | (2 << 6) | (3 << 9);
  std::array<uint32_t, 6> words{};
  std::memcpy(words.data(), &f, sizeof(f));
  return words;
}
CapturedMemory Capture(uint32_t physical, std::vector<uint8_t> bytes) {
  WorkBatch b;
  b.bytes.assign(bytes.begin(), bytes.end());
  b.ranges = {{0xa0000000 + physical, uint32_t(bytes.size()), 0}};
  WorkCmd cmd;
  cmd.range_count = 1;
  CapturedMemory m;
  std::string error;
  SR_CHECK(CapturedMemory::Capture(b, cmd, m, error));
  return m;
}
} // namespace
SR_TEST(linear_layout_owns_pixels_and_swizzle) {
  auto f = Fetch(2, 2);
  std::vector<uint8_t> bytes(8192);
  for (uint32_t y = 0; y < 2; ++y)
    for (uint32_t x = 0; x < 8; ++x)
      bytes[y * 256 + x] = uint8_t(10 + y * 8 + x);
  LinearTexture out;
  std::string error;
  SR_CHECK(DecodeTextureLayout(f, Capture(4096, bytes), out, error));
  SR_CHECK_EQ(out.width, 2u);
  SR_CHECK_EQ(out.levels.size(), 1u);
  SR_CHECK_EQ(out.data[8], 18u);
  SR_CHECK_EQ(out.swizzle[3], 3u);
  SR_CHECK(out.format == LinearFormat::kRGBA8Unorm);
}
SR_TEST(tiled_layout_uses_existing_xenos_addressing) {
  auto f = Fetch(32, 32, true);
  std::vector<uint8_t> bytes(4096);
  for (uint32_t y = 0; y < 32; ++y)
    for (uint32_t x = 0; x < 32; ++x) {
      auto offset = superman_returns::native::TiledAddress2D(x, y, 1, 2);
      bytes[offset] = uint8_t(x);
      bytes[offset + 1] = uint8_t(y);
      bytes[offset + 3] = 255;
    }
  LinearTexture out;
  std::string error;
  SR_CHECK(DecodeTextureLayout(f, Capture(4096, bytes), out, error));
  for (uint32_t y = 0; y < 32; ++y)
    for (uint32_t x = 0; x < 32; ++x) {
      SR_CHECK_EQ(out.data[(y * 32 + x) * 4], x);
      SR_CHECK_EQ(out.data[(y * 32 + x) * 4 + 1], y);
    }
}
SR_TEST(texture_capture_truncation_is_transactional) {
  auto f = Fetch(32, 32, true);
  LinearTexture out;
  out.width = 99;
  std::string error;
  SR_CHECK(!DecodeTextureLayout(f, Capture(4096, {1, 2, 3}), out, error));
  SR_CHECK(!error.empty());
  SR_CHECK_EQ(out.width, 99u);
}

SR_TEST(texture_snapshot_survives_guest_change_and_command_reuse) {
  auto words = Fetch(32, 32, true);
  std::vector<uint8_t> bytes(4096, 7);
  std::shared_ptr<const TextureCapture> first, second;
  std::string error;
  auto read = [&](uint32_t a, uint32_t n) -> std::span<const uint8_t> {
    return a == 0xa0001000 && n == bytes.size()
               ? std::span<const uint8_t>(bytes)
               : std::span<const uint8_t>{};
  };
  SR_CHECK(CaptureTexture(words, 1, read, first, error));
  std::fill(bytes.begin(), bytes.end(), 9);
  SR_CHECK(CaptureTexture(words, 2, read, second, error));
  bytes.clear();
  SR_CHECK_EQ(first->version, 1u);
  SR_CHECK_EQ(second->version, 2u);
  SR_CHECK_EQ(first->memory.Read(0xa0001000, 4096)[0], 7u);
  SR_CHECK_EQ(second->memory.Read(0xa0001000, 4096)[0], 9u);
}

SR_TEST(texture_physical_alias_matches_sdk_translation) {
  auto words=Fetch(32,32,true);
  xenos::xe_gpu_texture_fetch_t f{};
  std::memcpy(&f,words.data(),sizeof(f));
  f.base_address=0xe0001;
  std::memcpy(words.data(),&f,sizeof(f));
  LinearTexture out;std::string error;
  SR_CHECK(DecodeTextureLayout(words,Capture(4096,std::vector<uint8_t>(4096,7)),out,error));
  SR_CHECK_EQ(out.data.size(),4096u);
  if(!out.data.empty()) SR_CHECK_EQ(out.data[0],7u);
}

SR_TEST(cube_layout_keeps_six_distinct_faces) {
  auto words = Fetch(32, 32, true);
  xenos::xe_gpu_texture_fetch_t f{};
  std::memcpy(&f, words.data(), sizeof(f));
  f.dimension = xenos::DataDimension::kCube;
  f.size_2d.stack_depth = 5;
  std::memcpy(words.data(), &f, sizeof(f));
  std::vector<uint8_t> bytes(6 * 4096);
  for (uint32_t face = 0; face < 6; ++face)
    std::fill_n(bytes.begin() + face * 4096, 4096, uint8_t(face + 1));
  LinearTexture out;
  std::string error;
  SR_CHECK(DecodeTextureLayout(words, Capture(4096, bytes), out, error));
  SR_CHECK_EQ(out.depth, 6u);
  SR_CHECK_EQ(out.levels.size(), 6u);
  for (uint32_t face = 0; face < 6; ++face)
    SR_CHECK_EQ(out.data[out.levels[face].offset], face + 1);
}

SR_TEST(volume_layout_keeps_z_slices_and_tiling) {
  auto words = Fetch(32, 32, true);
  xenos::xe_gpu_texture_fetch_t f{};
  std::memcpy(&f, words.data(), sizeof(f));
  f.dimension = xenos::DataDimension::k3D;
  f.size_3d.width = 31;
  f.size_3d.height = 31;
  f.size_3d.depth = 3;
  std::memcpy(words.data(), &f, sizeof(f));
  std::vector<uint8_t> bytes(4 * 4096);
  for (uint32_t z = 0; z < 4; ++z)
    for (uint32_t y = 0; y < 32; ++y)
      for (uint32_t x = 0; x < 32; ++x) {
        auto a = superman_returns::native::TiledAddress3D(x, y, z, 32, 32, 2);
        bytes[a] = uint8_t(z + 1);
        bytes[a + 1] = uint8_t(y);
        bytes[a + 2] = uint8_t(x);
      }
  LinearTexture out;
  std::string error;
  SR_CHECK(DecodeTextureLayout(words, Capture(4096, bytes), out, error));
  SR_CHECK_EQ(out.levels.size(), 4u);
  for (uint32_t z = 0; z < 4; ++z)
    for (uint32_t y = 0; y < 32; ++y)
      for (uint32_t x = 0; x < 32; ++x) {
        auto a = out.levels[z].offset + (y * 32 + x) * 4;
        SR_CHECK_EQ(out.data[a], z + 1);
        SR_CHECK_EQ(out.data[a + 1], y);
        SR_CHECK_EQ(out.data[a + 2], x);
      }
}

SR_TEST(texture_swizzle_constants_and_block_endian) {
  auto words = Fetch(32, 32, true);
  xenos::xe_gpu_texture_fetch_t f{};
  std::memcpy(&f, words.data(), sizeof(f));
  f.swizzle = 4 | (5 << 3) | (0 << 6) | (1 << 9);
  f.endianness = xenos::Endian::k8in32;
  std::memcpy(words.data(), &f, sizeof(f));
  std::vector<uint8_t> bytes(4096);
  bytes[0] = 1;
  bytes[1] = 2;
  bytes[2] = 3;
  bytes[3] = 4;
  LinearTexture out;
  std::string error;
  SR_CHECK(DecodeTextureLayout(words, Capture(4096, bytes), out, error));
  SR_CHECK((out.swizzle == std::array<uint32_t, 4>({4, 5, 0, 1})));
  SR_CHECK_EQ(out.data[0], 4u);
  SR_CHECK_EQ(out.data[3], 1u);
}

SR_TEST(packed_mips_keep_distinct_small_levels) {
  auto words = Fetch(64, 64, true);
  xenos::xe_gpu_texture_fetch_t f{};
  std::memcpy(&f, words.data(), sizeof(f));
  f.mip_address = 0x100;
  f.mip_max_level = 3;
  f.packed_mips = 1;
  std::memcpy(words.data(), &f, sizeof(f));
  WorkBatch batch;
  batch.bytes.resize(16384 + 8192, 0);
  batch.ranges = {{0xa0001000, 16384, 0}, {0xa0100000, 8192, 16384}};
  for (uint32_t y = 0; y < 32; ++y)
    for (uint32_t x = 0; x < 32; ++x) {
      auto a = superman_returns::native::TiledAddress2D(x, y, 1, 2);
      batch.bytes[16384 + a] = 2;
    }
  for (uint32_t mip = 2; mip <= 3; ++mip) {
    uint32_t size = 64 >> mip, offset_x = 16 >> (mip - 2);
    for (uint32_t y = 0; y < size; ++y)
      for (uint32_t x = 0; x < size; ++x) {
        auto a =
            superman_returns::native::TiledAddress2D(x + offset_x, y, 1, 2);
        batch.bytes[16384 + 4096 + a] = uint8_t(mip + 1);
      }
  }
  WorkCmd cmd;
  cmd.range_count = 2;
  CapturedMemory memory;
  std::string error;
  SR_CHECK(CapturedMemory::Capture(batch, cmd, memory, error));
  LinearTexture out;
  uint64_t raw_bytes = 0;
  uint32_t raw_reads = 0;
  SR_CHECK(DecodeTextureLayoutUsing(words, [&](uint32_t address, uint32_t size) {
    ++raw_reads;
    raw_bytes += size;
    return memory.Read(address, size);
  }, out, error));
  SR_CHECK_EQ(out.levels.size(), 4u);
  for (uint32_t mip = 1; mip <= 3; ++mip) {
    SR_CHECK_EQ(out.data[out.levels[mip].offset], mip + 1);
    SR_CHECK_EQ(out.levels[mip].width, 64u >> mip);
  }
  uint64_t copied_bytes = 0;
  uint32_t reads = 0;
  std::shared_ptr<const TextureCapture> capture;
  SR_CHECK(CaptureTexture(words, 7, [&](uint32_t address, uint32_t size) {
    ++reads;
    copied_bytes += size;
    return memory.Read(address, size);
  }, capture, error));
  if (!capture) return;
  SR_CHECK_EQ(reads, 2u);
  SR_CHECK_EQ(copied_bytes, 24576u); // Base + union of the three mip requests.
  SR_CHECK_EQ(raw_reads, 4u);
  SR_CHECK(copied_bytes < raw_bytes);
  std::printf("packed mip capture: raw_reads=%u raw_bytes=%llu union_reads=%u union_bytes=%llu\n",
              raw_reads, static_cast<unsigned long long>(raw_bytes), reads,
              static_cast<unsigned long long>(copied_bytes));
  LinearTexture coalesced;
  SR_CHECK(DecodeTextureLayout(words, capture->memory, coalesced, error));
  SR_CHECK(coalesced.data == out.data);
  SR_CHECK_EQ(coalesced.levels.size(), out.levels.size());
}

SR_TEST(coalesced_texture_capture_matches_live_decode_for_cube_volume_and_linear_mips) {
  for (uint32_t variant = 0; variant < 3; ++variant) {
    auto words = Fetch(64, 64, variant != 2);
    xenos::xe_gpu_texture_fetch_t fetch{};
    std::memcpy(&fetch, words.data(), sizeof(fetch));
    fetch.mip_address = 0x100;
    fetch.mip_max_level = 3;
    fetch.packed_mips = 1;
    if (variant == 0) {
      fetch.dimension = xenos::DataDimension::kCube;
      fetch.size_2d.stack_depth = 5;
    } else if (variant == 1) {
      fetch.dimension = xenos::DataDimension::k3D;
      fetch.size_3d.width = 31;
      fetch.size_3d.height = 31;
      fetch.size_3d.depth = 4;
    }
    std::memcpy(words.data(), &fetch, sizeof(fetch));
    std::vector<uint8_t> source(2u << 20);
    for (size_t i = 0; i < source.size(); ++i)
      source[i] = uint8_t(i ^ (i >> 5) ^ (i >> 12));
    auto read = [&](uint32_t address, uint32_t size) -> std::span<const uint8_t> {
      const uint32_t physical = address & 0x1fffffffu;
      if (uint64_t(physical) + size > source.size()) return {};
      return std::span(source).subspan(physical, size);
    };
    LinearTexture direct, captured;
    std::string error;
    SR_CHECK(DecodeTextureLayoutUsing(words, read, direct, error));
    std::shared_ptr<const TextureCapture> snapshot;
    SR_CHECK(CaptureTexture(words, 1, read, snapshot, error));
    if (!snapshot) continue;
    std::fill(source.begin(), source.end(), 0); // Decoder must use owned bytes.
    SR_CHECK(DecodeTextureLayout(words, snapshot->memory, captured, error));
    SR_CHECK(captured.data == direct.data);
    SR_CHECK_EQ(captured.levels.size(), direct.levels.size());
    SR_CHECK_EQ(captured.depth, direct.depth);
    SR_CHECK_EQ(captured.mip_levels, direct.mip_levels);
    uint64_t previous_end = 0;
    for (const auto& range : snapshot->ranges) {
      SR_CHECK(range.address >= previous_end);
      previous_end = uint64_t(range.address) + range.length;
    }
  }
}

SR_TEST(texture_validation_detects_changes_in_sdk_described_packed_mips) {
  auto words = Fetch(64, 64, true);
  xenos::xe_gpu_texture_fetch_t fetch{};
  std::memcpy(&fetch, words.data(), sizeof(fetch));
  fetch.mip_address = 0x100;
  fetch.mip_max_level = 3;
  fetch.packed_mips = 1;
  std::memcpy(words.data(), &fetch, sizeof(fetch));
  std::vector<TextureRange> ranges;
  std::string error;
  SR_CHECK(DescribeTextureRanges(words, ranges, error));
  SR_CHECK_EQ(ranges.size(), 2u);
  if (ranges.size() != 2) return;
  uint64_t end = 0;
  for (const auto& range : ranges) end = std::max(end, uint64_t(range.address) + range.length);
  std::vector<uint8_t> memory(size_t(end), 0);
  auto read = [&](uint32_t address) { return memory.data() + address; };
  auto hash = [](const uint8_t* data, size_t size, uint64_t seed) {
    for (size_t i = 0; i < size; ++i) seed = (seed ^ data[i]) * 1099511628211ull;
    return seed;
  };
  using superman_returns::native::HashTextureContent;
  using superman_returns::native::TextureContentWritten;
  const auto before = HashTextureContent(ranges, read, hash);
  const auto base_before = HashTextureContent(std::span(ranges).first(1), read, hash);
  const auto mip_address = ranges.back().address;
  SR_CHECK(mip_address >= ranges.front().address + ranges.front().length);
  memory[mip_address] = 0xff;
  SR_CHECK(HashTextureContent(ranges, read, hash) != before);
  SR_CHECK_EQ(HashTextureContent(std::span(ranges).first(1), read, hash), base_before);
  auto written = [=](uint32_t address, uint32_t size, uint32_t) {
    return address <= mip_address && uint64_t(address) + size > mip_address;
  };
  SR_CHECK(TextureContentWritten(ranges, 1, written));
  SR_CHECK(!TextureContentWritten(std::span(ranges).first(1), 1, written));
}

SR_TEST(compressed_blocks_keep_payload_and_endian) {
  for (auto format : {xenos::TextureFormat::k_DXT1, xenos::TextureFormat::k_DXN}) {
    auto words = Fetch(32, 32, true);
    xenos::xe_gpu_texture_fetch_t f{};
    std::memcpy(&f, words.data(), sizeof(f));
    f.format = format;
    f.endianness = xenos::Endian::k8in16;
    std::memcpy(words.data(), &f, sizeof(f));
    std::string error;
    std::vector<TextureRange> ranges;
    SR_CHECK(DescribeTextureRanges(words, ranges, error));
    if (ranges.empty()) continue;
    const uint32_t block_bytes = format == xenos::TextureFormat::k_DXT1 ? 8 : 16;
    std::vector<uint8_t> bytes(ranges[0].length);
    for (uint32_t y=0;y<8;++y) for (uint32_t x=0;x<8;++x) {
      auto offset=superman_returns::native::TiledAddress2D(x,y,1,block_bytes==8?3:4);
      for(uint32_t b=0;b<block_bytes;++b) bytes[offset+b]=uint8_t(x+y*8+b);
    }
    LinearTexture out;
    SR_CHECK(DecodeTextureLayout(words,Capture(4096,bytes),out,error));
    SR_CHECK_EQ(out.levels.size(),1u);
    SR_CHECK_EQ(out.data.size(),64u*block_bytes);
    if(out.data.size()!=64u*block_bytes) continue;
    for(uint32_t y=0;y<8;++y) for(uint32_t x=0;x<8;++x)
      for(uint32_t b=0;b<block_bytes;++b)
        SR_CHECK_EQ(out.data[(y*8+x)*block_bytes+b],uint8_t(x+y*8+(b^1)));
    SR_CHECK(out.format==(block_bytes==8?LinearFormat::kBC1:LinearFormat::kBC5));
  }
}
SR_TEST(texture_16in32_swaps_exact_bytes_without_overwriting_destination) {
  auto words=Fetch(32,32,true);xenos::xe_gpu_texture_fetch_t f{};
  std::memcpy(&f,words.data(),sizeof(f));f.endianness=xenos::Endian::k16in32;std::memcpy(words.data(),&f,sizeof(f));
  std::vector<uint8_t> bytes(4096);for(size_t i=0;i<bytes.size();++i) bytes[i]=uint8_t(i);
  LinearTexture out;std::string error;
  SR_CHECK(DecodeTextureLayout(words,Capture(4096,bytes),out,error));
  if(out.data.size()==4096) {SR_CHECK_EQ(out.data[0],2u);SR_CHECK_EQ(out.data[1],3u);SR_CHECK_EQ(out.data[2],0u);SR_CHECK_EQ(out.data[3],1u);}
}
