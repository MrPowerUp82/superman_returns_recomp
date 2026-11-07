#pragma once
// AUD-02: host-side contract of the hybrid DXR reflection path. DXR visibility
// is combined with projected on-screen radiance; there is no off-screen hit
// radiance, so those fall back to the environment. Non-DXR devices and the
// Vulkan backend never run ray queries.
namespace superman_returns::graphics {

struct HybridReflectionEvaluation {
  bool ray_query_allowed = false;
  bool fallback_to_environment = false;
  float reflection_weight = 0.f;  // roughness attenuation, 1 = mirror
};

inline HybridReflectionEvaluation EvaluateHybridReflectionContract(
    bool dxr_supported, bool is_vulkan, float roughness, bool hit_on_screen) {
  HybridReflectionEvaluation e;
  e.ray_query_allowed = dxr_supported && !is_vulkan;
  const float r = roughness < 0.f ? 0.f : (roughness > 1.f ? 1.f : roughness);
  e.reflection_weight = 1.f - r;
  e.fallback_to_environment = !hit_on_screen;
  return e;
}

}  // namespace superman_returns::graphics
