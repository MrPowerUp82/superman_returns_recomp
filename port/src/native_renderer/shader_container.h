// XDK shader container header (the object the XDK CreateVertexShader /
// CreatePixelShader receive). Pure code, no SDK dependency: unit-tested in
// tests/native without the game.
//
// Layout (big-endian; XenosRecomp ShaderContainer, kit shader_registry.cpp):
//   +0 flags     0x102A1100 = pixel shader, 0x102A1101 = vertex shader
//   +4 virtualSize, +8 physicalSize; the microcode (physical part) directly
//   follows the virtual part, so the container is [0, virtual + physical).
// The container hash (the key of the offline shader corpus, XenosRecomp and
// UnleashedRecomp) is XXH3_64 of exactly those bytes.
#pragma once

#include <cstddef>
#include <cstdint>

namespace superman_returns::native {

struct ShaderContainerHeader {
  uint32_t flags = 0;
  uint32_t virtual_size = 0;
  uint32_t physical_size = 0;
  bool is_vertex = false;
  size_t total_size() const { return size_t(virtual_size) + physical_size; }
};

// Containers far larger than any real shader mean the hook read something
// else (a wrong hook address while the profile is being confirmed).
inline constexpr size_t kMaxShaderContainerBytes = 1u << 20;

inline uint32_t LoadBigEndian32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// Parses the header at `data`; `available` bounds the readable bytes
// (SIZE_MAX when unknown). The kit hashed without checking; the checks here
// keep a wrongly confirmed hook from reading gigabytes of guest memory.
inline bool ParseShaderContainerHeader(const uint8_t* data, size_t available,
                                       ShaderContainerHeader& out) {
  if (!data || available < 12) return false;
  ShaderContainerHeader h;
  h.flags = LoadBigEndian32(data);
  h.virtual_size = LoadBigEndian32(data + 4);
  h.physical_size = LoadBigEndian32(data + 8);
  if ((h.flags & 0xFFFFFF00u) != 0x102A1100u) return false;
  if (!h.virtual_size || !h.physical_size) return false;
  if (h.total_size() > kMaxShaderContainerBytes || h.total_size() > available) return false;
  h.is_vertex = (h.flags & 1) != 0;
  out = h;
  return true;
}

}  // namespace superman_returns::native
