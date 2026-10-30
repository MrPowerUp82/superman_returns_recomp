#include "../../port/src/graphics/guest/shader_texture_usage.h"
#include "test_main.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {
using superman_returns::graphics::guest::AnalyzeShaderTextureUsage;

void PutBE(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (size_t i = 0; i < 4; ++i)
    bytes[offset + i] = static_cast<uint8_t>(value >> (24 - i * 8));
}

// Hand-encoded Xenos fields: EXEC address[11:0], count[14:12],
// sequence[27:16]; CF opcode[47:44]. Two CFs occupy three BE dwords.
uint64_t Exec(uint32_t opcode, uint32_t address, uint32_t count,
              uint32_t sequence) {
  return (uint64_t{opcode} << 44) | (uint64_t{1} << 43) |
         (uint64_t{sequence} << 16) | (uint64_t{count} << 12) | address;
}
uint64_t Flow(uint32_t opcode, uint32_t target = 0) {
  return (uint64_t{opcode} << 44) | (uint64_t{1} << 43) | target;
}

struct Fixture {
  size_t pairs;
  std::vector<uint32_t> words;
  Fixture(size_t cf_pairs, size_t body_count)
      : pairs(cf_pairs), words((cf_pairs + body_count) * 3) {}
  void Cf(size_t index, uint64_t cf) {
    const size_t base = (index / 2) * 3;
    if ((index & 1) == 0) {
      words[base] = static_cast<uint32_t>(cf);
      words[base + 1] = (words[base + 1] & 0xffff0000u) |
                        static_cast<uint32_t>((cf >> 32) & 0xffff);
    } else {
      words[base + 1] = (words[base + 1] & 0xffffu) |
                        (static_cast<uint32_t>(cf) << 16);
      words[base + 2] = static_cast<uint32_t>(cf >> 16);
    }
  }
  void Body(size_t index, std::array<uint32_t, 3> instruction) {
    for (size_t i = 0; i < 3; ++i)
      words[(pairs + index) * 3 + i] = instruction[i];
  }
  std::vector<uint8_t> Container(bool vertex = false) const {
    // 36-byte container header, 24-byte shader header, 4-byte virtual padding,
    // 4-byte physical padding, then the original BE microcode.
    std::vector<uint8_t> bytes(68 + words.size() * 4);
    PutBE(bytes, 0, vertex ? 0x102a1101u : 0x102a1100u);
    PutBE(bytes, 4, 64);
    PutBE(bytes, 8, static_cast<uint32_t>(bytes.size() - 64));
    PutBE(bytes, 24, 36);
    PutBE(bytes, 36, 4);
    PutBE(bytes, 40, static_cast<uint32_t>(words.size() * 4));
    for (size_t i = 0; i < words.size(); ++i) PutBE(bytes, 68 + i * 4, words[i]);
    return bytes;
  }
};

std::array<uint32_t, 3> Fetch(uint32_t slot, uint32_t opcode = 1) {
  // const_index[24:20], opcode[4:0]; non-predicated by default.
  return {opcode | (slot << 20), 0, 0};
}
void Expect(const std::vector<uint8_t>& bytes, uint32_t mask) {
  const auto usage = AnalyzeShaderTextureUsage(bytes);
  SR_CHECK(usage.known);
  SR_CHECK_EQ(usage.mask, mask);
}
void Unknown(std::span<const uint8_t> bytes) {
  const auto usage = AnalyzeShaderTextureUsage(bytes);
  SR_CHECK(!usage.known);
  SR_CHECK_EQ(usage.mask, 0u);
}
Fixture SingleFetch() {
  Fixture f(1, 1);
  f.Cf(0, Exec(2, 1, 1, 1));
  f.Body(0, Fetch(0));
  return f;
}
}  // namespace

SR_TEST(shader_texture_usage_fetch_zero_and_31_both_stages) {
  auto f = SingleFetch();
  for (bool vertex : {false, true}) {
    Expect(f.Container(vertex), 1u);
    f.Body(0, Fetch(31));
    Expect(f.Container(vertex), 0x80000000u);
    f.Body(0, Fetch(0));
  }
}

