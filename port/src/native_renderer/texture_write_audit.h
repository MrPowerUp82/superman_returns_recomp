#pragma once

#include <cstdint>

namespace superman_returns::native {
// Diagnostic evidence only: unchanged samples never authorize skipping a hash.
struct TextureWriteAudit {
  uint64_t checks = 0, bytes = 0, changes = 0;
  uint64_t unnotified_changes = 0, unnotified_bytes = 0;

  void Record(uint64_t size, bool changed, bool notified) {
    ++checks;
    bytes += size;
    if (!changed) return;
    ++changes;
    if (!notified) {
      ++unnotified_changes;
      unnotified_bytes += size;
    }
  }
};
}  // namespace superman_returns::native
