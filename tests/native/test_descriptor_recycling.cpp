#include "descriptor_recycling.h"
#include "test_main.h"
#include <algorithm>
using namespace superman_returns::native;

SR_TEST(descriptor_recycling_uses_frame_completion_without_polling_per_draw) {
  RetiredDescriptors retired;
  for (uint32_t i=3;i<3003;++i) retired.emplace_back(7,i);
  uint32_t next=4000, queries=0;
  uint64_t completed=7;
  auto query=[&] { ++queries; return uint64_t(7); };
  for (uint32_t i=3;i<3003;++i)
    SR_CHECK_EQ(AllocateDescriptor(retired,completed,next,32768,query),i);
  SR_CHECK(retired.empty());
  SR_CHECK_EQ(queries,0u);
  SR_CHECK_EQ(next,4000u);
}

SR_TEST(descriptor_recycling_never_overwrites_inflight_or_pinned_slots) {
  RetiredDescriptors retired{{9,3},{UINT64_MAX,4}};
  uint32_t next=5,queries=0;
  uint64_t completed=8;
  auto query=[&] { ++queries; return uint64_t(8); };
  SR_CHECK_EQ(AllocateDescriptor(retired,completed,next,6,query),5u);
  SR_CHECK_EQ(queries,0u);
  SR_CHECK_EQ(AllocateDescriptor(retired,completed,next,6,query),0u);
  SR_CHECK_EQ(retired.size(),2u);
  SR_CHECK_EQ(queries,1u);
  auto progressed=[] {return uint64_t(9);};
  SR_CHECK_EQ(AllocateDescriptor(retired,completed,next,6,progressed),3u);
  SR_CHECK_EQ(AllocateDescriptor(retired,completed,next,6,progressed),0u);
  SR_CHECK_EQ(retired.front().second,4u);
}

SR_TEST(descriptor_recycling_does_not_treat_device_loss_as_completed_work) {
  RetiredDescriptors retired{{9,3},{UINT64_MAX,4}};
  uint32_t next=6;
  uint64_t completed=8;
  SR_CHECK_EQ(AllocateDescriptor(retired,completed,next,6,[]{return UINT64_MAX;}),0u);
  SR_CHECK_EQ(retired.size(),2u);
  SR_CHECK_EQ(completed,8u);
}

SR_TEST(descriptor_recycling_preserves_diagnostic_pin_and_retag_order) {
  RetiredDescriptors retired{{4,3},{4,4},{5,5},{5,6}};
  retired[1].first=UINT64_MAX; // An SRV kept by a diagnostic command-list split.
  auto order=[&] {std::stable_sort(retired.begin(),retired.end(),
      [](const auto& a,const auto& b) {return a.first<b.first;});};
  order();
  uint32_t next=7;
  uint64_t completed=5;
  auto query=[]{return uint64_t(5);};
  for (uint32_t expected:{3u,5u,6u})
    SR_CHECK_EQ(AllocateDescriptor(retired,completed,next,7,query),expected);
  SR_CHECK_EQ(AllocateDescriptor(retired,completed,next,7,query),0u);
  retired.front().first=6; // Retag after the split has been submitted.
  retired.emplace_back(6,8);
  order();
  completed=6;
  SR_CHECK_EQ(AllocateDescriptor(retired,completed,next,7,query),4u);
  SR_CHECK_EQ(AllocateDescriptor(retired,completed,next,7,query),8u);
}
