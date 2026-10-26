#include "scene_resolution_shader.h"
#include <utility>
namespace superman_returns::graphics {
namespace {
constexpr std::string_view kOriginal=R"hlsl(float2 getGuestTexture2DDimensions(uint resourceDescriptorIndex, Texture2D<float4> texture)
{
    return float2(getTexture2DDimensions(texture)) / TEX_SCALE(resourceDescriptorIndex);
})hlsl";
constexpr std::string_view kGrid=R"hlsl(float2 getGuestTexture2DDimensions(uint resourceDescriptorIndex, Texture2D<float4> texture)
{
    if (resourceDescriptorIndex & 0x40000000u)
        return float2(g_SrSceneTextureGrids[(resourceDescriptorIndex >> 16) & 15u].xy);
    return float2(getTexture2DDimensions(texture)) / TEX_SCALE(resourceDescriptorIndex);
})hlsl";
constexpr std::string_view kPrefix=R"hlsl(// SrSceneResolution v1: exact host-to-guest pixels and per-binding guest grids.
cbuffer SrSceneResolution : register(b6, space4)
{
    float2 g_SrScenePixelScale;
    uint2 g_SrSceneReserved;
    uint4 g_SrSceneTextureGrids[16];
};

)hlsl";
constexpr std::string_view kPixel="iPos.xy * g_PixelPosScale";
constexpr std::string_view kExactPixel="iPos.xy * g_SrScenePixelScale";
}
bool SpecializeSceneResolutionShader(std::string_view source,std::string& output,std::string& error) {
  auto reject=[&](const char* why){output.clear();error=why;return false;};
  if(source.empty() || source.size()>4*1024*1024) return reject("Scene-resolution shader source is empty or exceeds 4 MiB");
  std::string result;result.reserve(source.size()+kPrefix.size()+kGrid.size());
  for(char c:source) if(c!='\r')result+=c;
  if(result.find("SrSceneResolution")!=std::string::npos || result.find("g_SrScene")!=std::string::npos)
    return reject("Scene-resolution shader symbols are already defined");
  const auto grid=result.find(kOriginal);
  if(result.find("#define CONAN_RECOMP")==std::string::npos || grid==std::string::npos ||
     result.find(kOriginal,grid+kOriginal.size())!=std::string::npos)
    return reject("Scene-resolution sampling helper is not the audited emitter contract");
  result.replace(grid,kOriginal.size(),kGrid);
  size_t at=0;
  while((at=result.find(kPixel,at))!=std::string::npos) {
    result.replace(at,kPixel.size(),kExactPixel);at+=kExactPixel.size();
  }
  result.insert(0,kPrefix);output=std::move(result);error.clear();return true;
}
std::string_view SceneResolutionGridFunction(){return kGrid;}
std::string_view SceneResolutionShaderCacheIdentity(){
  static const std::string identity=std::string(kPrefix)+std::string(kOriginal)+std::string(kGrid)+std::string(kExactPixel);
  return identity;
}
}
