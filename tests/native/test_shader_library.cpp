// Pre-shader library (shader_library.h, written by
// tools/shaders/make_preshaders.py): format checks and how guest containers
// are recognised (exact, with zeroed size words, by microcode).

#include <cstdint>
#include <cstring>
#include <vector>

#include "shader_library.h"
#include "test_main.h"

namespace {

using superman_returns::native::Fnv1a64;
using superman_returns::native::PreShader;
using superman_returns::native::PreShaderMatch;
using superman_returns::native::ShaderLibrary;

void PutBE(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  b[at] = uint8_t(v >> 24);
  b[at + 1] = uint8_t(v >> 16);
  b[at + 2] = uint8_t(v >> 8);
  b[at + 3] = uint8_t(v);
}

// Container of `vsize` virtual bytes (pattern `seed`) and `psize` microcode bytes.
std::vector<uint8_t> Container(bool vertex, uint8_t seed, uint32_t vsize = 0x80,
                               uint32_t psize = 0x30, uint8_t ucode_seed = 0) {
  std::vector<uint8_t> b(vsize + psize);
  for (size_t i = 0; i < vsize; ++i) b[i] = uint8_t(seed + i * 7);
  for (size_t i = vsize; i < b.size(); ++i) b[i] = uint8_t((ucode_seed ? ucode_seed : seed) ^ i);
  PutBE(b, 0, 0x102A1100u | (vertex ? 1 : 0));
  PutBE(b, 4, vsize);
  PutBE(b, 8, psize);
  return b;
}

PreShader Entry(uint64_t hash, std::vector<uint8_t> container, uint8_t dxil_tag) {
  PreShader s;
  s.container_hash = hash;
  s.vertex = (container[3] & 1) != 0;
  s.container = std::move(container);
  s.dxil = {'D', 'X', 'B', 'C', dxil_tag};
  return s;
}

ShaderLibrary Load(const std::vector<uint8_t>& file) {
  ShaderLibrary lib;
  std::string error;
  SR_CHECK(lib.Load(file.data(), file.size(), &error));
  return lib;
}

}  // namespace

SR_TEST(shader_library_fnv_reference_values) {
  // FNV-1a 64 reference values (also used by make_preshaders.py).
  SR_CHECK(Fnv1a64(nullptr, 0) == 0xCBF29CE484222325ull);
  const uint8_t a[] = {'a'};
  SR_CHECK(Fnv1a64(a, 1) == 0xAF63DC4C8601EC8Cull);
}

SR_TEST(shader_library_round_trip_and_find) {
  auto file = ShaderLibrary::Serialize({Entry(0x20, Container(false, 3), 2),
                                        Entry(0x10, Container(true, 1), 1),
                                        Entry(0x10, Container(false, 5), 9)});
  ShaderLibrary lib = Load(file);
  SR_CHECK_EQ(lib.size(), size_t(3));
  const PreShader* vs = lib.Find(0x10, true);
  const PreShader* ps = lib.Find(0x10, false);
  SR_CHECK(vs && vs->vertex && vs->dxil.back() == 1);
  SR_CHECK(ps && !ps->vertex && ps->dxil.back() == 9);
  SR_CHECK(lib.Find(0x20, false) != nullptr);
  SR_CHECK(lib.Find(0x20, true) == nullptr);
  SR_CHECK(lib.Find(0x30, false) == nullptr);
}

SR_TEST(shader_library_marks_instancing_only_in_vertex_metadata) {
  auto vs = Container(true, 1);
  auto ps = Container(false, 2);
  auto ordinary = Container(true, 3);
  const char marker[] = "instance_data";
  std::memcpy(vs.data() + 0x40, marker, sizeof(marker));
  std::memcpy(ps.data() + 0x40, marker, sizeof(marker));
  // A byte sequence in microcode is not a named shader parameter.
  std::memcpy(ordinary.data() + 0x80, marker, sizeof(marker));
  ShaderLibrary lib = Load(ShaderLibrary::Serialize({Entry(1, vs, 1),
      Entry(2, ps, 2), Entry(3, ordinary, 3)}));
  SR_CHECK(lib.Find(1, true)->dynamic_vertex_fetch);
  SR_CHECK(!lib.Find(2, false)->dynamic_vertex_fetch);
  SR_CHECK(!lib.Find(3, true)->dynamic_vertex_fetch);
}

SR_TEST(shader_library_rejects_bad_files) {
  auto good = ShaderLibrary::Serialize({Entry(1, Container(false, 1), 1)});
  ShaderLibrary lib;
  std::string error;
  auto corrupt = good;
  corrupt.back() ^= 0xFF;  // checksum
  SR_CHECK(!lib.Load(corrupt.data(), corrupt.size(), &error));
  auto truncated = good;
  truncated.pop_back();
  SR_CHECK(!lib.Load(truncated.data(), truncated.size(), &error));
  auto magic = good;
  magic[0] = 'X';
  SR_CHECK(!lib.Load(magic.data(), magic.size(), &error));
  // A stage that disagrees with the container flags.
  PreShader wrong = Entry(1, Container(false, 1), 1);
  wrong.vertex = true;
  auto staged = ShaderLibrary::Serialize({wrong});
  SR_CHECK(!lib.Load(staged.data(), staged.size(), &error));
  // A failed load keeps the previous contents.
  SR_CHECK(lib.Load(good.data(), good.size(), &error));
  SR_CHECK(!lib.Load(corrupt.data(), corrupt.size(), &error));
  SR_CHECK_EQ(lib.size(), size_t(1));
}

