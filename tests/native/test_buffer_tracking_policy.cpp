#include "test_main.h"
#include "../../port/src/native_renderer/buffer_tracking_policy.h"
using superman_returns::native::BufferTrackingPolicy;
using superman_returns::native::BufferWatchChurnPolicy;
using superman_returns::native::ShouldSkipIntraBatchBufferCheck;
SR_TEST(buffer_tracking_waits_for_stable_hashes_before_watching) {
  BufferTrackingPolicy policy;
  for (int i = 0; i < 3; ++i) { policy.OnUnchangedHash(); SR_CHECK(!policy.ReadyToWatch()); }
  policy.OnUnchangedHash();
  SR_CHECK(policy.ReadyToWatch());
}
SR_TEST(buffer_tracking_backs_off_after_each_lost_proof) {
  BufferTrackingPolicy policy;
  policy.OnProofLost();
  SR_CHECK_EQ(policy.threshold, 8u);
  SR_CHECK_EQ(policy.stable, 0u);
  for (int i = 0; i < 10; ++i) policy.OnProofLost();
  SR_CHECK_EQ(policy.threshold, 256u);  // capped: a rewritten buffer retries rarely
  for (int i = 0; i < 255; ++i) policy.OnUnchangedHash();
  SR_CHECK(!policy.ReadyToWatch());
  policy.OnUnchangedHash();
  SR_CHECK(policy.ReadyToWatch());
}
SR_TEST(buffer_tracking_skips_intra_batch_revalidation_when_no_write_or_unlock_occurred) {
  SR_CHECK(ShouldSkipIntraBatchBufferCheck(false, false, true, 12, 12, 50, 50, 7, 7));
  SR_CHECK(!ShouldSkipIntraBatchBufferCheck(false, false, true, 11, 12, 50, 50, 7, 7));
  SR_CHECK(!ShouldSkipIntraBatchBufferCheck(false, false, true, 12, 12, 50, 51, 7, 7));
  SR_CHECK(!ShouldSkipIntraBatchBufferCheck(false, false, true, 12, 12, 50, 50, 7, 8));
  SR_CHECK(!ShouldSkipIntraBatchBufferCheck(false, true, true, 12, 12, 50, 50, 7, 7));
  SR_CHECK(!ShouldSkipIntraBatchBufferCheck(true, false, true, 12, 12, 50, 50, 7, 7));
}
SR_TEST(buffer_watch_is_suspended_only_by_consecutive_unchanged_notifications) {
  BufferWatchChurnPolicy policy;
  for (int i = 0; i < BufferWatchChurnPolicy::kFalseAlarms - 1; ++i)
    SR_CHECK(!policy.OnNotification(false, 10));
  // A notification that found new content is what the watch is for.
  SR_CHECK(!policy.OnNotification(true, 10));
  SR_CHECK_EQ(policy.false_alarms, 0u);
  for (int i = 0; i < BufferWatchChurnPolicy::kFalseAlarms - 1; ++i)
    SR_CHECK(!policy.OnNotification(false, 10));
  SR_CHECK(!policy.suspended);
  SR_CHECK(policy.OnNotification(false, 10));
  SR_CHECK(policy.suspended);
  // Suspended buffers receive no notifications; a stray one changes nothing.
  SR_CHECK(!policy.OnNotification(false, 11));
  SR_CHECK(!policy.OnNotification(true, 11));
  SR_CHECK(policy.suspended);
}
SR_TEST(buffer_watch_resumes_after_a_backoff_that_doubles_up_to_a_cap) {
  BufferWatchChurnPolicy policy;
  SR_CHECK(!policy.ShouldResume(1u << 30));  // never suspended
  uint64_t frame = 100;
  uint32_t expected = BufferWatchChurnPolicy::kFirstRetryFrames;
  for (int round = 0; round < 8; ++round) {
    for (int i = 0; i < BufferWatchChurnPolicy::kFalseAlarms; ++i) policy.OnNotification(false, frame);
    SR_CHECK(policy.suspended);
    SR_CHECK(!policy.ShouldResume(frame + expected - 1));
    SR_CHECK(policy.ShouldResume(frame + expected));
    frame += expected;
    policy.OnResumed();
    SR_CHECK(!policy.suspended);
    SR_CHECK_EQ(policy.false_alarms, 0u);
    expected = std::min(expected * 2, BufferWatchChurnPolicy::kMaxRetryFrames);
  }
  SR_CHECK_EQ(expected, BufferWatchChurnPolicy::kMaxRetryFrames);
}
