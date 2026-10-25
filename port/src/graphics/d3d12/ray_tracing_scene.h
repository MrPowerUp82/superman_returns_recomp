#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <cstdint>
#include <memory>
#include <span>

namespace superman_returns::graphics::d3d12 {

struct RayTracingMaterial {
  float albedo[3]{};  // Linear RGB, [0,1]. Used only when valid is true.
  bool valid = false;
};

struct RayTracingGeometry {
  ID3D12Resource* vertices = nullptr;
  uint64_t offset = 0;
  uint32_t vertex_count = 0;  // Nonindexed world-space float3 triangle list, stride 12.
  bool character = false;
  RayTracingMaterial material{};
};

// Also useful to consumers needing explicit visibility queries. Layout matches HLSL.
struct RayTracingRay {
  float origin[3]{};
  float t_min = 0;
  float direction[3]{};  // Must be finite and unit length.
  float t_max = 0;
  uint32_t mask = 3;  // World=1, character=2.
  uint32_t reserved[3]{};
};
struct RayTracingHit {
  uint32_t hit, instance_id, primitive_id, material_valid;
  float distance, albedo[3];
};
static_assert(sizeof(RayTracingRay) == 48 && sizeof(RayTracingHit) == 32);
struct RayTracingDepthInputs {
  ID3D12Resource* depth=nullptr;
  uint32_t width=0,height=0,grid_width=0,grid_height=0;
  float inv_view_projection[16]{}; // Inverse of the final raster clip transform, including pixel offset.
  float camera[3]{};
  float clear_depth=1;
};
struct RayTracingDepthHit {
  uint32_t valid,hit,primitive_id,reserved;
  float depth,expected_distance,measured_distance,relative_error;
};
static_assert(sizeof(RayTracingDepthHit)==32);

enum class RayTracingShadowQuality : uint32_t { Low = 4, High = 16 };

struct RayTracingTraceInputs {
  ID3D12Resource* depth = nullptr;  // Single-sample 2D R32_FLOAT/TYPELESS or R24G8_TYPELESS (depth plane).
  ID3D12Resource* color = nullptr;  // RGBA8 UNORM, RGBA16_FLOAT or RGBA32_FLOAT.
  uint32_t width = 0, height = 0;
  // Row-major inverse of world->D3D clip. Shader uses mul(matrix, column).
  float inv_view_projection[16]{};
  float camera[3]{};
  float direction_to_light[3]{};
  float cone_half_angle = 0;  // Radians, [0, pi/2). Explicit point/directional cone.
  float ray_bias = 0, max_distance = 0;
  float clear_depth = 1;  // 0 for reverse-Z, 1 for standard Z.
  float max_depth_delta = 0;  // Reject discontinuous/degenerate depth normals.
  bool inv_view_projection_valid = false, camera_valid = false, light_valid = false;
  bool depth_valid = false, color_valid = false;
  bool exclude_character_shadows = false, exclude_character_reflections = false;
  bool enable_reflections = false;
  RayTracingShadowQuality shadow_quality = RayTracingShadowQuality::Low;
};

struct RayTracingOutputs {
  ID3D12Resource* shadow = nullptr;      // R32_FLOAT visibility [0,1], -1 invalid receiver.
  ID3D12Resource* reflection = nullptr;  // RGBA32_FLOAT: hit albedo, alpha=1 iff valid hit material.
  uint32_t width = 0, height = 0;
  bool reflections_available = false;  // Requested and at least one valid material in scene.
};

// Original implementation from the Microsoft DXR specification (not copied sample code).
// Official sources and sample license references are in ray_tracing_scene.cpp.
// No scene hooks or game-data inference. All submitted triangles are opaque.
// Caller validates world-space vertex data on its producer path. Not thread-safe.
class RayTracingScene {
 public:
  static constexpr uint32_t kFrameSlots = 3;
  RayTracingScene();
  ~RayTracingScene();
  RayTracingScene(const RayTracingScene&) = delete;
  RayTracingScene& operator=(const RayTracingScene&) = delete;

  HRESULT Initialize(ID3D12Device* device);
  // Caller MUST wait for this slot's queue fence before BeginFrame, and all slots
  // before destruction. This API cannot infer fence completion. Clears this slot
  // only. No BLAS/TLAS caching by resource pointer; every frame is a fresh build.
  HRESULT BeginFrame(uint32_t slot);
  HRESULT AddTriangles(std::span<const RayTracingGeometry> geometry);
  HRESULT BuildScene(ID3D12GraphicsCommandList* list);
  HRESULT BuildScene(ID3D12GraphicsCommandList* list,
                     std::span<const RayTracingGeometry> geometry);
  // Caller transitions vertex/depth/color inputs to NON_PIXEL_SHADER_RESOURCE
  // (GENERIC_READ is fine for upload). GPU streamout needs its producer barrier.
  // Keep source contents immutable until the build's GPU fence completes.
  // Build/trace on one ordered DIRECT or COMPUTE queue; command list must support
  // list4. Caller synchronizes cross-queue use. Module retains resources per slot.
  // Trace overwrites compute PSO/root state and binds a private descriptor heap;
  // caller must rebind its own state before subsequent rendering/compute work.
  // A failed Trace leaves the previous successful Outputs intact; check HRESULT.
  HRESULT Trace(ID3D12GraphicsCommandList* list, const RayTracingTraceInputs& inputs);
  // results: default-heap UAV buffer, at least rays.size()*sizeof(RayTracingHit),
  // in UNORDERED_ACCESS state. Module emits a UAV barrier, caller does copy/SRV transitions.
  HRESULT TraceRays(ID3D12GraphicsCommandList* list, std::span<const RayTracingRay> rays,
                    ID3D12Resource* results);
  // Diagnostic comparison of depth-unprojected receivers with real triangle
  // hits. Disagreement is evidence against a scene/camera contract, not a
  // reason to mark missing geometry or a guessed projection as valid.
  // results is a grid_width*grid_height RayTracingDepthHit UAV buffer.
  HRESULT ProbeDepth(ID3D12GraphicsCommandList* list,const RayTracingDepthInputs& inputs,
                     ID3D12Resource* results);
  // Outputs stay in UAV state. Caller restores UAV before reuse; valid until slot BeginFrame.
  RayTracingOutputs Outputs() const;
  // Ordered consumers on the build queue may bind this TLAS as a root SRV.
  // Retained by the active slot until its next fenced BeginFrame/destruction.
  ID3D12Resource* AccelerationStructure() const;
  const char* LastError() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace superman_returns::graphics::d3d12
