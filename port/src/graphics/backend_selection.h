#pragma once
#include <string>
#include <string_view>
namespace superman_returns::graphics {
enum class NativeApi {kD3D12,kVulkan};
struct NativeVulkanOptions {
  bool fxaa=false,ssao=false,lsfg=false,ab_mode=false,foliage_antialiasing=false;
  double render_scale=1;
  int msaa_samples=1;
  int shadow_quality=1,bloom_quality=1,anisotropic_filtering=-1;
};
bool ParseNativeApi(std::string_view,NativeApi&,std::string&);
bool ValidateNativeApi(NativeApi,bool vulkan_compiled,const NativeVulkanOptions&,std::string&);
}
