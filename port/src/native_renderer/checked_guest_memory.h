#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <algorithm>
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
    // Each successful read owns a different slot until Reset. Moving the
    // outer vector preserves inner byte allocations and outstanding spans.
    if(used_==copies_.size()) copies_.emplace_back();
    auto& bytes=copies_[used_];
    bytes.resize(length);
    SIZE_T copied = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), source, bytes.data(), length, &copied) || copied != length) {
      return {};
    }
    ++used_;
    return bytes;
  }
  std::span<const uint8_t> ReadScratch(const void* source, uint32_t length) {
    if (!source || !length || length > 0x20000000u) return {};
    scratch_.resize(length);
    SIZE_T copied = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), source, scratch_.data(), length, &copied) || copied != length) {
      return {};
    }
    return scratch_;
  }
  void Reset() {
    // Reuse small draw/PM4 reads, but never retain an unbounded streaming
    // snapshot. At most 32 slots of 64 KiB (2 MiB) survive a command.
    copies_.resize(std::min(copies_.size(),size_t{32}));
    for(auto& bytes:copies_) if(bytes.capacity()>64*1024) std::vector<uint8_t>().swap(bytes);
    if(scratch_.capacity()>64*1024) std::vector<uint8_t>().swap(scratch_);
    used_=0;
  }
 private:
  std::vector<std::vector<uint8_t>> copies_;
  std::vector<uint8_t> scratch_;
  size_t used_=0;
};
} // namespace superman_returns::native
