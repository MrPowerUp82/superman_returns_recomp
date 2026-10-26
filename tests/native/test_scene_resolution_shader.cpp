#include "test_main.h"
#include "../../port/src/graphics/scene_resolution_shader.h"
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace superman_returns::graphics;
namespace {
std::string SceneSource(const char* name) {
  const auto root=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
  std::ifstream f(root/"artifacts/shaders/hlsl"/name,std::ios::binary);
  SR_CHECK(f.good());std::string raw{std::istreambuf_iterator<char>(f),{}},lf;
  for(char c:raw) if(c!='\r')lf+=c;
  return lf;
}
constexpr std::string_view original=R"hlsl(float2 getGuestTexture2DDimensions(uint resourceDescriptorIndex, Texture2D<float4> texture)
{
    return float2(getTexture2DDimensions(texture)) / TEX_SCALE(resourceDescriptorIndex);
})hlsl";
}
SR_TEST(scene_resolution_descriptor_grid_keeps_real_descriptor_gamma_and_smoothing) {
  const uint32_t old=0x90000000u | (171u<<16) | 2345u;
  for(uint32_t slot=0;slot<16;++slot) {
    const auto encoded=EncodeSceneTextureGrid(old,slot);
    SR_CHECK((encoded&0x7fffu)==2345u);SR_CHECK((encoded&0x90000000u)==0x90000000u);
    SR_CHECK(encoded&0x40000000u);SR_CHECK(((encoded>>16)&15)==slot);
    SR_CHECK(((encoded>>20)&255)==0);
  }
  SR_CHECK(EncodeSceneTextureGrid(old,16)==old);
  SR_CHECK(EncodeSceneTextureGrid(0,3)==0);
  SR_CHECK(EncodeSceneTextureGrid(2,3)==2);
  SR_CHECK(sizeof(SceneResolutionShaderConstants)==512);
}
SR_TEST(scene_resolution_shader_preserves_original_except_grid_and_pixel_conversion) {
  for(const char* name:{"8A6DD1D52752FAD6.ps.hlsl","71CB82D791DE8E77.ps.hlsl","2C45BFFE76D77398.vs.hlsl"}) {
    const auto source=SceneSource(name);std::string out,error;
    const bool ok=SpecializeSceneResolutionShader(source,out,error);SR_CHECK(ok);
    if(!ok) {std::printf("scene variant: %s\n",error.c_str());continue;}
    const auto begin=out.find(source.substr(0,60));SR_CHECK(begin!=std::string::npos);if(begin==std::string::npos)continue;
    auto restored=out.substr(begin);
    const auto grid=restored.find(SceneResolutionGridFunction());SR_CHECK(grid!=std::string::npos);
    if(grid==std::string::npos)continue;
    restored.replace(grid,SceneResolutionGridFunction().size(),original);
    for(size_t at=0;(at=restored.find("iPos.xy * g_SrScenePixelScale",at))!=std::string::npos;)
      restored.replace(at,std::string_view("iPos.xy * g_SrScenePixelScale").size(),"iPos.xy * g_PixelPosScale");
    SR_CHECK(restored==source);
    SR_CHECK(out.find("register(b6, space4)")!=std::string::npos);
    SR_CHECK(!SpecializeSceneResolutionShader(out,out,error));SR_CHECK(out.empty());
  }
}
SR_TEST(scene_resolution_shader_rejects_changed_or_ambiguous_sampling_contract) {
  const auto source=SceneSource("8A6DD1D52752FAD6.ps.hlsl");std::string out,error;
  for(auto bad:{std::string{},source+std::string(original),std::string("cbuffer SrSceneResolution {}\n")+source}) {
    SR_CHECK(!SpecializeSceneResolutionShader(bad,out,error));SR_CHECK(out.empty());SR_CHECK(!error.empty());
  }
  auto altered=source;const auto at=altered.find(original);SR_CHECK(at!=std::string::npos);
  if(at!=std::string::npos)altered.replace(at,original.size(),"float2 getGuestTexture2DDimensions() {return 0;}");
  SR_CHECK(!SpecializeSceneResolutionShader(altered,out,error));
}
