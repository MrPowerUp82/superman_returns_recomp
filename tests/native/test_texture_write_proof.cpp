#include "test_main.h"
#include "texture_write_proof.h"
#include <array>
#if defined(_WIN32)
#include "../../tools/rexglue-write-tracking/src/arm_windows_regions.h"

namespace {
constexpr uintptr_t TestView=0x100000000ull;
constexpr size_t TestPage=4096;
MEMORY_BASIC_INFORMATION TestMapping(size_t page,size_t pages,DWORD protection) {
  MEMORY_BASIC_INFORMATION m{};
  m.BaseAddress=reinterpret_cast<void*>(TestView+page*TestPage);
  m.AllocationBase=reinterpret_cast<void*>(TestView);
  m.RegionSize=pages*TestPage;m.State=MEM_COMMIT;m.Type=MEM_MAPPED;m.Protect=protection;
  return m;
}
}
SR_TEST(region_arm_batches_queries_and_protects_only_watched_pages) {
  std::array<uint8_t,16> owned{};unsigned queries=0,protects=0;
  SR_CHECK(sr::tracking::ArmWindowsRegions(reinterpret_cast<uint8_t*>(TestView),owned,3,7,
      [&](auto,auto& m) {++queries;m=TestMapping(0,16,PAGE_READWRITE);return true;},
      [&](auto* address,size_t length) {++protects;SR_CHECK_EQ(uintptr_t(address),TestView+3*TestPage);
        SR_CHECK_EQ(length,5*TestPage);return true;}));
  SR_CHECK_EQ(queries,1u);SR_CHECK_EQ(protects,1u);
  for(size_t i=0;i<owned.size();++i) SR_CHECK_EQ(owned[i],uint8_t(i>=3 && i<=7));
}
SR_TEST(region_arm_preserves_guards_and_short_alias_boundaries) {
  std::array<uint8_t,6> owned{1,0,0,0,0,0};unsigned queries=0,protects=0;
  auto query=[&](auto* address,auto& m) {++queries;size_t page=(uintptr_t(address)-TestView)/TestPage;
    m=TestMapping(page,2,page==0?PAGE_READONLY:page==2?PAGE_NOACCESS:PAGE_READWRITE);return true;};
  SR_CHECK(sr::tracking::ArmWindowsRegions(reinterpret_cast<uint8_t*>(TestView),owned,0,15,query,
      [&](auto* address,size_t length) {++protects;SR_CHECK_EQ(uintptr_t(address),TestView+4*TestPage);
        SR_CHECK_EQ(length,2*TestPage);return true;}));
  SR_CHECK_EQ(queries,3u);SR_CHECK_EQ(protects,1u);
  SR_CHECK((owned==std::array<uint8_t,6>{1,0,0,0,1,1}));
  SR_CHECK(sr::tracking::ArmWindowsRegions(reinterpret_cast<uint8_t*>(TestView),owned,6,15,query,
      [&](auto*,size_t) {++protects;return false;}));
  SR_CHECK_EQ(queries,3u);SR_CHECK_EQ(protects,1u);
  std::array<uint8_t,2> clipped{};
  SR_CHECK(sr::tracking::ArmWindowsRegions(reinterpret_cast<uint8_t*>(TestView),clipped,0,15,
      [](auto,auto& m) {m=TestMapping(0,16,PAGE_READWRITE);return true;},
      [](auto* address,size_t length) {SR_CHECK_EQ(uintptr_t(address),TestView);
        SR_CHECK_EQ(length,2*TestPage);return true;}));
  SR_CHECK((clipped==std::array<uint8_t,2>{1,1}));
}
SR_TEST(region_arm_fails_closed_on_bad_mappings_or_failed_protect) {
  for(unsigned failure=0;failure<14;++failure) {
    std::array<uint8_t,4> owned{};unsigned protects=0;
    SR_CHECK(!sr::tracking::ArmWindowsRegions(reinterpret_cast<uint8_t*>(TestView),owned,0,3,
        [&](auto,auto& m) {m=TestMapping(0,4,PAGE_READWRITE);
          if(failure==0) return false;
          if(failure==1) m.State=MEM_RESERVE;
          if(failure==2) m.Type=MEM_PRIVATE;
          if(failure==3) m.AllocationBase=nullptr;
          if(failure==4) m.Protect=PAGE_EXECUTE_READWRITE;
          if(failure==5) m.RegionSize=0;
          if(failure==6) m.RegionSize=SIZE_MAX;
          if(failure==7) m.Protect=PAGE_READWRITE|PAGE_GUARD;
          if(failure==8) m.Protect=PAGE_WRITECOPY;
          if(failure==9) m.Protect=PAGE_READWRITE|PAGE_NOCACHE;
          if(failure==10) m.BaseAddress=reinterpret_cast<void*>(TestView+1);
          if(failure==11) {m.BaseAddress=reinterpret_cast<void*>(TestView-TestPage);m.RegionSize=TestPage;}
          if(failure==12) m.RegionSize=TestPage/2;
          return true;},[&](auto*,size_t) {++protects;return false;}));
    SR_CHECK((owned==std::array<uint8_t,4>{}));SR_CHECK_EQ(protects,failure==13?1u:0u);
  }
}
SR_TEST(region_arm_retains_partial_ownership_for_failure_cleanup) {
  for(bool fail_protect:{false,true}) {
    std::array<uint8_t,4> owned{};unsigned queries=0,protects=0;
    SR_CHECK(!sr::tracking::ArmWindowsRegions(reinterpret_cast<uint8_t*>(TestView),owned,0,3,
        [&](auto,auto& m) {if(++queries==2 && !fail_protect) return false;
          m=TestMapping(queries==1?0:2,2,PAGE_READWRITE);return true;},
        [&](auto*,size_t) {return ++protects==1;}));
    SR_CHECK((owned==std::array<uint8_t,4>{1,1,0,0}));SR_CHECK_EQ(queries,2u);
    SR_CHECK_EQ(protects,fail_protect?2u:1u);
  }
}
#endif

