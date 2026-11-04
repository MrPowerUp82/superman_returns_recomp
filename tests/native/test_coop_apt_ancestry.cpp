#include "../../port/src/coop_apt_ancestry.h"
#include "test_main.h"
using namespace superman_returns::coop::apt;
namespace {
Node Main(){return {1,101,1000,63,1,Movie::MainHud,true};}
Node Stamina(){return {2,202,2000,6,172,Movie::StaminaMeter,true};}
Node Meter(){return {3,303,3000,45,1,Movie::GenericMeter,true};}
Node Fill(){return {4,404,3000,21,34,Movie::GenericMeter,false};}
}
SR_TEST(coop_apt_ancestry_selects_only_the_main_hud_stamina_instance) {
  Ancestry path;
  SR_CHECK(path.Push(Main()));SR_CHECK(path.Push(Stamina()));SR_CHECK(path.Push(Meter()));
  SR_CHECK(path.Push(Fill()));SR_CHECK(path.SelectedRole()==Role::FillBody);
  SR_CHECK(path.Push({5,505,3000,15,4,Movie::GenericMeter,false}));
  SR_CHECK(path.SelectedRole()==Role::FillBody);
  SR_CHECK(path.Pop(5));SR_CHECK(path.Pop(4));SR_CHECK(path.SelectedRole()==Role::None);
  SR_CHECK(path.Pop(3));SR_CHECK(path.Pop(2));SR_CHECK(path.Pop(1));
  SR_CHECK(path.Valid() && path.Path().empty());
}
SR_TEST(coop_apt_ancestry_rejects_other_widgets_and_shared_balance_geometry) {
  for(int invalid=0;invalid<5;++invalid) {
    Ancestry path;auto main=Main(),stamina=Stamina(),meter=Meter(),fill=Fill();
    if(invalid==0)stamina.movie=Movie::Unknown;
    if(invalid==1)stamina.depth=158;
    if(invalid==2)meter.exported=false;
    if(invalid==3)fill.depth=13; // mBalanceFill uses the very same character 21.
    if(invalid==4)fill.root=9000;
    path.Push(main);path.Push(stamina);path.Push(meter);path.Push(fill);
    SR_CHECK(path.SelectedRole()==Role::None);
  }
  Ancestry standalone;standalone.Push(Stamina());standalone.Push(Meter());standalone.Push(Fill());
  SR_CHECK(standalone.SelectedRole()==Role::None);
}
SR_TEST(coop_apt_ancestry_roles_require_the_exact_placement_and_balanced_stack) {
  Ancestry path;path.Push(Main());path.Push(Stamina());path.Push(Meter());
  for(const auto node:std::array<Node,4>{{{4,404,3000,13,24,Movie::GenericMeter,false},
      {4,404,3000,26,30,Movie::GenericMeter,false},{4,404,3000,27,39,Movie::GenericMeter,false},
      {4,404,3000,40,43,Movie::GenericMeter,false}}}) {
    SR_CHECK(path.Push(node));SR_CHECK(path.SelectedRole()!=Role::None);SR_CHECK(path.Pop(4));
  }
  path.Push(Fill());SR_CHECK(!path.Pop(99));SR_CHECK(!path.Valid());
  SR_CHECK(path.SelectedRole()==Role::None);
  Ancestry overflow;
  for(uint32_t i=1;i<=64;++i)SR_CHECK(overflow.Push({i}));
  SR_CHECK(!overflow.Push({65}));SR_CHECK(!overflow.Valid());
  SR_CHECK(overflow.SelectedRole()==Role::None);
}
