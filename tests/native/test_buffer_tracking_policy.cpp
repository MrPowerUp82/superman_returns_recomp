#include "test_main.h"
#include "../../port/src/native_renderer/buffer_tracking_policy.h"
using superman_returns::native::BufferTrackingPolicy;
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
