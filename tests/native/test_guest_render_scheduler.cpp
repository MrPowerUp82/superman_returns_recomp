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

SR_TEST(render_scheduler_admission_is_limited_to_render_among_four_engine_peers) {
  // 8218CFE8 constructs all four. Table 82911888 supplies 60,30,30,60;
  // the fixed peers feed +72 directly into the callback delta in 82720740.
  // 82720418 uses single-precision division/addition, so 60 Hz is 16666668 ns.
  struct Peer { uint32_t vtable,mode,rate;uint64_t period;bool render; };
  for(const auto peer:{Peer{0x82005B70,0,0x42700000,16666668,false}, // core
      Peer{0x82005B98,1,0x41F00000,33333334,true},                 // render
      Peer{0x82005BC4,0,0x41F00000,33333334,false},                // simulation
      Peer{0x82005BEC,1,0x42700000,16666668,false}}) {             // sound
    for(const auto [fps,period]:{std::pair{0,1000000ull},
        std::pair{60,16666667ull},std::pair{120,8333334ull}}) {
      auto b=ObservedScheduler();
      Put(b,0,peer.vtable,4);Put(b,52,peer.mode,4);
      Put(b,60,peer.rate,4);Put(b,72,peer.period,8);
      const auto original=b;
      SR_CHECK_EQ(ApplyGuestRenderSchedulerPeriod(b,fps),peer.render);
      if(peer.render) {
        SR_CHECK_EQ(Get(b,72,8),period);
        for(size_t i=0;i<b.size();++i) if(i<72 || i>=80) SR_CHECK_EQ(b[i],original[i]);
      } else SR_CHECK(b==original);
    }
  }
}

SR_TEST(render_scheduler_peer_identity_is_required_even_with_render_timing_fields) {
  // A mode-only unlock would also admit the sound peer (821663E8), while
  // a rate-only unlock would also admit the simulation peer (821780C0).
  // Give each peer render-like fields to exercise the class guard independently.
  for(const uint32_t vtable:{0x82005B70u,0x82005BC4u,0x82005BECu}) {
    auto b=ObservedScheduler();Put(b,0,vtable,4);const auto original=b;
    SR_CHECK(!ApplyGuestRenderSchedulerPeriod(b,0));SR_CHECK(b==original);
  }
}

SR_TEST(render_scheduler_reapplication_preserves_already_published_period_and_state) {
  auto b=ObservedScheduler();
  SR_CHECK(ApplyGuestRenderSchedulerPeriod(b,120));
  // The constructor hook runs before publication; the helper must not become
  // a live retargeting path that overwrites an already adjusted scheduler.
  const auto adjusted=b;
  for(const int32_t fps:{0,30,60,120}) {
    SR_CHECK(!ApplyGuestRenderSchedulerPeriod(b,fps));SR_CHECK(b==adjusted);
  }
}
