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

SR_TEST(active_reuses_only_same_uploaded_resource) {
  const std::vector a{RexTrackingStamp{7,8,9,0,RexTrackingCoverage::Complete}};
  TextureUploadProof proof{12,a,55};
  SR_CHECK(CanReuseTexture(proof,a,55));
  SR_CHECK(!CanReuseTexture(proof,a,0));
  SR_CHECK(!CanReuseTexture(proof,a,56)); // replaced resource or another view/format
  auto b=a; ++b[0].allocation_generation;
  SR_CHECK(!CanReuseTexture(proof,b,55));
  proof.watch=0; SR_CHECK(!CanReuseTexture(proof,a,55));
}
SR_TEST(active_write_after_observation_invalidates_next_validation) {
  const std::vector a{RexTrackingStamp{7,8,9,0,RexTrackingCoverage::Complete}};
  TextureUploadProof proof{12,a,55};
  SR_CHECK(CanReuseTexture(proof,a,55));
  auto b=a; ++b[0].invalidation_epoch;b[0].coverage=RexTrackingCoverage::Invalidated;
  SR_CHECK(!CanReuseTexture(proof,b,55));
}
SR_TEST(active_fallback_keeps_legacy_cadence) {
  SR_CHECK(LegacyTextureValidationRequired(4096,true,false,2,1));
  SR_CHECK(LegacyTextureValidationRequired(8<<20,true,true,2,1));
  SR_CHECK(!LegacyTextureValidationRequired(8<<20,true,false,31,1));
  SR_CHECK(!LegacyTextureValidationRequired(8<<20,false,false,30,1));
  SR_CHECK(LegacyTextureValidationRequired(8<<20,false,false,31,1));
}

SR_TEST(active_descriptor_identity_checks_every_fetch_word_even_with_same_hash_bucket) {
  const std::array<uint32_t,6> a{1,2,3,4,5,6};
  SR_CHECK(TextureDescriptorMatches(a,a));
  for(size_t i=0;i<a.size();++i) {auto b=a; b[i]^=1;SR_CHECK(!TextureDescriptorMatches(a,b));}
}
