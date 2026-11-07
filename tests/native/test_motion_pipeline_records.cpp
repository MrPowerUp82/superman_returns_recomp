#include "../../port/src/graphics/motion_pipeline_records.h"
#include "test_main.h"

using namespace superman_returns::graphics;

SR_TEST(motion_pipeline_records_roundtrip_preserves_observed_bias_bits) {
  MotionPipelineRecord record{3,1,7,0x3f000000,0,40};
  const auto bytes=SerializeMotionPipelineRecords({record});
  std::vector<MotionPipelineRecord> records;
  SR_CHECK(ParseMotionPipelineRecords(bytes,records));
  SR_CHECK_EQ(records.size(),size_t(1));
  SR_CHECK(records[0]==record);
}

SR_TEST(motion_pipeline_records_reject_partial_and_nonfinite_state_without_publishing) {
  MotionPipelineRecord record{3,1,7,0x3f000000,0,40};
  auto bytes=SerializeMotionPipelineRecords({record});
  bytes.pop_back();
  std::vector<MotionPipelineRecord> records;
  SR_CHECK(!ParseMotionPipelineRecords(bytes,records));
  SR_CHECK(records.empty());
  record.slope=0x7f800000;
  SR_CHECK(SerializeMotionPipelineRecords({record}).empty());
}

SR_TEST(motion_pipeline_records_preserve_legacy_equal_and_flagged_invalidator_abi) {
  const std::vector<MotionPipelineRecord> expected{
      {1,0,0,0,0,40},{3,1,0,0,0,45},
      {1,0,0,0,0,40|kMotionPipelineInvalidator},
      {3,1,0,0,0,45|kMotionPipelineInvalidator}};
  const auto bytes=SerializeMotionPipelineRecords(expected);
  SR_CHECK_EQ(bytes.size(),size_t(16+4*24));
  std::vector<MotionPipelineRecord> parsed;
  SR_CHECK(ParseMotionPipelineRecords(bytes,parsed));
  SR_CHECK(parsed==expected);
  auto invalid=expected[0];invalid.depth|=0x20000u;
  SR_CHECK(SerializeMotionPipelineRecords({invalid}).empty());
  auto corrupt=bytes;corrupt[16+20+2]=2;
  SR_CHECK(!ParseMotionPipelineRecords(corrupt,parsed));
  SR_CHECK(parsed.empty());
}

SR_TEST(motion_pipeline_embedded_seed_is_valid_metadata_with_distinct_depth_visibility) {
  std::vector<uint32_t> comparisons;
  for(const auto& record:kObservedMotionPipelineSeed) {
    SR_CHECK(ValidMotionPipelineRecord(record));comparisons.push_back(MotionPipelineDepthTest(record.depth));
    SR_CHECK_EQ(record.depth&0xffffu,45u);
  }
  SR_CHECK(comparisons==std::vector<uint32_t>({3,8,7,5}));
  const std::vector<MotionPipelineRecord> seeded(kObservedMotionPipelineSeed.begin(),kObservedMotionPipelineSeed.end());
  std::vector<MotionPipelineRecord> decoded;SR_CHECK(ParseMotionPipelineRecords(SerializeMotionPipelineRecords(seeded),decoded));
  SR_CHECK(decoded==seeded);
}
