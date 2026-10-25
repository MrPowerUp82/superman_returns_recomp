#include "test_main.h"
#include "../../port/src/graphics/backend_selection.h"
#include "../../port/src/render_scale_policy.h"
using namespace superman_returns::graphics;
SR_TEST(internal_scale_preserves_guest_resolution_in_native_renderer) {
  using namespace superman_returns::render_scale;
  SR_CHECK_EQ(GuestPercent(75, true), 100);
  SR_CHECK_EQ(GuestPercent(50, true), 100);
  SR_CHECK_EQ(GuestPercent(75, false), 75);
  SR_CHECK_EQ(NativeScale(1.0, 75), 0.75f);
  SR_CHECK_EQ(NativeScale(1.0, 50), 0.5f);
  SR_CHECK_EQ(NativeScale(1.0, 100), 1.0f);
  SR_CHECK_EQ(NativeScale(1.5, 75), 1.5f);
  SR_CHECK_EQ(NativeScale(0.75, 100), 0.75f);
  SR_CHECK_EQ(HostDimension(1280, 0.75f), 960u);
  SR_CHECK_EQ(HostDimension(720, 0.75f), 540u);
  SR_CHECK_EQ(HostDimension(1280, 0.5f), 640u);
  SR_CHECK_EQ(HostDimension(720, 0.5f), 360u);
  SR_CHECK_EQ(HostDimension(1, 0.5f), 1u);
}
SR_TEST(native_vulkan_frame_generation_requires_compiled_backend) {
  NativeVulkanOptions options;options.lsfg=true;std::string error;
  SR_CHECK(ValidateNativeApi(NativeApi::kVulkan,true,options,error,true));
  options.fxaa=true;
  SR_CHECK(!ValidateNativeApi(NativeApi::kVulkan,true,options,error,true));
  SR_CHECK(error.find("FXAA")!=std::string::npos);
}
SR_TEST(native_api_selection_is_explicit_and_rejects_unavailable_vulkan) {
  NativeApi api=NativeApi::kD3D12;std::string error;
  SR_CHECK(ParseNativeApi("d3d12",api,error));SR_CHECK(api==NativeApi::kD3D12);
  SR_CHECK(ParseNativeApi("vulkan",api,error));SR_CHECK(api==NativeApi::kVulkan);
  SR_CHECK(!ParseNativeApi("auto",api,error));SR_CHECK(!error.empty());
  SR_CHECK(!ValidateNativeApi(NativeApi::kVulkan,false,{},error));
  SR_CHECK(error.find("SR_VULKAN_GAME")!=std::string::npos);
  SR_CHECK(ValidateNativeApi(NativeApi::kD3D12,false,{},error));
}
SR_TEST(native_vulkan_rejects_options_it_cannot_apply) {
  std::string error;NativeVulkanOptions options;
  SR_CHECK(ValidateNativeApi(NativeApi::kVulkan,true,options,error));
  options.fxaa=true;SR_CHECK(!ValidateNativeApi(NativeApi::kVulkan,true,options,error));SR_CHECK(error.find("FXAA")!=std::string::npos);
  options={};options.ssao=true;SR_CHECK(!ValidateNativeApi(NativeApi::kVulkan,true,options,error));
  options={};options.lsfg=true;SR_CHECK(!ValidateNativeApi(NativeApi::kVulkan,true,options,error));
  options={};options.render_scale=2;SR_CHECK(!ValidateNativeApi(NativeApi::kVulkan,true,options,error));
  options={};options.msaa_samples=4;SR_CHECK(!ValidateNativeApi(NativeApi::kVulkan,true,options,error));
  options={};options.ab_mode=true;SR_CHECK(!ValidateNativeApi(NativeApi::kVulkan,true,options,error));
  options={};options.shadow_quality=2;SR_CHECK(!ValidateNativeApi(NativeApi::kVulkan,true,options,error));
  options={};options.bloom_quality=2;SR_CHECK(!ValidateNativeApi(NativeApi::kVulkan,true,options,error));
  options={};options.anisotropic_filtering=16;SR_CHECK(!ValidateNativeApi(NativeApi::kVulkan,true,options,error));
  options={};options.foliage_antialiasing=true;SR_CHECK(!ValidateNativeApi(NativeApi::kVulkan,true,options,error));
  SR_CHECK(ValidateNativeApi(NativeApi::kD3D12,false,options,error));
}
