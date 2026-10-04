#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace superman_returns::native {
// Windows checks only the requested pages and copies them in one operation.
// VirtualQuery may scan the remainder of the 512 MB physical mapping on every
// small read. Keep each result alive through nested PM4 reader callbacks.
class CheckedGuestReads {
 public:
  std::span<const uint8_t> Read(const void* source, uint32_t length) {
    if (!source || !length || length > 0x20000000u) return {};
    auto& bytes = copies_.emplace_back(length);
    SIZE_T copied = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), source, bytes.data(), length, &copied) || copied != length) {
      copies_.pop_back();
      return {};
    }
    return bytes;
  }
  void Reset() { copies_.clear(); }
 private:
  std::deque<std::vector<uint8_t>> copies_;
};
} // namespace superman_returns::native