using namespace superman_returns::native;

SR_TEST(texture_query_admission_never_samples_an_unpublished_upload) {
  TextureUploadProof proof;
  unsigned calls=0;
  auto query=[&] {++calls;return true;};
  SR_CHECK(!QueryPublishedTexture(proof,7,query));SR_CHECK_EQ(calls,0u);
  proof.watch=1;proof.resource_version=7;
  SR_CHECK(!QueryPublishedTexture(proof,7,query));SR_CHECK_EQ(calls,0u);
  proof.stamps={{1,2,3,0,RexTrackingCoverage::Complete}};
  SR_CHECK(!QueryPublishedTexture(proof,0,query));SR_CHECK(!QueryPublishedTexture(proof,8,query));
  SR_CHECK_EQ(calls,0u);
  SR_CHECK(QueryPublishedTexture(proof,7,query));SR_CHECK_EQ(calls,1u);
  proof.stamps[0].coverage=RexTrackingCoverage::Invalidated;
  SR_CHECK(!QueryPublishedTexture(proof,7,query));SR_CHECK_EQ(calls,1u);
  proof.stamps[0].coverage=RexTrackingCoverage::Complete;proof.stamps[0].active_writers=1;
  SR_CHECK(!QueryPublishedTexture(proof,7,query));SR_CHECK_EQ(calls,1u);
}

