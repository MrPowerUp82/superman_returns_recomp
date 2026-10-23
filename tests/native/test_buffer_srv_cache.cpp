#include "buffer_srv_cache.h"
#include "descriptor_recycling.h"
#include "test_main.h"
using namespace superman_returns::native;

SR_TEST(buffer_srv_cache_reuses_host_generation_until_invalidated) {
  BufferSrvCache cache(2);
  SR_CHECK(!cache.Insert(17,3));
  for(int frame=0;frame<100;++frame) SR_CHECK_EQ(cache.Find(17),3u);
  auto removed=cache.Erase(17);
  SR_CHECK(removed.has_value());if(removed) SR_CHECK_EQ(removed->index,3u);
  SR_CHECK_EQ(cache.Find(17),0u);
  SR_CHECK(!cache.Erase(17));
  SR_CHECK(!cache.Insert(18,4));
  SR_CHECK_EQ(cache.Find(18),4u);SR_CHECK_EQ(cache.Find(17),0u);
}
SR_TEST(buffer_srv_cache_evicts_least_recently_used_generation) {
  BufferSrvCache cache(2);
  cache.Insert(17,3);cache.Insert(18,4);
  SR_CHECK_EQ(cache.Find(17),3u);
  auto old=cache.Insert(19,5);
  SR_CHECK(old.has_value());
  if(old) {SR_CHECK_EQ(old->generation,18u);SR_CHECK_EQ(old->index,4u);}
  SR_CHECK_EQ(cache.Find(18),0u);
  SR_CHECK_EQ(cache.Find(17),3u);SR_CHECK_EQ(cache.Find(19),5u);
}
SR_TEST(buffer_srv_cache_replacement_returns_previous_view_without_leaking) {
  BufferSrvCache cache(1);
  SR_CHECK(!cache.Insert(17,3));SR_CHECK(!cache.Insert(17,3));
  auto old=cache.Insert(17,4);
  SR_CHECK(old.has_value());if(old) SR_CHECK_EQ(old->index,3u);
  SR_CHECK_EQ(cache.Find(17),4u);SR_CHECK_EQ(cache.size(),size_t(1));
}
SR_TEST(buffer_srv_cache_does_not_retain_unknown_generations_or_failed_indices) {
  BufferSrvCache cache(2);
  SR_CHECK(!cache.Insert(17,0));SR_CHECK_EQ(cache.size(),size_t(0));
  auto lease=cache.Insert(0,3);
  SR_CHECK(lease.has_value());if(lease) SR_CHECK_EQ(lease->index,3u);
  SR_CHECK_EQ(cache.Find(0),0u);SR_CHECK_EQ(cache.size(),size_t(0));
  BufferSrvCache disabled(0);
  auto bypass=disabled.Insert(17,4);
  SR_CHECK(bypass.has_value());SR_CHECK_EQ(disabled.Find(17),0u);
}
SR_TEST(buffer_srv_cache_bounds_transient_resource_churn) {
  BufferSrvCache cache(16);
  size_t evictions=0;
  for(uint64_t i=1;i<=10000;++i) {
    auto old=cache.Insert(i,uint32_t(i+2));
    if(old) {++evictions;SR_CHECK_EQ(old->generation,i-16);}
    SR_CHECK(cache.size()<=16);
  }
  SR_CHECK_EQ(evictions,size_t(9984));SR_CHECK_EQ(cache.Find(1),0u);
  SR_CHECK_EQ(cache.Find(10000),10002u);
}
SR_TEST(buffer_srv_cache_eviction_still_requires_gpu_fence_completion) {
  BufferSrvCache cache(1);cache.Insert(17,3);
  auto old=cache.Insert(18,4);SR_CHECK(old.has_value());if(!old)return;
  RetiredDescriptors retired{{9,old->index}};
  uint64_t completed=8;uint32_t next=5;
  SR_CHECK_EQ(AllocateDescriptor(retired,completed,next,5,[]{return uint64_t(8);}),0u);
  SR_CHECK_EQ(cache.Find(17),0u);SR_CHECK_EQ(cache.Find(18),4u);
  SR_CHECK_EQ(AllocateDescriptor(retired,completed,next,5,[]{return uint64_t(9);}),3u);
  SR_CHECK_EQ(cache.Find(17),0u);
}
