#include "cpu_bundle.h"
#include "resolve_identity.h"
#include "test_main.h"
#include <bit>
#include <unordered_map>

using namespace superman_returns::native;

SR_TEST(cpu_bundle_selects_all_paths_together_and_preserves_individual_off_options) {
  const auto off=SelectCpuBundle(false,true,false,false,false,0);
  SR_CHECK(!off.tracking_on && !off.bounded_reads && !off.shared_staging && !off.resolve_identity);
  SR_CHECK_EQ(off.srv_capacity,0u);
  const auto on=SelectCpuBundle(true,true,false,false,false,0);
  SR_CHECK(on.tracking_on && on.bounded_reads && on.shared_staging && on.resolve_identity);
  SR_CHECK_EQ(on.srv_capacity,4096u);
  const auto explicit_options=SelectCpuBundle(false,true,true,false,true,123);
  SR_CHECK(!explicit_options.tracking_on && explicit_options.bounded_reads && !explicit_options.shared_staging && explicit_options.resolve_identity);
  SR_CHECK_EQ(explicit_options.srv_capacity,123u);
  SR_CHECK_EQ(SelectCpuBundle(true,true,false,false,false,8192).srv_capacity,8192u);
  SR_CHECK_EQ(SelectCpuBundle(true,true,false,false,false,99999).srv_capacity,8192u);
  const auto vk=SelectCpuBundle(true,false,true,true,true,8192);
  SR_CHECK(!vk.tracking_on && !vk.bounded_reads && !vk.shared_staging && !vk.resolve_identity);
  SR_CHECK_EQ(vk.srv_capacity,0u);
}

SR_TEST(gamma_upload_preserves_all_ramp_bits_without_inspecting_upload_memory) {
  std::array<uint32_t,256> ramp{};
  auto zero=BuildGammaUpload(ramp,true);
  for(auto v:zero)SR_CHECK_EQ(v,0u);
  for(uint32_t i=0;i<ramp.size();++i)ramp[i]=0x80000000u ^ (i*0x1020304u);
  const auto on=BuildGammaUpload(ramp,true),off=BuildGammaUpload(ramp,false);
  for(size_t i=0;i<ramp.size();++i) {
    SR_CHECK_EQ(on[i],ramp[i]);SR_CHECK_EQ(off[i],ramp[i]);
  }
  SR_CHECK_EQ(on[256],1u);SR_CHECK_EQ(off[256],0u);
  for(size_t i=257;i<on.size();++i)SR_CHECK_EQ(on[i],0u);
  const auto invalid=BuildGammaUpload(std::span(ramp).first(255),true);
  for(auto v:invalid)SR_CHECK_EQ(v,0u);
  // The policy consumes the CPU snapshot immediately; later source mutation
  // cannot change the block waiting to be copied to an upload allocation.
  ramp.fill(0);SR_CHECK_EQ(on[0],0x80000000u);
}

SR_TEST(resolve_identity_reuses_alternating_variants_without_replacing_their_resources) {
  ResolveIdentities ids;
  ResolveIdentity base{0x100000,2,640,640,std::bit_cast<uint32_t>(1.0f),1,false,false,false};
  auto larger=base;larger.scale_bits=std::bit_cast<uint32_t>(2.0f);
  const auto a=ids.Get(base),b=ids.Get(larger);
  SR_CHECK(a!=0 && b!=0 && a!=b);
  std::unordered_map<uint64_t,unsigned> simulated_resources;
  unsigned creations=0;
  for(unsigned frame=0;frame<100;++frame) {
    for(auto variant:{base,larger}) {
      const auto id=ids.Get(variant);
      if(!simulated_resources.contains(id))simulated_resources[id]=++creations;
    }
  }
  SR_CHECK_EQ(creations,2u);SR_CHECK_EQ(ids.size(),2u);
  SR_CHECK_EQ(ids.Get(base),a);SR_CHECK_EQ(ids.Get(larger),b);
}

SR_TEST(resolve_identity_keeps_complete_dimensions_and_interpretation_distinct) {
  ResolveIdentities ids;
  const ResolveIdentity base{0x100000,2,640,640,std::bit_cast<uint32_t>(1.0f),1,false,false,false};
  const auto first=ids.Get(base);
  auto check=[&](ResolveIdentity variant) {
    SR_CHECK(ids.Get(variant)!=first);
    const auto id=ids.Get(variant);SR_CHECK_EQ(ids.Get(variant),id);
  };
  auto v=base;v.base+=4096;check(v);
  v=base;v.format+=256;check(v); // old format key truncated to 8 bits
  v=base;v.width+=8192;check(v);
  v=base;v.height+=2048;check(v); // old height key truncated to 11 bits
  v=base;v.scale_bits=std::bit_cast<uint32_t>(1.25f);check(v);
  v=base;v.layers=4;check(v);
  v=base;v.volume=true;check(v);
  v=base;v.depth=true;check(v);
  v=base;v.shadow=true;check(v);
  SR_CHECK_EQ(ids.size(),10u);
}
