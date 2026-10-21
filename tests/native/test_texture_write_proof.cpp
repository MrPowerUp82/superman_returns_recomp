#include "test_main.h"
#include "texture_write_proof.h"
#include <array>

using namespace superman_returns::native;

SR_TEST(tracking_missing_or_changed_stamp_rejects_proof) {
  const std::array original{RexTrackingStamp{7, 8, 9, 0, RexTrackingCoverage::Complete}};
  SR_CHECK(SameCoveredContent(original, original));
  SR_CHECK(!SameCoveredContent({}, {}));
  SR_CHECK(!SameCoveredContent(original, {}));
  auto changed = original;
  ++changed[0].allocation_generation;
  SR_CHECK(!SameCoveredContent(original, changed));
  changed = original; ++changed[0].coverage_generation;
  SR_CHECK(!SameCoveredContent(original, changed));
  changed = original; ++changed[0].invalidation_epoch;
  SR_CHECK(!SameCoveredContent(original, changed));
  changed = original; changed[0].active_writers = 1;
  SR_CHECK(!SameCoveredContent(original, changed));
  SR_CHECK(!SameCoveredContent(changed, changed));
  changed = original; changed[0].coverage = RexTrackingCoverage::Unknown;
  SR_CHECK(!SameCoveredContent(changed, changed));
  changed[0].coverage = RexTrackingCoverage::Invalidated;
  SR_CHECK(!SameCoveredContent(changed, changed));
  changed = original; changed[0].allocation_generation = 0;
  SR_CHECK(!SameCoveredContent(changed, changed));
  changed = original; changed[0].coverage_generation = 0;
  SR_CHECK(!SameCoveredContent(changed, changed));
}

SR_TEST(proof_race_during_capture_rejects_publication) {
  const std::array a{RexTrackingStamp{7,8,9,0,RexTrackingCoverage::Complete}};
  auto changed=a; ++changed[0].invalidation_epoch;
  TextureWriteProof proof;
  SR_CHECK(!proof.Publish(1,42,a,changed,changed));
  SR_CHECK(!proof.Publish(1,42,a,a,changed));
  SR_CHECK(proof.Publish(1,42,a,a,a));
  SR_CHECK(!proof.CanReuse(TextureTrackingMode::On,2,a));
  SR_CHECK(!proof.CanReuse(TextureTrackingMode::On,1,changed));
}
SR_TEST(compare_never_skips_legacy_hash_or_upload) {
  const std::array a{RexTrackingStamp{7,8,9,0,RexTrackingCoverage::Complete}};
  TextureWriteProof proof; SR_CHECK(proof.Publish(1,42,a,a,a));
  SR_CHECK(!proof.CanReuse(TextureTrackingMode::Off,1,a));
  SR_CHECK(!proof.CanReuse(TextureTrackingMode::Compare,1,a));
  SR_CHECK(proof.CanReuse(TextureTrackingMode::On,1,a));
  SR_CHECK(ParseTextureTrackingMode("compare")==TextureTrackingMode::Compare);
  SR_CHECK(ParseTextureTrackingMode("on")==TextureTrackingMode::On);
  SR_CHECK(ParseTextureTrackingMode("invalid")==TextureTrackingMode::Off);
}
SR_TEST(compare_mismatch_disqualifies_allocation) {
  const std::array a{RexTrackingStamp{7,8,9,0,RexTrackingCoverage::Complete}};
  TextureWriteProof proof; SR_CHECK(proof.Publish(1,42,a,a,a));
  auto raced=a; ++raced[0].invalidation_epoch;
  SR_CHECK(!proof.AuditMismatch(1,a,43,raced));
  SR_CHECK(proof.AuditMismatch(1,a,43,a));
  proof.Clear(); SR_CHECK(!proof.CanReuse(TextureTrackingMode::On,1,a));
}