SR_TEST(texture_query_admission_checks_all_stamps_and_preserves_fresh_validation) {
  TextureUploadProof good{1,{{1,2,0,0,RexTrackingCoverage::Complete},
                             {3,4,5,0,RexTrackingCoverage::Complete}},7,0};
  unsigned calls=0;
  auto query=[&] {++calls;return true;};
  auto changed=good;changed.watch=0;
  SR_CHECK(!QueryPublishedTexture(changed,7,query));
  changed=good;changed.resource_version=0;
  SR_CHECK(!QueryPublishedTexture(changed,7,query));
  changed=good;changed.stamps[1].allocation_generation=0;
  SR_CHECK(!QueryPublishedTexture(changed,7,query));
  changed=good;changed.stamps[1].coverage_generation=0;
  SR_CHECK(!QueryPublishedTexture(changed,7,query));
  changed=good;changed.stamps[1].coverage=RexTrackingCoverage::Unknown;
  SR_CHECK(!QueryPublishedTexture(changed,7,query));
  changed=good;changed.stamps[1].active_writers=1;
  SR_CHECK(!QueryPublishedTexture(changed,7,query));
  SR_CHECK_EQ(calls,0u);
  // Domain zero is supported by runtimes without the optional fast view.
  SR_CHECK(QueryPublishedTexture(good,7,query));SR_CHECK_EQ(calls,1u);
  SR_CHECK(!QueryPublishedTexture(good,7,[&] {++calls;return false;}));
  SR_CHECK_EQ(calls,2u);
  for(unsigned field=0;field<4;++field) {
    auto fresh=good.stamps;
    if(field==0) ++fresh[1].invalidation_epoch;
    if(field==1) ++fresh[1].allocation_generation;
    if(field==2) ++fresh[1].coverage_generation;
    if(field==3) fresh[1].active_writers=1;
    SR_CHECK(!QueryPublishedTexture(good,7,[&] {return CanReuseTexture(good,fresh,7);}));
  }
}

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

SR_TEST(unchanged_small_textures_back_off_hashes_but_still_sample) {
  TextureHashBackoff backoff;
  backoff.OnHashed(10, false);
  uint64_t last_hash = 10;
  for (uint64_t delay : {2ull, 4ull, 8ull, 8ull, 8ull}) {
    // Hashing unchanged content doubles the interval up to eight frames.
    backoff.OnHashed(last_hash, true);
    for (uint64_t frame = last_hash + 1; frame < last_hash + delay; ++frame)
      SR_CHECK(!backoff.ShouldValidate(4096, true, false, frame, last_hash, true));
    SR_CHECK(backoff.ShouldValidate(4096, true, false, last_hash + delay, last_hash, true));
    last_hash += delay;
  }
  backoff.OnHashed(last_hash, false);
  SR_CHECK(backoff.ShouldValidate(4096, true, false, last_hash + 1, last_hash, true));
}

SR_TEST(untracked_video_or_ui_writes_trigger_continuous_validation) {
  TextureHashBackoff backoff;
  backoff.OnHashed(10, false);
  backoff.OnHashed(11, true);
  backoff.OnHashed(13, true);
  backoff.OnHashed(17, false, true);
  backoff.OnHashed(18, true);
  SR_CHECK(backoff.ShouldValidate(4096, true, false, 19, 18, true));
  backoff.OnHashed(19, false, true); // Another frame updated without a write notification.
  SR_CHECK(backoff.ShouldValidate(4096, true, false, 139, 138, true));
  backoff.OnHashed(139, true);
  SR_CHECK(!backoff.ShouldValidate(4096, true, false, 140, 139, true));
}

SR_TEST(texture_hash_backoff_keeps_write_and_fallback_rules) {
  TextureHashBackoff backoff;
  backoff.OnHashed(100, false);
  backoff.OnHashed(101, true);
  backoff.OnHashed(103, true);
  SR_CHECK(!backoff.ShouldValidate(4096, true, false, 104, 103, true));
  SR_CHECK(backoff.ShouldValidate(4096, true, true, 104, 103, true));
  SR_CHECK(backoff.ShouldValidate(4096, false, false, 104, 103, true));
  SR_CHECK(backoff.ShouldValidate(4096, true, false, 104, 103, false));
  SR_CHECK(!backoff.ShouldValidate(8u << 20, true, false, 104, 103, true));
  SR_CHECK(backoff.ShouldValidate(8u << 20, true, true, 104, 103, true));
  SR_CHECK(backoff.ShouldValidate(8u << 20, false, false, 133, 103, true));
}

SR_TEST(active_descriptor_identity_checks_every_fetch_word_even_with_same_hash_bucket) {
  const std::array<uint32_t,6> a{1,2,3,4,5,6};
  SR_CHECK(TextureDescriptorMatches(a,a));
  for(size_t i=0;i<a.size();++i) {auto b=a; b[i]^=1;SR_CHECK(!TextureDescriptorMatches(a,b));}
}
