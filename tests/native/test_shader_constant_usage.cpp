#include "test_main.h"
#include "../../port/src/graphics/guest/shader_constant_usage.h"
#include "../../port/src/graphics/guest/shader_constant_cache.h"
#include "../../port/src/graphics/guest/shader_constant_policy.h"
#include <vector>
using namespace superman_returns::graphics::guest;
SR_TEST(shader_constant_mask_is_experimental_without_an_explicit_environment_override) {
  const char* value=std::getenv("SR_NATIVE_USED_CONSTANT_MASK");
  if(!value || !*value) SR_CHECK(!ShaderConstantMaskEnabled());
}
namespace {
void Put(std::vector<uint8_t>& b,size_t p,uint32_t w) {for(uint32_t i=0;i<4;++i)b[p+i]=uint8_t(w>>(24-i*8));}
std::vector<uint8_t> Program(std::array<uint32_t,3> alu) {
  std::vector<uint8_t> b(88);Put(b,0,0x102a1100);Put(b,4,64);Put(b,8,24);Put(b,24,36);Put(b,40,24);
  Put(b,64,1|(1<<12));Put(b,68,2<<12); // EXEC_END address1 count1, ALU.
  for(size_t i=0;i<3;++i) Put(b,76+i*4,alu[i]);return b;
}
ShaderConstantUsage Used(uint32_t reg) {ShaderConstantUsage u;u.known=true;u.mask[reg/64]|=uint64_t(1)<<(reg%64);return u;}
bool Has(const ShaderConstantUsage& u,uint32_t reg) {return (u.mask[reg/64]>>(reg%64))&1;}
}
SR_TEST(shader_constants_static_sources_cover_all_three_and_endpoints) {
  auto u=AnalyzeShaderConstantUsage(Program({0,0,(64<<8)|255}));
  SR_CHECK(u.known && !u.dynamic);SR_CHECK(Has(u,0)&&Has(u,64)&&Has(u,255));
  SR_CHECK(!Has(u,1));
}
SR_TEST(shader_constants_temp_sources_are_not_float_constants) {
  auto u=AnalyzeShaderConstantUsage(Program({0,0,0xe0000000u|0x00ff8040}));
  SR_CHECK(u.known && !u.dynamic);SR_CHECK((u.mask==std::array<uint64_t,4>{}));
}
SR_TEST(shader_constants_scalar_const_reg_forms_ignore_src3_temp_selector) {
  for(uint32_t opcode=42;opcode<=47;++opcode) {
    auto u=AnalyzeShaderConstantUsage(Program({opcode<<26,0,0xe0000000u|255}));
    SR_CHECK(u.known && Has(u,255));
  }
}
SR_TEST(shader_constants_relative_float_addressing_covers_whole_bank) {
  for(uint32_t relative:{1u<<30,1u<<31}) {
    auto u=AnalyzeShaderConstantUsage(Program({0,relative,0x60050000}));
    SR_CHECK(u.known && u.dynamic);for(auto mask:u.mask) SR_CHECK_EQ(mask,UINT64_MAX);
  }
  auto gpr=AnalyzeShaderConstantUsage(Program({0,1u<<29,0xe0400000u}));
  SR_CHECK(gpr.known && !gpr.dynamic);SR_CHECK((gpr.mask==std::array<uint64_t,4>{}));
}
SR_TEST(shader_constants_unknown_code_and_invalid_container_use_fallback) {
  SR_CHECK(!AnalyzeShaderConstantUsage({}).known);
  auto b=Program({0,0,0xe0000000});b.resize(87);SR_CHECK(!AnalyzeShaderConstantUsage(b).known);
  SR_CHECK(!AnalyzeShaderConstantUsage(Program({51u<<26,0,0xe0000000})).known);
  SR_CHECK(!AnalyzeShaderConstantUsage(Program({0,0,0xe0000000|(30u<<24)})).known);
}
SR_TEST(shader_constant_cache_reuses_omitted_writes_and_detects_shader_expansion) {
  VersionedShaderConstantCache cache;std::array<uint32_t,1024> values{};
  auto words=[&]() -> const auto& {return values;};auto a=Used(0),b=Used(1);
  SR_CHECK(!cache.CanReuse(1,a,words));cache.Commit(1,values);
  values[4]=42;SR_CHECK(cache.CanReuse(2,a,words));
  SR_CHECK(cache.CanReuse(2,a,words)); // proof memoization
  SR_CHECK(!cache.CanReuse(2,b,words)); // c1 in actual uploaded baseline is still zero
  SR_CHECK(!cache.CanReuse(2,ShaderConstantUsage{},words));
  cache.Commit(2,values);SR_CHECK(cache.CanReuse(2,b,words));
  values[0]=7;SR_CHECK(!cache.CanReuse(3,a,words));
}
SR_TEST(shader_constant_cache_never_reads_upload_memory_or_promotes_a_masked_baseline) {
  VersionedShaderConstantCache cache;std::array<uint32_t,1024> values{};cache.Commit(1,values);
  uint32_t reads=0;auto words=[&]() -> const auto& {++reads;return values;};
  SR_CHECK(cache.CanReuse(1,Used(0),words));SR_CHECK_EQ(reads,0u);
  values[4]=42;SR_CHECK(cache.CanReuse(2,Used(0),words));SR_CHECK_EQ(reads,1u);
  SR_CHECK(cache.CanReuse(2,Used(0),words));SR_CHECK_EQ(reads,1u);
  SR_CHECK(!cache.CanReuse(2,Used(1),words));SR_CHECK_EQ(reads,2u);
  cache.Reset();SR_CHECK(!cache.CanReuse(2,Used(0),words));
  cache.Commit(10,values);SR_CHECK(!cache.CanReuse(9,Used(0),words));
  cache.Commit(0,values);SR_CHECK(!cache.CanReuse(0,Used(0),words));
}
SR_TEST(shader_constant_comparison_handles_empty_unknown_and_bit_exact_float_lanes) {
  std::array<uint32_t,1024> a{},b{};b[1023]=0x80000000;
  ShaderConstantUsage empty;empty.known=true;
  SR_CHECK(EqualShaderConstants(a,b,empty));SR_CHECK(!EqualShaderConstants(a,b,{}));
  SR_CHECK(!EqualShaderConstants(a,b,Used(255)));
  b[1023]=0;a[0]=0x7fc00001;b[0]=0x7fc00002;
  SR_CHECK(!EqualShaderConstants(a,b,Used(0))); // caller supplies sanitized words
}
SR_TEST(shader_constant_policy_resolves_reversed_stages_and_conservative_fallback) {
  ShaderCapture vs,ps,other_vs;vs.vertex=other_vs.vertex=true;
  vs.constant_usage=Used(63);ps.constant_usage=Used(255);
  SR_CHECK(ConstantUsageForShaders(&ps,&vs,true,true)==vs.constant_usage);
  SR_CHECK(ConstantUsageForShaders(&ps,&vs,false,true)==ps.constant_usage);
  SR_CHECK(!ConstantUsageForShaders(&vs,&ps,true,false).known);
  SR_CHECK(!ConstantUsageForShaders(&vs,&ps,true,true,false).known);
  SR_CHECK(!ConstantUsageForShaders(&vs,&other_vs,true,true).known);
  SR_CHECK(!ConstantUsageForShaders(&vs,nullptr,false,true).known);
  SR_CHECK(ConstantUsageForShaders(&vs,nullptr,true,true)==vs.constant_usage);
}
SR_TEST(shader_constant_comparison_covers_runs_across_all_mask_blocks_and_dynamic_fallback) {
  std::array<uint32_t,1024> before{},after{};ShaderConstantUsage used;used.known=true;
  for(uint32_t reg:{0u,63u,64u,127u,128u,191u,192u,255u}) used.mask[reg/64]|=uint64_t(1)<<(reg%64);
  for(uint32_t reg=0;reg<256;++reg) {
    after[reg*4+3]=1;SR_CHECK(EqualShaderConstants(before,after,used)==!Has(used,reg));after[reg*4+3]=0;
  }
  used.mask.fill(UINT64_MAX);after[512]=1;SR_CHECK(!EqualShaderConstants(before,after,used));
  used.mask={};used.dynamic=true;SR_CHECK(!EqualShaderConstants(before,after,used));
}
