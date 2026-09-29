// Shader container header (shader_container.h) and offline shader pack
// (shader_pack.h, written by tools/shaders/pack_shaders.py).

#include <cstdint>
#include <cstring>
#include <vector>

#include "shader_container.h"
#include "shader_pack.h"
#include "test_main.h"

namespace {

using superman_returns::native::kMaxShaderContainerBytes;
using superman_returns::native::ParseShaderContainerHeader;
using superman_returns::native::ShaderContainerHeader;
using superman_returns::native::ShaderPackEntry;
using superman_returns::native::ShaderPackView;

std::vector<uint8_t> ContainerHeader(uint32_t flags, uint32_t virtual_size, uint32_t physical_size) {
  std::vector<uint8_t> b;
  for (uint32_t v : {flags, virtual_size, physical_size}) {
    b.push_back(uint8_t(v >> 24));
    b.push_back(uint8_t(v >> 16));
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v));
  }
  return b;
}

// Same layout as tools/shaders/pack_shaders.py (little-endian).
struct PackBuilder {
  struct Item {
    uint64_t hash;
    uint32_t stage;
    std::vector<uint8_t> data;
  };
  std::vector<Item> items;  // must be added sorted by (hash, stage)
  std::vector<uint8_t> Build() const {
    std::vector<uint8_t> out(12 + 20 * items.size());
    std::memcpy(out.data(), "CNSH", 4);
    const uint32_t version = 1, count = uint32_t(items.size());
    std::memcpy(out.data() + 4, &version, 4);
    std::memcpy(out.data() + 8, &count, 4);
    uint32_t offset = uint32_t(out.size());
    for (size_t i = 0; i < items.size(); ++i) {
      uint8_t* e = out.data() + 12 + 20 * i;
      const uint32_t size = uint32_t(items[i].data.size());
      std::memcpy(e, &items[i].hash, 8);
      std::memcpy(e + 8, &items[i].stage, 4);
      std::memcpy(e + 12, &offset, 4);
      std::memcpy(e + 16, &size, 4);
      offset += size;
    }
    for (const Item& it : items) out.insert(out.end(), it.data.begin(), it.data.end());
    return out;
  }
};

}  // namespace

SR_TEST(container_header_pixel_and_vertex) {
  ShaderContainerHeader h;
  auto ps = ContainerHeader(0x102A1100, 0x80, 0x40);
  ps.resize(0xC0);
  SR_CHECK(ParseShaderContainerHeader(ps.data(), ps.size(), h));
  SR_CHECK(!h.is_vertex);
  SR_CHECK_EQ(h.total_size(), size_t(0xC0));
  auto vs = ContainerHeader(0x102A1101, 0x80, 0x40);
  vs.resize(0xC0);
  SR_CHECK(ParseShaderContainerHeader(vs.data(), vs.size(), h));
  SR_CHECK(h.is_vertex);
}

SR_TEST(container_header_rejects_garbage) {
  ShaderContainerHeader h;
  auto bad_magic = ContainerHeader(0x102A1200, 0x80, 0x40);
  bad_magic.resize(0xC0);
  SR_CHECK(!ParseShaderContainerHeader(bad_magic.data(), bad_magic.size(), h));
  auto zero = ContainerHeader(0x102A1100, 0, 0x40);
  zero.resize(0x40);
  SR_CHECK(!ParseShaderContainerHeader(zero.data(), zero.size(), h));
  auto truncated = ContainerHeader(0x102A1100, 0x80, 0x40);
  SR_CHECK(!ParseShaderContainerHeader(truncated.data(), truncated.size(), h));
  auto huge = ContainerHeader(0x102A1100, uint32_t(kMaxShaderContainerBytes), 4);
  SR_CHECK(!ParseShaderContainerHeader(huge.data(), SIZE_MAX, h));
  SR_CHECK(!ParseShaderContainerHeader(nullptr, 100, h));
  SR_CHECK(!ParseShaderContainerHeader(bad_magic.data(), 8, h));
}

SR_TEST(shader_pack_lookup) {
  PackBuilder b;
  b.items = {{0x10, 0, {1, 2, 3}}, {0x10, 1, {4}}, {0x20, 1, {5, 6}}, {0xFFFFFFFFFFFFFFFFull, 0, {7}}};
  const auto pack = b.Build();
  ShaderPackView view;
  SR_CHECK(view.Parse(pack.data(), pack.size()));
  SR_CHECK_EQ(view.count(), 4u);
  ShaderPackEntry e;
  SR_CHECK(view.Find(0x10, true, e));
  SR_CHECK_EQ(e.size, 3u);
  SR_CHECK_EQ(view.base()[e.offset], 1);
  SR_CHECK(view.Find(0x10, false, e));
  SR_CHECK_EQ(view.base()[e.offset], 4);
  SR_CHECK(view.Find(0x20, false, e));
  SR_CHECK_EQ(view.base()[e.offset + 1], 6);
  SR_CHECK(!view.Find(0x20, true, e));   // PS only
  SR_CHECK(!view.Find(0x15, false, e));  // between entries
  SR_CHECK(view.Find(0xFFFFFFFFFFFFFFFFull, true, e));
  SR_CHECK_EQ(view.base()[e.offset], 7);
}

SR_TEST(shader_pack_rejects_bad_tables) {
  PackBuilder b;
  b.items = {{0x10, 0, {1, 2, 3}}};
  auto pack = b.Build();
  ShaderPackView view;
  auto bad_magic = pack;
  bad_magic[0] = 'X';
  SR_CHECK(!view.Parse(bad_magic.data(), bad_magic.size()));
  SR_CHECK_EQ(view.count(), 0u);
  auto bad_version = pack;
  bad_version[4] = 2;
  SR_CHECK(!view.Parse(bad_version.data(), bad_version.size()));
  // Count larger than the table.
  SR_CHECK(!view.Parse(pack.data(), 12 + 19));
  // An entry pointing past the end is treated as missing.
  auto bad_offset = pack;
  const uint32_t far = 0x10000;
  std::memcpy(bad_offset.data() + 12 + 12, &far, 4);
  SR_CHECK(view.Parse(bad_offset.data(), bad_offset.size()));
  ShaderPackEntry e;
  SR_CHECK(!view.Find(0x10, true, e));
  SR_CHECK(!view.Parse(nullptr, 0));
}

SR_TEST(empty_shader_pack) {
  PackBuilder b;
  const auto pack = b.Build();
  ShaderPackView view;
  SR_CHECK(view.Parse(pack.data(), pack.size()));
  ShaderPackEntry e;
  SR_CHECK(!view.Find(0, true, e));
}
