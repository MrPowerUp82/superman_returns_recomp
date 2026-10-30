// Xenos instruction layouts and CF-bound discovery adapted from Xenia's
// src/xenia/gpu/{ucode.h,shader.cc} and ReXGlue's ucode.h / AnalyzeUcode.
// Copyright (c) 2015, 2022, Ben Vanik and Xenia project contributors.
// Copyright (c) 2026, Tom Clay <tomc@tctechstuff.com> (ReXGlue adaptations).
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace superman_returns::graphics::guest {

struct ShaderTextureUsage {
  uint32_t mask = 0;
  bool known = false;
};

// Input is the original XDK container, including big-endian microcode, not host
// byte-swapped ucode or a host shader. Unknown always has a zero, unusable mask.
inline ShaderTextureUsage AnalyzeShaderTextureUsage(
    std::span<const uint8_t> container) {
  // Every read below is within a previously checked header or instruction.
  // Byte assembly also permits unaligned input and avoids host bitfield layout.
  const auto be32 = [](std::span<const uint8_t> bytes, std::size_t offset) {
    return (uint32_t{bytes[offset]} << 24) |
           (uint32_t{bytes[offset + 1]} << 16) |
           (uint32_t{bytes[offset + 2]} << 8) | uint32_t{bytes[offset + 3]};
  };
  if (container.size() < 36 ||
      (be32(container, 0) & 0xffffff00u) != 0x102a1100u ||
      be32(container, 28) != 0 || be32(container, 32) != 0) {
    return {};
  }
  const std::size_t virtual_size = be32(container, 4);
  const std::size_t physical_size = be32(container, 8);
  const std::size_t shader_offset = be32(container, 24);
  // Subtraction checks avoid overflow even on 32-bit size_t hosts. Constant
  // and default tables need only have valid virtual offsets; they aren't read.
  if (virtual_size < 36 || virtual_size > container.size() ||
      physical_size > container.size() - virtual_size ||
      be32(container, 16) >= virtual_size ||
      be32(container, 20) >= virtual_size || shader_offset < 36 ||
      shader_offset > virtual_size || virtual_size - shader_offset < 24) {
    return {};
  }
  const std::size_t physical_offset = be32(container, shader_offset);
  const std::size_t code_size = be32(container, shader_offset + 4);
  // EXEC has a 12-bit address and at most six sequence entries. Bounding the
  // entire input program also caps work on adversarial containers. Larger or
  // partially padded (non-96-bit) programs conservatively use the fallback.
  constexpr std::size_t kMaxInstructions = 0xfff + 6;
  if (code_size == 0 || code_size % 12 != 0 ||
      code_size / 12 > kMaxInstructions || physical_offset > physical_size ||
      code_size > physical_size - physical_offset) {
    return {};
  }
  const auto code = container.subspan(virtual_size, physical_size)
                        .subspan(physical_offset, code_size);
  const std::size_t instruction_count = code_size / 12;
  const auto cf_at = [&](std::size_t index) -> uint64_t {
    const std::size_t offset = (index / 2) * 12;
    const uint32_t middle = be32(code, offset + 4);
    if ((index & 1) == 0) {
      return uint64_t{be32(code, offset)} | (uint64_t{middle & 0xffffu} << 32);
    }
    return uint64_t{middle >> 16} | (uint64_t{be32(code, offset + 8)} << 16);
  };
  const auto is_exec = [](uint32_t opcode) {
    return (opcode >= 1 && opcode <= 6) || opcode == 13 || opcode == 14;
  };

  // CF pairs occupy the prefix, ending at the lowest EXEC body address. Scan
  // every CF there, including those after EXEC_END: jumps may target them.
  // This deliberately overapproximates reachability instead of evaluating
  // predicates, bool constants, loop iterations, or jumps on the host.
  std::size_t cf_pairs = instruction_count;
  bool found_exec = false;
  bool found_end = false;
  for (std::size_t pair = 0; pair < cf_pairs; ++pair) {
    for (std::size_t half = 0; half < 2; ++half) {
      const uint64_t cf = cf_at(pair * 2 + half);
      const uint32_t opcode = static_cast<uint32_t>(cf >> 44);
      if (!is_exec(opcode)) continue;
      const std::size_t address = cf & 0xfff;
      const std::size_t count = (cf >> 12) & 7;
      if (count > 6 || address <= pair || address > instruction_count ||
          count > instruction_count - address) {
        return {};
      }
      if (address < cf_pairs) cf_pairs = address;
      found_exec = true;
      found_end |= opcode == 2;
    }
  }
  // A conditional END alone can fall through. Require an unconditional end;
  // this accepts less than a full validator, but never invents an empty shader.
  if (!found_exec || !found_end) return {};

  struct Loop {
    std::size_t start;
    std::size_t skip;
    uint32_t id;
  };
  std::array<Loop, 4> loops{};
  std::size_t loop_depth = 0;
  uint32_t mask = 0;
  for (std::size_t index = 0; index < cf_pairs * 2; ++index) {
    const uint64_t cf = cf_at(index);
    const uint32_t opcode = static_cast<uint32_t>(cf >> 44);
    if (is_exec(opcode)) {
      const std::size_t address = cf & 0xfff;
      const std::size_t count = (cf >> 12) & 7;
      uint32_t sequence = static_cast<uint32_t>((cf >> 16) & 0xfff);
      // All EXEC references were bounded in the first pass. The final prefix
      // boundary can only shrink, so they also cannot overlap the CF region.
      for (std::size_t i = 0; i < count; ++i, sequence >>= 2) {
        if ((sequence & 1) == 0) continue;  // ALU cannot fetch a texture.
        const uint32_t fetch = be32(code, (address + i) * 12);
        switch (fetch & 31) {
          case 0:  // VFETCH uses vertex constants, not texture slots.
          case 24:  // SetTextureLod / SetTextureGradients only set registers.
          case 25:
          case 26:
            break;
          case 1:  // TFETCH, including predicated fetches.
          case 16:  // GetTextureBorderColorFrac
          case 17:  // GetTextureComputedLod
          case 18:  // GetTextureGradients
          case 19:  // GetTextureWeights
            mask |= uint32_t{1} << ((fetch >> 20) & 31);
            break;
          default:
            return {};  // Unknown fetch form: never silently drop a sampler.
        }
      }
      continue;
    }
    switch (opcode) {
      case 0:   // NOP (including paired padding)
      case 12:  // ALLOC
      case 15:  // MARK_VS_FETCH_DONE
        break;
      case 7: {  // LOOP_START: analyze both its body and its skip path.
        const std::size_t target = cf & 0x1fff;
        if (target >= cf_pairs * 2 || target <= index ||
            loop_depth == loops.size()) return {};
        loops[loop_depth++] = {index, target,
                              static_cast<uint32_t>((cf >> 16) & 31)};
        break;
      }
      case 8: {  // LOOP_END: no iteration or predicate simulation needed.
        if (loop_depth == 0) return {};
        const Loop& loop = loops[--loop_depth];
        if ((cf & 0x1fff) != loop.start + 1 || loop.skip != index + 1 ||
            ((cf >> 16) & 31) != loop.id) return {};
        break;
      }
      case 11:  // Conditional/unconditional JMP: both paths are already scanned.
        if ((cf & 0x1fff) >= cf_pairs * 2) return {};
        break;
      default:  // CALL / RETURN require a different subroutine layout analysis.
        return {};
    }
  }
  if (loop_depth != 0) return {};
  return {mask, true};
}

}  // namespace superman_returns::graphics::guest
