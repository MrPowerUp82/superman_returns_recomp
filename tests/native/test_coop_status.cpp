#include "../../port/src/coop_status_packet.h"
#include "test_main.h"
using namespace superman_returns::coop;
SR_TEST(coop_status_normalizes_native_stamina_with_minimum_hud_capacity) {
  const auto full=status::Normalize(1,10000,10000);
  SR_CHECK(full && full->scaled==10000 && full->hud_maximum==10000);
  const auto quarter=status::Normalize(8780,2500,10000);
  SR_CHECK(quarter && quarter->scaled==2500);
  const auto small=status::Normalize(1,100,100);
  SR_CHECK(small && small->current==100 && small->maximum==100 && small->hud_maximum==500 && small->scaled==2000);
  const auto empty=status::Normalize(8780,0,10000);SR_CHECK(empty && empty->scaled==0);
}
SR_TEST(coop_status_rejects_missing_ids_invalid_capacity_and_out_of_range_magnitude) {
  SR_CHECK(!status::Normalize(0,1,10000));SR_CHECK(!status::Normalize(255,1,10000));
  SR_CHECK(!status::Normalize(1,-1,10000));SR_CHECK(!status::Normalize(1,1,0));
  SR_CHECK(!status::Normalize(1,1,-1));SR_CHECK(!status::Normalize(1,10001,10000));
}
SR_TEST(coop_status_selects_the_distinct_actor_for_the_consumed_view_and_binding) {
  const auto a=status::Normalize(1,10000,10000),b=status::Normalize(8780,2500,10000);
  if(!a || !b){SR_CHECK(false);return;}
  auto pair=status::MakePair(0xC0000001,120,*a,*b);SR_CHECK(pair.has_value());
  SR_CHECK(status::Select(pair,0xC0000001,0)==a);
  SR_CHECK(status::Select(pair,0xC0000001,1)==b);
  SR_CHECK(!status::Select(pair,0xC0000002,1));SR_CHECK(!status::Select(pair,0xC0000001,2));
  SR_CHECK(!status::Select({},0xC0000001,1));
  SR_CHECK(!status::MakePair(0,120,*a,*b));SR_CHECK(!status::MakePair(0xC0000000,120,*a,*b));
  SR_CHECK(!status::MakePair(0x80000001,120,*a,*b));SR_CHECK(!status::MakePair(0xC0000001,0,*a,*b));
  SR_CHECK(!status::MakePair(0xC0000001,120,*a,*a));
  pair=status::MakePair(0xC0000002,121,*a,*b);
  SR_CHECK(!status::Select(pair,0xC0000001,1));SR_CHECK(status::Select(pair,0xC0000002,1)==b);
}