SR_TEST(shader_texture_usage_all_32_slots) {
  Fixture f(16, 32);
  for (uint32_t slot = 0; slot < 32; ++slot) {
    f.Cf(slot, Exec(slot == 31 ? 2 : 1, 16 + slot, 1, 1));
    f.Body(slot, Fetch(slot));
  }
  Expect(f.Container(), 0xffffffffu);
}

SR_TEST(shader_texture_usage_alu_and_vertex_fetch_are_known_empty) {
  Fixture f(1, 2);
  f.Cf(0, Exec(2, 1, 2, 4));  // ALU then VFETCH.
  f.Body(0, Fetch(31));        // ALU bits happen to resemble a TFETCH.
  f.Body(1, Fetch(31, 0));     // VFETCH constant does not address a texture.
  Expect(f.Container(true), 0u);
}

SR_TEST(shader_texture_usage_empty_exec_is_known_empty) {
  Fixture f(1, 0);
  f.Cf(0, Exec(2, 1, 0, 0));
  Expect(f.Container(), 0u);
}

SR_TEST(shader_texture_usage_mixed_sequence_serialize_and_fetch_predication) {
  Fixture f(1, 4);
  f.Cf(0, Exec(2, 1, 4, 0xd1));  // TFETCH, ALU, VFETCH, serialized TFETCH.
  f.Body(0, Fetch(0));
  f.Body(1, Fetch(17));
  f.Body(2, Fetch(22, 0));
  auto predicated = Fetch(31);
  predicated[1] = 0x80000000u;
  predicated[2] = 0x80000000u;
  f.Body(3, predicated);
  Expect(f.Container(), 0x80000001u);
}

SR_TEST(shader_texture_usage_sixth_sequence_entry_is_not_dropped) {
  Fixture f(1, 6);
  f.Cf(0, Exec(2, 1, 6, 0xc00));  // Only instruction six is a fetch.
  f.Body(5, Fetch(31));
  Expect(f.Container(), 0x80000000u);
}

SR_TEST(shader_texture_usage_unreferenced_words_are_not_fetches) {
  auto f = SingleFetch();
  f.words.insert(f.words.end(), {0x01f00001u, 0u, 0u});
  Expect(f.Container(), 1u);
}

SR_TEST(shader_texture_usage_every_conditional_exec_including_end_fallthrough) {
  for (uint32_t opcode : {3u, 4u, 5u, 6u, 13u, 14u}) {
    Fixture f(1, 2);
    f.Cf(0, Exec(opcode, 1, 1, 1));
    f.Cf(1, Exec(2, 2, 1, 1));
    f.Body(0, Fetch(0));
    f.Body(1, Fetch(31));
    Expect(f.Container(), 0x80000001u);
  }
}

SR_TEST(shader_texture_usage_jump_target_after_earlier_exec_end_is_included) {
  Fixture f(2, 2);
  f.Cf(0, Flow(11, 3));
  f.Cf(1, Exec(2, 2, 1, 1));
  f.Cf(2, Flow(0));
  f.Cf(3, Exec(2, 3, 1, 1));
  f.Body(0, Fetch(0));
  f.Body(1, Fetch(31));
  Expect(f.Container(), 0x80000001u);
  // Even an unconditional jump is conservatively analyzed on both paths.
  f.Cf(0, Flow(11, 3 | (1u << 13)));
  Expect(f.Container(), 0x80000001u);
}

SR_TEST(shader_texture_usage_loop_body_and_exit_are_included) {
  Fixture f(2, 2);
  f.Cf(0, Flow(7, 3));
  f.Cf(1, Exec(1, 2, 1, 1));
  f.Cf(2, Flow(8, 1));
  f.Cf(3, Exec(2, 3, 1, 1));
  f.Body(0, Fetch(0));
  f.Body(1, Fetch(31));
  Expect(f.Container(), 0x80000001u);
}

SR_TEST(shader_texture_usage_texture_queries_conservatively_keep_the_slot) {
  for (uint32_t opcode : {1u, 16u, 17u, 18u, 19u}) {
    auto f = SingleFetch();
    f.Body(0, Fetch(31, opcode));
    Expect(f.Container(), 0x80000000u);
  }
}

SR_TEST(shader_texture_usage_lod_and_gradient_setters_need_no_texture) {
  for (uint32_t opcode : {24u, 25u, 26u}) {
    auto f = SingleFetch();
    f.Body(0, Fetch(31, opcode));
    Expect(f.Container(), 0u);
  }
}

