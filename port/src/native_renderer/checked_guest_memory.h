#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <span>
#include <vector>

namespace superman_returns::native {
// Copies guest bytes inside an SEH frame instead of a syscall. ReadProcessMemory
// on the own process cost ~1.6 us per PM4 constant load (~600 loads/frame) and
// VirtualQuery ~12 us; a guarded memcpy costs only the copy. A fault (unmapped,
// PAGE_NOACCESS, partial range) returns false; `destination` may then hold a
// prefix of the source and must be discarded, as a failed checked read.
inline bool CopyGuestBytes(void* destination, const void* source, size_t length) {
  // Called through a volatile pointer so the copy stays a call inside the
  // __try region: clang only unwinds faults raised by calls without /EHa.
  static void* (*volatile const copy)(void*, const void*, size_t) = std::memcpy;
  __try {
    copy(destination, source, length);
  } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                  ? EXCEPTION_EXECUTE_HANDLER
                  : EXCEPTION_CONTINUE_SEARCH) {
    return false;
  }
  return true;
}

// Owned copies of guest ranges, validated by CopyGuestBytes. VirtualQuery may
// scan the remainder of the 512 MB physical mapping on every small read. Keep
// each result alive through nested PM4 reader callbacks.
class CheckedGuestReads {
 public:
  std::span<const uint8_t> Read(const void* source, uint32_t length) {
    if (!source || !length || length > 0x20000000u) return {};
    // Each successful read owns a different slot until Reset. Moving the
    // outer vector preserves inner byte allocations and outstanding spans.
    if(used_==copies_.size()) copies_.emplace_back();
    auto& bytes=copies_[used_];
    bytes.resize(length);
    if (!CopyGuestBytes(bytes.data(), source, length)) return {};
    ++used_;
    return bytes;
  }
  std::span<const uint8_t> ReadScratch(const void* source, uint32_t length) {
    if (!source || !length || length > 0x20000000u) return {};
    scratch_.resize(length);
    if (!CopyGuestBytes(scratch_.data(), source, length)) return {};
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
