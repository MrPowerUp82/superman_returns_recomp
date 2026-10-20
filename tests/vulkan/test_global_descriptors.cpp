#include "global_descriptors.h"
#include "test_main.h"
using namespace superman_returns::graphics::vulkan;
SR_TEST(global_slots_replace_a_full_live_cache_in_linear_work_without_touching_pending_slots) {
  constexpr uint32_t count=256;
  GlobalSlotTable slots(count);Error e;uint32_t index=99;bool fresh=false;
  std::vector<std::shared_ptr<int>> old,live;
  for(uint32_t i=0;i<count;++i){old.push_back(std::make_shared<int>(i));SR_CHECK(slots.Acquire(old.back(),0,4,1,index,fresh,e));}
  slots.Retire(1);
  std::array<bool,count> replaced{};
  for(uint32_t i=0;i<count;++i){
    live.push_back(std::make_shared<int>(i));SR_CHECK(slots.Acquire(live.back(),0,4,2,index,fresh,e));
    SR_CHECK(fresh);SR_CHECK(index<count);if(index<count){SR_CHECK(!replaced[index]);replaced[index]=true;}
  }
  SR_CHECK(slots.EvictionCandidatesExamined()<=count);
  auto extra=std::make_shared<int>(-1);index=999;
  SR_CHECK(!slots.Acquire(extra,0,4,2,index,fresh,e));SR_CHECK_EQ(index,999u);
  for(uint32_t i=0;i<count;++i){SR_CHECK(slots.Acquire(live[i],0,4,2,index,fresh,e));SR_CHECK(!fresh);}
  slots.Retire(2);
  for(uint32_t i=0;i<count;++i){SR_CHECK(slots.Acquire(old[i],0,4,3,index,fresh,e));SR_CHECK(fresh);}
  SR_CHECK(slots.EvictionCandidatesExamined()<=3*count);
}
SR_TEST(global_slots_keep_versions_until_their_last_submission_completes) {
  GlobalSlotTable slots(1);Error e;uint32_t index=99;bool fresh=false;
  auto old=std::make_shared<int>(1);std::weak_ptr<int> weak=old;
  SR_CHECK(slots.Acquire(old,0,16,4,index,fresh,e));SR_CHECK_EQ(index,0u);SR_CHECK(fresh);
  SR_CHECK(slots.Acquire(old,0,16,7,index,fresh,e));SR_CHECK(!fresh);
  old.reset();slots.Retire(4);SR_CHECK(!weak.expired());
  auto current=std::make_shared<int>(2);index=99;
  SR_CHECK(!slots.Acquire(current,0,16,8,index,fresh,e));SR_CHECK_EQ(index,99u);
  slots.Retire(7);SR_CHECK(weak.expired());
  SR_CHECK(slots.Acquire(current,0,16,8,index,fresh,e));SR_CHECK_EQ(index,0u);SR_CHECK(fresh);
}
SR_TEST(global_slots_deduplicate_same_range_and_hold_once_per_submission) {
  GlobalSlotTable slots(3);Error e;uint32_t index=99,other=99;bool fresh=false;
  auto value=std::make_shared<int>(1);
  SR_CHECK(slots.Acquire(value,16,32,1,index,fresh,e));SR_CHECK(fresh);SR_CHECK_EQ(value.use_count(),2);
  SR_CHECK(slots.Acquire(value,16,32,1,other,fresh,e));SR_CHECK(!fresh);SR_CHECK_EQ(index,other);SR_CHECK_EQ(value.use_count(),2);
  SR_CHECK(slots.Acquire(value,32,32,1,other,fresh,e));SR_CHECK(fresh);SR_CHECK(index!=other);
  SR_CHECK(slots.Acquire(value,16,16,1,other,fresh,e));SR_CHECK(fresh);SR_CHECK(index!=other);
  index=99;SR_CHECK(!slots.Acquire(value,0,32,1,index,fresh,e));SR_CHECK_EQ(index,99u);
}
SR_TEST(global_slots_evict_only_completed_entries_even_if_resource_is_still_live) {
  GlobalSlotTable slots(1);Error e;uint32_t index=99;bool fresh=false;
  auto first=std::make_shared<int>(1),second=std::make_shared<int>(2);
  SR_CHECK(slots.Acquire(first,0,4,1,index,fresh,e));
  slots.Retire(1);SR_CHECK_EQ(first.use_count(),1);
  SR_CHECK(slots.Acquire(second,0,4,2,index,fresh,e));SR_CHECK(fresh);
  SR_CHECK(!slots.Acquire(first,0,4,2,index,fresh,e));
  slots.Retire(2);SR_CHECK(slots.Acquire(first,0,4,3,index,fresh,e));SR_CHECK(fresh);
}
SR_TEST(global_slots_reject_null_retired_and_decreasing_serials) {
  GlobalSlotTable slots(2);Error e;uint32_t index=99;bool fresh=false;
  auto value=std::make_shared<int>(1);
  SR_CHECK(!slots.Acquire({},0,4,1,index,fresh,e));
  SR_CHECK(!slots.Acquire(value,0,0,1,index,fresh,e));
  SR_CHECK(slots.Acquire(value,0,4,4,index,fresh,e));
  SR_CHECK(!slots.Acquire(value,0,4,3,index,fresh,e));
  slots.Retire(4);SR_CHECK(!slots.Acquire(value,0,4,4,index,fresh,e));
  SR_CHECK(slots.Acquire(value,0,4,5,index,fresh,e));
}
SR_TEST(global_heap_limits_count_samplers_only_in_sampler_and_pool_budgets) {
  VkPhysicalDeviceDescriptorIndexingProperties limits{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
  limits.maxDescriptorSetUpdateAfterBindSampledImages=limits.maxPerStageDescriptorUpdateAfterBindSampledImages=10;
  limits.maxDescriptorSetUpdateAfterBindStorageBuffers=limits.maxPerStageDescriptorUpdateAfterBindStorageBuffers=20;
  limits.maxDescriptorSetUpdateAfterBindSamplers=limits.maxPerStageDescriptorUpdateAfterBindSamplers=5;
  limits.maxPerStageUpdateAfterBindResources=30;limits.maxUpdateAfterBindDescriptorsInAllPools=35;
  GlobalHeapCapacities capacity{8,20,5,1,1};Error e;
  SR_CHECK(ValidateGlobalHeapCapacities(capacity,limits,e));
  limits.maxUpdateAfterBindDescriptorsInAllPools=34;SR_CHECK(!ValidateGlobalHeapCapacities(capacity,limits,e));
  limits.maxUpdateAfterBindDescriptorsInAllPools=35;capacity.images_2d=9;SR_CHECK(!ValidateGlobalHeapCapacities(capacity,limits,e));
}
