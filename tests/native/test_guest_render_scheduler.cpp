#include "guest_render_scheduler_policy.h"
#include "test_main.h"
#include <array>
#include <algorithm>
using namespace superman_returns::native;
namespace {
void Put(std::span<uint8_t> b,size_t offset,uint64_t value,size_t size) {
  for(size_t i=0;i<size;++i) b[offset+i]=uint8_t(value>>((size-1-i)*8));
}
uint64_t Get(std::span<const uint8_t> b,size_t offset,size_t size) {
  uint64_t value=0;for(size_t i=0;i<size;++i) value=(value<<8)|b[offset+i];return value;
}
std::array<uint8_t,176> ObservedScheduler() {
  std::array<uint8_t,176> b;b.fill(0xA5);
  Put(b,0,0x82005B98,4);Put(b,52,1,4);Put(b,60,0x41F00000,4);
  Put(b,72,33333334,8);Put(b,88,100000002,8);Put(b,120,33000000,8);
  Put(b,132,0x3D072B02,4);return b;
}
}
SR_TEST(render_scheduler_unlock_changes_only_the_minimum_period) {
  const auto original=ObservedScheduler();auto changed=original;
  SR_CHECK(ApplyGuestRenderSchedulerPeriod(changed,0));
  SR_CHECK_EQ(Get(changed,72,8),1000000u);
  for(size_t i=0;i<changed.size();++i) if(i<72 || i>=80) SR_CHECK_EQ(changed[i],original[i]);
}
SR_TEST(render_scheduler_explicit_targets_and_original_cadence) {
  for(auto [fps,period]:{std::pair{60,16666667ull},std::pair{120,8333334ull}}) {
    auto b=ObservedScheduler();SR_CHECK(ApplyGuestRenderSchedulerPeriod(b,fps));SR_CHECK_EQ(Get(b,72,8),period);
  }
  auto b=ObservedScheduler();const auto original=b;
  SR_CHECK(!ApplyGuestRenderSchedulerPeriod(b,30));SR_CHECK(b==original);
}
SR_TEST(render_scheduler_rejects_unknown_types_modes_rates_and_short_objects) {
  for(auto [offset,value,size]:{std::array<uint64_t,3>{0,0x82005B99,4},
      {52,0,4},{52,2,4},{60,0x42700000,4},{72,16666667,8}}) {
    auto b=ObservedScheduler();Put(b,size_t(offset),value,size_t(size));const auto original=b;
    SR_CHECK(!ApplyGuestRenderSchedulerPeriod(b,0));SR_CHECK(b==original);
  }
  auto b=ObservedScheduler();const auto original=b;
  SR_CHECK(!ApplyGuestRenderSchedulerPeriod(std::span(b).first(79),0));SR_CHECK(b==original);
  SR_CHECK(!ApplyGuestRenderSchedulerPeriod(b,-1));SR_CHECK(b==original);
}