SR_TEST(shader_texture_usage_unsupported_fetch_and_calls_are_unknown) {
  for (uint32_t opcode : {2u, 15u, 20u, 23u, 27u, 31u}) {
    auto f = SingleFetch();
    f.Body(0, Fetch(31, opcode));
    Unknown(f.Container());
  }
  for (uint32_t opcode : {9u, 10u}) {
    auto f = SingleFetch();
    f.Cf(1, Flow(opcode));
    Unknown(f.Container());
  }
}

SR_TEST(shader_texture_usage_every_container_prefix_is_bounded) {
  const auto bytes = SingleFetch().Container();
  for (size_t length = 0; length < bytes.size(); ++length)
    Unknown(std::span<const uint8_t>(bytes).first(length));
}

SR_TEST(shader_texture_usage_invalid_container_offsets_sizes_and_magic) {
  const auto good = SingleFetch().Container();
  const std::array<std::array<uint32_t, 2>, 15> mutations = {{
      {0, 0x112a1100}, {4, 35}, {4, 0xffffffff}, {8, 0xffffffff},
      {16, 64}, {20, 64}, {24, 0}, {24, 48}, {24, 0xfffffffcu},
      {28, 1}, {32, 1}, {36, 0xfffffffcu}, {40, 0}, {40, 23}, {40, 28}}};
  for (const auto& mutation : mutations) {
    auto bytes = good;
    PutBE(bytes, mutation[0], mutation[1]);
    Unknown(bytes);
  }
}

SR_TEST(shader_texture_usage_invalid_exec_refs_and_unrepresentable_sequence) {
  for (const auto& cf : {Exec(2, 0, 1, 1), Exec(2, 2, 1, 1),
                         Exec(2, 1, 2, 1), Exec(2, 1, 7, 0xfff),
                         Exec(2, 4095, 6, 0xfff)}) {
    auto f = SingleFetch();
    f.Cf(0, cf);
    Unknown(f.Container());
  }
}

SR_TEST(shader_texture_usage_invalid_jump_and_loop_targets) {
  for (uint32_t opcode : {7u, 8u, 11u}) {
    auto f = SingleFetch();
    f.Cf(1, Flow(opcode, 2));  // CF count is 2, so target points into body.
    Unknown(f.Container());
    f.Cf(1, Flow(opcode, 8191));
    Unknown(f.Container());
  }
}

SR_TEST(shader_texture_usage_unbalanced_or_inconsistent_loops_are_unknown) {
  for (uint32_t opcode : {7u, 8u}) {
    auto f = SingleFetch();
    f.Cf(1, Flow(opcode, 0));
    Unknown(f.Container());
  }
  Fixture f(2, 1);
  f.Cf(0, Flow(7, 3));
  f.Cf(1, Exec(1, 2, 1, 1));
  f.Cf(2, Flow(8, 0));  // Expected repeat target is CF 1.
  f.Cf(3, Exec(2, 2, 1, 1));
  Unknown(f.Container());
  f.Cf(2, Flow(8, 1 | (1u << 16)));  // Wrong loop constant ID.
  Unknown(f.Container());
}

SR_TEST(shader_texture_usage_conflicting_cf_and_body_boundary_is_unknown) {
  Fixture f(2, 1);
  f.Cf(0, Flow(12));
  f.Cf(1, Flow(0));
  f.Cf(2, Exec(2, 1, 1, 1));  // Refers into the pair containing itself.
  Unknown(f.Container());
}

SR_TEST(shader_texture_usage_unterminated_cf_is_unknown) {
  auto f = SingleFetch();
  f.Cf(0, Exec(1, 1, 1, 1));
  Unknown(f.Container());
  Fixture nops(1, 0);
  Unknown(nops.Container());
}

SR_TEST(shader_texture_usage_unaligned_input_and_trailing_container_bytes) {
  auto bytes = SingleFetch().Container();
  bytes.insert(bytes.begin(), 0xab);
  bytes.push_back(0xcd);
  const auto usage = AnalyzeShaderTextureUsage(std::span<const uint8_t>(bytes).subspan(1));
  SR_CHECK(usage.known);
  SR_CHECK_EQ(usage.mask, 1u);
}
