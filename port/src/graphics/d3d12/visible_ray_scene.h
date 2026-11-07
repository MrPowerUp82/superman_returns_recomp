#pragma once
#include "ray_tracing_scene.h"

namespace superman_returns::graphics::d3d12 {
struct VisibleRayGeometry {
  ID3D12Resource* clips=nullptr; // Exact final original VS float4 clip, shader-readable buffer.
  uint64_t offset=0;
  uint32_t vertex_count=0,stride=16;
  bool opaque=false,same_camera=false,character=false;
};
struct VisibleRayInputs {
  RayTracingTraceInputs scene{};
  float view_projection[16]{}; // Exact final raster transform, inverse of scene.invVP.
  float previous_view_projection[16]{};
  // Optional M1 resources. Velocity R16G16_FLOAT/R32G32_FLOAT, current->previous
  // in full-resolution render pixels; validity R8_UINT/R32_UINT. Both in SRV.
  ID3D12Resource* motion=nullptr;
  ID3D12Resource* validity=nullptr;
  ID3D12Resource* reactivity=nullptr; // Optional R32_UINT accumulated VFX mask; nonzero rejects history.
  float current_jitter[2]{},previous_jitter[2]{};
  uint64_t frame_serial=0;
  bool reset_history=true,previous_projection_valid=false,enable_shadows=true;
  uint32_t reflection_rays=1; // 1/2/4; independently of shadow low/high samples.
  float reflection_distance=10000,reflection_cone_angle=0;
  float trace_scale=1; // .25/.5/1; outputs always match full scene dimensions.
  float depth_rejection=.002f,history_weight=.85f;
};
struct VisibleRayOutputs {
  ID3D12Resource* shadow=nullptr; // R32_FLOAT, -1 invalid, otherwise visibility.
  ID3D12Resource* reflection=nullptr; // RGBA32_FLOAT lit-scene hit color/confidence.
  uint32_t width=0,height=0;
  bool history_used=false;
  ID3D12Resource* counters=nullptr; // Two uint32 counts: valid sun receivers, validated reflection hits; COPY_SOURCE.
  ID3D12Resource* color=nullptr; // Published by Composite, matching scene.color, COPY_SOURCE.
};
// Reuses RayTracingScene for BLAS/TLAS. All GPU inputs/outputs are retained in
// three fenced slots; caller waits a slot before BeginFrame and all slots before
// destruction. One ordered DIRECT/COMPUTE queue, no internal thread/fence/wait.
// Inputs stay immutable until that slot's fence. Methods replace compute PSO,
// root arguments and descriptor heaps; caller rebinds its state afterward.
class VisibleRayScene {
 public:
  static constexpr uint32_t kMaxVertices=4194303;
  VisibleRayScene();
  ~VisibleRayScene();
  HRESULT Initialize(ID3D12Device* device);
  HRESULT BeginFrame(uint32_t slot);
  // Clips are in NON_PIXEL_SHADER_RESOURCE (or GENERIC_READ), with producer
  // barriers supplied by caller. Unattested opaque/same-camera spans are skipped.
  // GPU conversion emits degenerate triangles for nonfinite/invalid homogeneous
  // triples; never feeds NaN/Inf to BLAS. Includes already deformed VS output.
  HRESULT Build(ID3D12GraphicsCommandList* list,std::span<const VisibleRayGeometry> geometry,
                const float* inverse_final_view_projection);
  // Depth/color/motion/validity must be NON_PIXEL_SHADER_RESOURCE, same device,
  // matching single-sample dimensions. Color is retained pre-RT lit scene, never
  // this module's output. Trace hits use projected-depth validation before color.
  // Previous frame filtered data is used only for consecutive serials, matching
  // size, identical estimator settings, valid motion/validity, valid previous
  // projection, and no reset. A live settings change rejects history for one frame.
  HRESULT Trace(ID3D12GraphicsCommandList* list,const VisibleRayInputs& inputs);
  // Separately composite the retained pre-RT lit color and filtered RT results.
  // Writes a distinct matching-format color resource; never samples its target.
  // Shadow attenuation .35+.65*visibility, reflection Schlick F0=.04 times
  // validated hit confidence. This is an explicit material proxy, not a BRDF.
  // color output is COPY_SOURCE for caller CopyResource before DLSS/HUD.
  HRESULT Composite(ID3D12GraphicsCommandList* list);
  // Borrowed outputs in NON_PIXEL_SHADER_RESOURCE. Restore that state after use
  // if needed; failure/BeginFrame invalidates this struct. Check HRESULT.
  VisibleRayOutputs Outputs() const;
  ID3D12Resource* AccelerationStructure() const;
  const char* LastError() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
