#include "test_main.h"
#include "../../port/src/graphics/observed_skinned_motion.h"
#include <bit>
#include <limits>
using namespace superman_returns::graphics;
namespace {
constexpr uint64_t shader=0xC20921D0DBBD2F1Full;
ObservedRenderInvocation Invocation() {return {{0x20000,7},0x82300328,0x82021D18};}
}
SR_TEST(observed_skin_requires_the_audited_scope_and_nonzero_lifetime) {
  std::array<uint32_t,1024> f{};std::array<uint32_t,4> b{};
  ObservedSkinnedMotionInputs out;out.position_words.fill(17);const auto sentinel=out;
  for(unsigned fault=0;fault<7;++fault) {
    auto i=Invocation();auto vs=shader;
    switch(fault) {
      case 0:vs^=1;break;case 1:i.token.address=0;break;
      case 2:i.token.address=0xFFFF;break;case 3:++i.token.address;break;
      case 4:i.token.generation=0;break;case 5:i.render=0x8230F8E0;break;
      case 6:i.vtable=0x820211C0;break;
    }
    SR_CHECK(!CaptureObservedSkinnedMotionInputs(vs,i,f,b,out));SR_CHECK(out==sentinel);
  }
  SR_CHECK(CaptureObservedSkinnedMotionInputs(shader,Invocation(),f,b,out));
  SR_CHECK(out.owner==Invocation().token);
}
SR_TEST(observed_skin_keeps_actual_palette_boolean_and_projection_inputs) {
  std::array<uint32_t,1024> f{};std::array<uint32_t,4> b{};
  for(size_t j=0;j<f.size();++j) f[j]=std::bit_cast<uint32_t>(float(j));
  b={15,22,33,44};ObservedSkinnedMotionInputs out;
  if(!CaptureObservedSkinnedMotionInputs(shader,Invocation(),f,b,out)) {SR_CHECK(false);return;}
  SR_CHECK(std::equal(f.begin(),f.end(),out.position_words.begin()));
  SR_CHECK(std::equal(b.begin(),b.end(),out.position_words.begin()+1024));
  SR_CHECK(std::equal(f.begin()+32,f.begin()+48,out.clip_rows.begin()));
  const auto old=out;f[100]=std::bit_cast<uint32_t>(-100.f);b[0]=3;
  SR_CHECK(CaptureObservedSkinnedMotionInputs(shader,Invocation(),f,b,out));
  SR_CHECK(out.owner==old.owner);SR_CHECK(out.position_words!=old.position_words);
  SR_CHECK(out.clip_rows==old.clip_rows);
  const auto sentinel=out;f[100]=std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN());
  SR_CHECK(!CaptureObservedSkinnedMotionInputs(shader,Invocation(),f,b,out));SR_CHECK(out==sentinel);
}

SR_TEST(observed_body_skin_requires_exact_body_scope_and_preserves_its_wvp) {
  auto i=Invocation();i.render=0x82306BB0;i.vtable=0x820217E0;
  std::array<uint32_t,1024> f{};std::array<uint32_t,4> b{7,0,0,0};
  for(size_t j=0;j<f.size();++j)f[j]=std::bit_cast<uint32_t>(float(j));
  ObservedSkinnedMotionInputs out;
  SR_CHECK(CaptureObservedSkinnedMotionInputs(0xC687EB65D635C5D7ull,i,f,b,out));
  SR_CHECK(std::equal(f.begin()+32,f.begin()+48,out.clip_rows.begin()));
  SR_CHECK(!CaptureObservedSkinnedMotionInputs(shader,i,f,b,out));
  SR_CHECK(!CaptureObservedSkinnedMotionInputs(0xC687EB65D635C5D7ull,Invocation(),f,b,out));
}