SR_TEST(shader_library_identifies_exact_containers) {
  auto c = Container(false, 7);
  ShaderLibrary lib = Load(ShaderLibrary::Serialize({Entry(0xAB, c, 1)}));
  std::vector<uint8_t> guest = c;
  guest.resize(c.size() + 256, 0xEE);  // whatever follows in guest memory
  PreShaderMatch how;
  const PreShader* s = lib.Identify(guest.data(), guest.size(), &how);
  SR_CHECK(s && s->container_hash == 0xAB);
  SR_CHECK(how == PreShaderMatch::kExact);
  // Wrong stage flag: no match.
  PutBE(guest, 0, 0x102A1101u);
  SR_CHECK(lib.Identify(guest.data(), guest.size(), &how) == nullptr);
}

SR_TEST(shader_library_identifies_containers_with_zeroed_sizes) {
  // XDK 2.0.3529: the MemStream container has its size words zeroed while the
  // outer loader (sub_820F9C78) runs the creators.
  auto c = Container(true, 9);
  ShaderLibrary lib = Load(ShaderLibrary::Serialize({Entry(0xCD, c, 1)}));
  std::vector<uint8_t> guest = c;
  PutBE(guest, 4, 0);
  PutBE(guest, 8, 0);
  PreShaderMatch how;
  const PreShader* s = lib.Identify(guest.data(), guest.size(), &how);
  SR_CHECK(s && s->container_hash == 0xCD);
  SR_CHECK(how == PreShaderMatch::kBody);
  // One changed byte anywhere in the body: no match.
  guest[guest.size() - 1] ^= 1;
  SR_CHECK(lib.Identify(guest.data(), guest.size(), &how) == nullptr);
  // Fewer readable bytes than the candidate: never read past them.
  guest[guest.size() - 1] ^= 1;
  SR_CHECK(lib.Identify(guest.data(), guest.size() - 1, &how) == nullptr);
}

SR_TEST(shader_library_identifies_by_microcode_when_unique) {
  // Same microcode, different constant tables: ambiguous, refused.
  auto a = Container(false, 1, 0x80, 0x30, 0x55);
  auto b = Container(false, 2, 0x80, 0x30, 0x55);
  auto c = Container(false, 3, 0x80, 0x30, 0x77);
  ShaderLibrary lib =
      Load(ShaderLibrary::Serialize({Entry(1, a, 1), Entry(2, b, 2), Entry(3, c, 3)}));
  // A guest copy whose virtual part was patched in place.
  auto guest_c = c;
  for (size_t i = 16; i < 0x80; ++i) guest_c[i] ^= 0x5A;
  PreShaderMatch how;
  SR_CHECK(lib.Identify(guest_c.data(), guest_c.size(), &how) == nullptr);
  const PreShader* s = lib.IdentifyMicrocode(guest_c.data(), guest_c.size(), false, &how);
  SR_CHECK(s && s->container_hash == 3);
  SR_CHECK(how == PreShaderMatch::kMicrocode);
  SR_CHECK(lib.IdentifyMicrocode(guest_c.data(), guest_c.size(), true, &how) == nullptr);
  auto guest_a = a;
  for (size_t i = 16; i < 0x80; ++i) guest_a[i] ^= 0x5A;
  SR_CHECK(lib.IdentifyMicrocode(guest_a.data(), guest_a.size(), false, &how) == nullptr);
}

SR_TEST(shader_library_file_layout_matches_make_preshaders) {
  // Header bytes as tools/shaders/make_preshaders.py writes them.
  auto c = Container(false, 4);
  auto file = ShaderLibrary::Serialize({Entry(0x1122334455667788ull, c, 6)});
  SR_CHECK(std::memcmp(file.data(), "SRSHLIB\0", 8) == 0);
  uint32_t version, count;
  uint64_t checksum, hash;
  uint32_t stage, csize, dsize;
  std::memcpy(&version, file.data() + 8, 4);
  std::memcpy(&count, file.data() + 12, 4);
  std::memcpy(&checksum, file.data() + 16, 8);
  std::memcpy(&hash, file.data() + 24, 8);
  std::memcpy(&stage, file.data() + 32, 4);
  std::memcpy(&csize, file.data() + 36, 4);
  std::memcpy(&dsize, file.data() + 40, 4);
  SR_CHECK_EQ(version, 1u);
  SR_CHECK_EQ(count, 1u);
  SR_CHECK(checksum == Fnv1a64(file.data() + 24, file.size() - 24));
  SR_CHECK(hash == 0x1122334455667788ull);
  SR_CHECK_EQ(stage, 1u);
  SR_CHECK_EQ(csize, uint32_t(c.size()));
  SR_CHECK_EQ(dsize, 5u);
  SR_CHECK_EQ(file.size(), size_t(24 + 24 + c.size() + 5));
}

SR_TEST(shader_library_identifies_short_containers) {
  // Containers shorter than the 64-byte body key use a shorter key.
  auto small = Container(false, 6, 0x10, 0x0C);
  auto large = Container(false, 6, 0x80, 0x30);
  ShaderLibrary lib =
      Load(ShaderLibrary::Serialize({Entry(0x51, small, 1), Entry(0x52, large, 2)}));
  std::vector<uint8_t> guest = small;
  PutBE(guest, 4, 0);
  PutBE(guest, 8, 0);
  guest.resize(guest.size() + 8, 0xEE);
  PreShaderMatch how;
  const PreShader* s = lib.Identify(guest.data(), guest.size(), &how);
  SR_CHECK(s && s->container_hash == 0x51);
  SR_CHECK(how == PreShaderMatch::kBody);
  std::vector<uint8_t> guest_large = large;
  PutBE(guest_large, 8, 0);
  s = lib.Identify(guest_large.data(), guest_large.size(), &how);
  SR_CHECK(s && s->container_hash == 0x52);
}
