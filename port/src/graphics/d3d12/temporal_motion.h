#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <cstdint>
#include <memory>
#include <span>

namespace superman_returns::graphics::d3d12 {

struct MotionGeometry {
  // Nonindexed triangle lists: packed float4, stride 16, offsets divisible by 16.
  // Current is the EXACT final original raster clip, including actual jitter and
  // legacy half-pixel adjustment. Previous is the exact previous-frame clip.
  // Caller separately attests same triangle/vertex ordering and valid history;
  // resource pointers/shader identities are never evidence of correspondence.
  ID3D12Resource* currentClip = nullptr;
  ID3D12Resource* previousClip = nullptr;
  uint64_t current_offset = 0, previous_offset = 0;
  uint32_t vertex_count = 0;
  bool correspondence_valid = false;
  // Previous may be null ONLY when correspondence_valid is false. In that case
  // current geometry still draws, with zero velocity AND validity zero.
  // Supply the original draw's raster state; omitted fields retain old defaults.
  D3D12_CULL_MODE cull_mode = D3D12_CULL_MODE_NONE;
  bool front_counter_clockwise = false;
  int32_t depth_bias = 0;
  float slope_scaled_depth_bias = 0, depth_bias_clamp = 0;
  bool scissor_enabled = false;
  D3D12_RECT scissor{}; // Ignored when disabled (full target); otherwise bounded, empty allowed.
  // Audited original draw's coverage ID. Positive IDs require inputs.coverage
  // and filter replay to that owner. Zero ALWAYS uses legacy unmasked replay,
  // even with a map: opaque draws/unknown invalidators retain original order.
  uint32_t coverage_id = 0;
};
struct TemporalMotionInputs {
  // Single-sample, one mip/layer, matching size, ALLOW_DEPTH_STENCIL texture:
  // R24G8_TYPELESS (D24S8 DSV) or R32_TYPELESS (D32 DSV), in DEPTH_READ state.
  ID3D12Resource* depth = nullptr;
  uint32_t width = 0, height = 0;
  float current_jitter[2]{}, previous_jitter[2]{}; // Actual render pixels: +right/+down.
  // Optional same-device, matching single-sample/mip/layer R32_UINT Texture2D
  // in PIXEL_SHADER_RESOURCE state. Caller attests the exact current frame's
  // LAST AUDITED writer, written by the ACTUAL audited original PS after its
  // alpha/discard/depth tests. Unsupported PS paths must unbind this MRT;
  // retained IDs need not identify the globally nearest/latest scene writer.
  // Preserve original draw order, EQUAL and unmasked unknown invalidators.
  // Zero denotes no audited writer. Keep contents immutable through the slot
  // fence; pointer/size alone cannot prove lineage.
  ID3D12Resource* coverage = nullptr;
};
struct TemporalMotionOutputs {
  ID3D12Resource* velocity = nullptr; // R16G16_FLOAT current->previous in render pixels.
  ID3D12Resource* validity = nullptr; // R8_UINT: 1 valid, 0 invalid/uncovered; invalid velocity=0.
  uint32_t width = 0, height = 0;
};

class TemporalMotion {
 public:
  // Hard per-slot bounds, checked before allocating or recording.
  static constexpr uint32_t kFrameSlots = 3, kMaxDraws = 4096;
  // Cumulative vertices per Render: a 64 MiB packed float4 producer arena,
  // rounded down to whole triangles (4,194,303 vertices; one float4 unused).
  // This bounds submitted GPU work, not a module-owned vertex allocation.
  static constexpr uint32_t kMaxVertices = (64u * 1024 * 1024 / 16 / 3) * 3;
  static constexpr uint32_t kMaxDimension = 8192, kMaxPixels = 16 * 1024 * 1024;
  // Lifetime cache limit, including the two default D32/D24 states. No eviction:
  // queued PSOs remain alive across all slots. New combinations fail at capacity;
  // existing states still work. Scissor is dynamic and consumes no cache entry.
  static constexpr uint32_t kMaxRasterStates = 128;
  TemporalMotion();
  ~TemporalMotion();
  TemporalMotion(const TemporalMotion&) = delete;
  TemporalMotion& operator=(const TemporalMotion&) = delete;
  HRESULT Initialize(ID3D12Device* device);
  // Caller MUST wait for ALL GPU use of this slot before BeginFrame(slot 0..2),
  // and all slots before destruction. No internal fence or implicit waiting.
  // Resets only this slot. One successful Render per BeginFrame; failed input
  // validation can be retried. Output textures may be reused after fenced reset.
  HRESULT BeginFrame(uint32_t slot);
  // DIRECT list on this device, on one caller-synchronized queue. Buffers must
  // be in NON_PIXEL_SHADER_RESOURCE (GENERIC_READ upload buffers also allowed).
  // Caller supplies producer/UAV/streamout barriers and cross-queue fences.
  // All resources and private RTV/DSV/coverage SRV descriptors are retained per
  // slot until its next fenced BeginFrame. Keep input CONTENTS immutable through that fence.
  // Exact raster coverage/depth must match opaque triangles: full viewport,
  // minZ=0/maxZ=1, solid fill, depth clip enabled, and supplied per-draw raster
  // state. Positive coverage IDs additionally filter to surviving pixels from
  // the actual audited original PS; a guessed/CPU mask does not prove alpha or
  // discard behavior. This does not emulate translucency/custom coverage.
  // ID-zero geometry keeps legacy unmasked semantics, including unknown draws
  // writing validity zero under EQUAL. Retain original replay order: audited
  // last-writer IDs are not a global nearest-surface oracle. Caller must still
  // exclude unsupported positive-validity sources. correspondence_valid=false
  // invalidates history and does NOT itself implement alpha/coverage semantics.
  // Cull must be NONE/FRONT/BACK, float biases finite, and enabled scissor must
  // satisfy 0<=left<=right<=width and 0<=top<=bottom<=height (empty is legal).
  // Render sets its own full viewport/per-draw scissor, EQUAL/no writes, and clears
  // both outputs. It clobbers graphics root signature/arguments, PSO, descriptor
  // heaps (binds its private coverage heap), OM RTV/DSV, IA topology,
  // viewport/scissor, predication,
  // and supported VRS state (1x1, PASSTHROUGH combiners, no Tier2 rate image).
  // Rebind caller state afterward; the module never transitions input resources.
  HRESULT Render(ID3D12GraphicsCommandList* list, const TemporalMotionInputs& inputs,
                 std::span<const MotionGeometry> geometry);
  // Borrowed resources in RENDER_TARGET state. Caller may transition for use,
  // but MUST restore RENDER_TARGET before the next BeginFrame of that slot.
  // Every API failure and BeginFrame invalidate Outputs (all-zero struct).
  // Only a successful Render publishes outputs; check HRESULT, never cache this
  // struct as proof of a later success. Prior GPU work remains retained on failure.
  TemporalMotionOutputs Outputs() const;
  const char* LastError() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
