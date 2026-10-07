#include "shared_constants_cache.h"
#include "texture_write_audit.h"
#include "test_main.h"

using namespace superman_returns::native;

SR_TEST(shared_constants_cache_checks_every_word_including_derived_state_and_vertex_tail) {
  SharedConstantsCache cache;
  SharedConstantBlock original{};
  SR_CHECK_EQ(cache.Find(original), 0u);
  cache.Commit(original, 0x10000);
  SR_CHECK_EQ(cache.Find(original), 0x10000u);
  for (size_t word = 0; word < original.size(); ++word) {
    auto changed = original;
    changed[word] = 0x80000000u;
    SR_CHECK_EQ(cache.Find(changed), 0u);
  }
  SR_CHECK_EQ(cache.Find(original), 0x10000u);
}

SR_TEST(shared_constants_cache_recycles_allocations_only_after_reset_and_successful_upload) {
  SharedConstantsCache cache;
  SharedConstantBlock a{}, b{};
  b[1023] = 123;
  cache.Commit(a, 0x10000);
  // An upload failure for b must not commit it or lose the old valid allocation.
  SR_CHECK_EQ(cache.Find(b), 0u);
  SR_CHECK_EQ(cache.Find(a), 0x10000u);
  cache.Commit(b, 0x20000);
  SR_CHECK_EQ(cache.Find(a), 0u);
  SR_CHECK_EQ(cache.Find(b), 0x20000u);
  cache.Reset();
  SR_CHECK_EQ(cache.Find(b), 0u);
  cache.Commit(b, 0x30000);
  SR_CHECK_EQ(cache.Find(b), 0x30000u);
  cache.Commit(b, 0);  // No valid GPU allocation is never a hit.
  SR_CHECK_EQ(cache.Find(b), 0u);
}

SR_TEST(texture_audit_distinguishes_changes_without_notifications_from_unchanged_neighbors) {
  TextureWriteAudit audit;
  audit.Record(4096, false, false);
  audit.Record(8192, false, true);  // Neighboring page write, unchanged texture.
  audit.Record(16384, true, true);  // Includes a notification racing with hashing.
  audit.Record(32768, true, false);
  SR_CHECK_EQ(audit.checks, 4u);
  SR_CHECK_EQ(audit.bytes, 61440u);
  SR_CHECK_EQ(audit.changes, 2u);
  SR_CHECK_EQ(audit.unnotified_changes, 1u);
  SR_CHECK_EQ(audit.unnotified_bytes, 32768u);
}
