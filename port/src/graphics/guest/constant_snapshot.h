#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
namespace superman_returns::graphics::guest {
struct ConstantSnapshot {
  std::array<uint32_t, 1024> vs{}, ps{};
  std::array<uint8_t, 4096> shared{};
};
bool CaptureFloatConstants(std::span<const uint32_t>, bool guest_big_endian,
                           std::array<uint32_t, 1024> &, std::string &);
} // namespace superman_returns::graphics::guest
