#include "temporal_motion.h"
#include "shaders/temporal_motion_VS.h"
#include "shaders/temporal_motion_PS.h"
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <limits>

namespace superman_returns::graphics::d3d12 {
using Microsoft::WRL::ComPtr;
struct TemporalMotion::Impl {
  struct Slot {
    ComPtr<ID3D12Resource> velocity, validity, depth;
    ComPtr<ID3D12DescriptorHeap> rtv, dsv;
    std::array<ComPtr<ID3D12Resource>, kMaxDraws * 2> buffers;
    uint32_t retained = 0, width = 0, height = 0;
    bool rendered = false;
  };
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12RootSignature> root;
  struct RasterKey {
    D3D12_CULL_MODE cull;
    bool front_ccw;
    int32_t bias;
    float slope, clamp;
    DXGI_FORMAT depth;
    bool operator==(const RasterKey&) const = default;
  };
  struct CachedPipeline {
    RasterKey key{};
    ComPtr<ID3D12PipelineState> state;
  };
  std::array<CachedPipeline,kMaxRasterStates> pipelines;
  uint32_t pipeline_count = 0;
  std::array<Slot, kFrameSlots> slots;
  TemporalMotionOutputs outputs{};
  const char* error = "";
  uint32_t active = kFrameSlots;
  bool initialized = false;
  D3D12_VARIABLE_SHADING_RATE_TIER vrs = D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;

  HRESULT Fail(const char* why, HRESULT hr = E_INVALIDARG) {
    outputs = {}; error = why; return hr;
  }
  bool SameDevice(ID3D12DeviceChild* object) const {
    ComPtr<ID3D12Device> owner;
    return object && SUCCEEDED(object->GetDevice(IID_PPV_ARGS(&owner))) && owner.Get() == device.Get();
  }
  bool Buffer(ID3D12Resource* r, uint64_t offset, uint32_t count) const {
    if (!SameDevice(r) || (offset % 16)) return false;
    const auto d = r->GetDesc();
    const uint64_t bytes = uint64_t(count) * 16;
    const auto address = r->GetGPUVirtualAddress();
    return d.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
           d.Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR && d.Format == DXGI_FORMAT_UNKNOWN &&
           !(d.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) &&
           offset <= d.Width && bytes <= d.Width - offset && address != 0 &&
           offset <= std::numeric_limits<uint64_t>::max() - address;
  }
  HRESULT PipelineFor(const MotionGeometry& raster, DXGI_FORMAT depth,
                      ID3D12PipelineState*& result) {
    const RasterKey key{raster.cull_mode,raster.front_counter_clockwise,raster.depth_bias,
                        raster.slope_scaled_depth_bias,raster.depth_bias_clamp,depth};
    for (uint32_t i=0;i<pipeline_count;++i) if (pipelines[i].key==key) {
      result=pipelines[i].state.Get(); return S_OK;
    }
    if (pipeline_count==kMaxRasterStates)
      return Fail("Raster PSO cache limit reached (128 raster/depth-format combinations)",E_OUTOFMEMORY);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.pRootSignature = root.Get();
    p.VS = {kTemporalMotionVS,sizeof(kTemporalMotionVS)};
    p.PS = {kTemporalMotionPS,sizeof(kTemporalMotionPS)};
    p.BlendState.IndependentBlendEnable = TRUE;
    for (auto& target : p.BlendState.RenderTarget) {
      target.SrcBlend = D3D12_BLEND_ONE; target.DestBlend = D3D12_BLEND_ZERO;
      target.BlendOp = D3D12_BLEND_OP_ADD;
      target.SrcBlendAlpha = D3D12_BLEND_ONE; target.DestBlendAlpha = D3D12_BLEND_ZERO;
      target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
      target.LogicOp = D3D12_LOGIC_OP_NOOP;
      target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    p.SampleMask = UINT_MAX;
    p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    p.RasterizerState.CullMode = key.cull;
    p.RasterizerState.FrontCounterClockwise = key.front_ccw;
    p.RasterizerState.DepthBias = key.bias;
    p.RasterizerState.SlopeScaledDepthBias = key.slope;
    p.RasterizerState.DepthBiasClamp = key.clamp;
    p.RasterizerState.DepthClipEnable = TRUE;
    p.DepthStencilState.DepthEnable = TRUE;
    p.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.NumRenderTargets = 2;
    p.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT; p.RTVFormats[1] = DXGI_FORMAT_R8_UINT;
    p.DSVFormat = depth; p.SampleDesc.Count = 1;
    auto& cached=pipelines[pipeline_count];
    const HRESULT hr=device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&cached.state));
    if (FAILED(hr)) return Fail("Temporal raster graphics PSO creation failed",hr);
    cached.key=key; ++pipeline_count; result=cached.state.Get();
    return S_OK;
  }
};

TemporalMotion::TemporalMotion() : impl_(std::make_unique<Impl>()) {}
TemporalMotion::~TemporalMotion() = default;

HRESULT TemporalMotion::Initialize(ID3D12Device* device) {
  auto& m = *impl_;
  m.outputs = {};
  if (m.initialized) return m.Fail("Initialize may only succeed once", E_UNEXPECTED);
  if (!device) return m.Fail("Initialize requires a device");
  // A failed Initialize has never recorded GPU work, so a retry can discard its
  // partial cache. A successful instance cannot be reinitialized while in flight.
  m.pipelines={}; m.pipeline_count=0;
  m.device = device;
  m.vrs = D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
  D3D12_FEATURE_DATA_D3D12_OPTIONS6 options{};
  if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6,&options,sizeof(options))))
    m.vrs = options.VariableShadingRateTier;
  for (auto format : {DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R8_UINT}) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};
    const HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
    if (FAILED(hr)) return m.Fail("Output format support query failed", hr);
    if (!(support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
      return m.Fail("Required output RTV format unavailable", DXGI_ERROR_UNSUPPORTED);
  }
  D3D12_ROOT_PARAMETER params[3]{};
  for (UINT i = 0; i < 2; ++i) {
    params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[i].Descriptor.ShaderRegister = i;
    params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  }
  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[2].Constants.Num32BitValues = 7;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root{};
  root.NumParameters = 3; root.pParameters = params;
  ComPtr<ID3DBlob> blob, errors;
  HRESULT hr = D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors);
  if (FAILED(hr)) return m.Fail("Temporal root signature serialization failed",hr);
  hr = device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&m.root));
  if (FAILED(hr)) return m.Fail("Temporal root signature creation failed",hr);
  for (auto format : {DXGI_FORMAT_D32_FLOAT,DXGI_FORMAT_D24_UNORM_S8_UINT}) {
    ID3D12PipelineState* unused=nullptr;
    hr=m.PipelineFor({},format,unused);
    if (FAILED(hr)) return hr;
  }
  for (auto& slot : m.slots) {
    D3D12_DESCRIPTOR_HEAP_DESC h{};
    h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; h.NumDescriptors = 2;
    hr = device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&slot.rtv));
    if (FAILED(hr)) return m.Fail("Temporal slot RTV heap creation failed",hr);
    h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; h.NumDescriptors = 1;
    hr = device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&slot.dsv));
    if (FAILED(hr)) return m.Fail("Temporal slot DSV heap creation failed",hr);
  }
  m.initialized = true; m.error = "";
  return S_OK;
}

HRESULT TemporalMotion::BeginFrame(uint32_t slot) {
  auto& m = *impl_;
  m.outputs = {}; m.active = kFrameSlots;
  if (!m.initialized) return m.Fail("BeginFrame requires Initialize",E_UNEXPECTED);
  if (slot >= kFrameSlots) return m.Fail("Frame slot must be 0, 1 or 2 after its caller fence");
  auto& s = m.slots[slot];
  // The caller's fence is the ONLY authority allowing these releases/descriptor rewrites.
  s.depth.Reset();
  for (uint32_t i = 0; i < s.retained; ++i) s.buffers[i].Reset();
  s.retained = 0; s.rendered = false;
  m.active = slot; m.error = "";
  return S_OK;
}

HRESULT TemporalMotion::Render(ID3D12GraphicsCommandList* list,
                                const TemporalMotionInputs& in,
                                std::span<const MotionGeometry> geometry) {
  auto& m = *impl_;
  m.outputs = {};
  if (!m.initialized || m.active >= kFrameSlots)
    return m.Fail("Render requires Initialize and a fenced BeginFrame",E_UNEXPECTED);
  auto& s = m.slots[m.active];
  if (s.rendered) return m.Fail("Only one successful Render is allowed per fenced BeginFrame",E_UNEXPECTED);
  if (!m.SameDevice(list) || list->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT)
    return m.Fail("Render requires this device's DIRECT command list");
  if (!in.width || !in.height || in.width > kMaxDimension || in.height > kMaxDimension ||
      uint64_t(in.width) * in.height > kMaxPixels)
    return m.Fail("Output dimensions exceed the bounded temporal target size");
  for (UINT i = 0; i < 2; ++i)
    if (!std::isfinite(in.current_jitter[i]) || !std::isfinite(in.previous_jitter[i]))
      return m.Fail("Actual pixel jitter must be finite");
  if (!m.SameDevice(in.depth)) return m.Fail("Depth must belong to the initialized device");
  const auto d = in.depth->GetDesc();
  if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.Width != in.width || d.Height != in.height ||
      d.DepthOrArraySize != 1 || d.MipLevels != 1 || d.SampleDesc.Count != 1 || d.SampleDesc.Quality != 0 ||
      !(d.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ||
      (d.Format != DXGI_FORMAT_R24G8_TYPELESS && d.Format != DXGI_FORMAT_R32_TYPELESS))
    return m.Fail("Depth must be matching single-sample R24G8_TYPELESS or R32_TYPELESS depth texture");
  if (geometry.size() > kMaxDraws) return m.Fail("Temporal draw budget exceeded");
  uint64_t vertices = 0;
  for (const auto& g : geometry) {
    vertices += g.vertex_count;
    if (!g.vertex_count || g.vertex_count % 3 || vertices > kMaxVertices)
      return m.Fail("Triangle list count must be positive, divisible by 3 and within the vertex budget");
    if (!m.Buffer(g.currentClip,g.current_offset,g.vertex_count))
      return m.Fail("Current clip must be a same-device SRV buffer with an aligned in-bounds float4 range");
    if (g.previous_offset % 16 || (g.correspondence_valid && !g.previousClip) ||
        (g.previousClip && !m.Buffer(g.previousClip,g.previous_offset,g.vertex_count)))
      return m.Fail("Previous clip range invalid or missing for attested correspondence");
    if (g.cull_mode!=D3D12_CULL_MODE_NONE && g.cull_mode!=D3D12_CULL_MODE_FRONT &&
        g.cull_mode!=D3D12_CULL_MODE_BACK)
      return m.Fail("Cull mode must be NONE, FRONT or BACK");
    if (!std::isfinite(g.slope_scaled_depth_bias) || !std::isfinite(g.depth_bias_clamp))
      return m.Fail("Slope-scaled depth bias and depth bias clamp must be finite");
    if (g.scissor_enabled && (g.scissor.left<0 || g.scissor.top<0 ||
        g.scissor.left>g.scissor.right || g.scissor.top>g.scissor.bottom ||
        g.scissor.right>LONG(in.width) || g.scissor.bottom>LONG(in.height)))
      return m.Fail("Enabled scissor must be ordered and contained in the target");
  }
  ComPtr<ID3D12GraphicsCommandList5> list5;
  if (m.vrs != D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED) {
    const HRESULT hr = list->QueryInterface(IID_PPV_ARGS(&list5));
    if (FAILED(hr)) return m.Fail("Fine per-pixel motion requires a VRS-capable command list",hr);
  }
  // Every required PSO must exist before recording ANY part of the pass. Cache
  // entries never evict, so other slots' queued draws retain their exact PSOs.
  const bool d24 = d.Format == DXGI_FORMAT_R24G8_TYPELESS;
  std::array<ID3D12PipelineState*,kMaxDraws> draw_pipelines{};
  for (size_t i=0;i<geometry.size();++i) {
    const HRESULT hr=m.PipelineFor(geometry[i],d24 ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_D32_FLOAT,
                                  draw_pipelines[i]);
    if (FAILED(hr)) return hr;
  }
  // Validate the ENTIRE batch before touching descriptors or recording commands.
  if (!s.velocity || s.width != in.width || s.height != in.height) {
    std::array<ComPtr<ID3D12Resource>,2> targets;
    for (UINT i = 0; i < 2; ++i) {
      D3D12_RESOURCE_DESC td{};
      td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      td.Width = in.width; td.Height = in.height; td.DepthOrArraySize = td.MipLevels = 1;
      td.Format = i == 0 ? DXGI_FORMAT_R16G16_FLOAT : DXGI_FORMAT_R8_UINT;
      td.SampleDesc.Count = 1; td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
      D3D12_CLEAR_VALUE clear{}; clear.Format = td.Format;
      const HRESULT hr = m.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,
          D3D12_RESOURCE_STATE_RENDER_TARGET,&clear,IID_PPV_ARGS(&targets[i]));
      if (FAILED(hr)) return m.Fail("Bounded temporal output allocation failed",hr);
    }
    s.velocity = targets[0]; s.validity = targets[1]; s.width = in.width; s.height = in.height;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE rt[2]{s.rtv->GetCPUDescriptorHandleForHeapStart()};
  rt[1].ptr = rt[0].ptr + m.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  m.device->CreateRenderTargetView(s.velocity.Get(),nullptr,rt[0]);
  m.device->CreateRenderTargetView(s.validity.Get(),nullptr,rt[1]);
  D3D12_DEPTH_STENCIL_VIEW_DESC view{};
  view.Format = d24 ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_D32_FLOAT;
  view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  view.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
  if (d24) view.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
  const auto ds = s.dsv->GetCPUDescriptorHandleForHeapStart();
  m.device->CreateDepthStencilView(in.depth,&view,ds);
  s.depth = in.depth;
  for (const auto& g : geometry) {
    s.buffers[s.retained++] = g.currentClip;
    if (g.previousClip) s.buffers[s.retained++] = g.previousClip;
  }
  s.rendered = true;
  if (list5) {
    // An inherited coarse rate broadcasts SV_Position/motion across pixels.
    // PASSTHROUGH ignores per-primitive/image rates; Tier2 also unbinds the image.
    const D3D12_SHADING_RATE_COMBINER combiners[]{D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
                                                D3D12_SHADING_RATE_COMBINER_PASSTHROUGH};
    list5->RSSetShadingRate(D3D12_SHADING_RATE_1X1,combiners);
    if (m.vrs >= D3D12_VARIABLE_SHADING_RATE_TIER_2) list5->RSSetShadingRateImage(nullptr);
  }
  list->SetPredication(nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
  list->SetDescriptorHeaps(0,nullptr);
  list->SetGraphicsRootSignature(m.root.Get());
  const float zero[4]{};
  list->ClearRenderTargetView(rt[0],zero,0,nullptr);
  list->ClearRenderTargetView(rt[1],zero,0,nullptr);
  list->OMSetRenderTargets(2,rt,FALSE,&ds);
  const D3D12_VIEWPORT vp{0,0,float(in.width),float(in.height),0,1};
  const D3D12_RECT rect{0,0,LONG(in.width),LONG(in.height)};
  list->RSSetViewports(1,&vp); list->RSSetScissorRects(1,&rect);
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  struct Constants { float dimensions[2], current[2], previous[2]; uint32_t valid; };
  static_assert(sizeof(Constants) == 7 * sizeof(uint32_t));
  Constants constants{{float(in.width),float(in.height)},
                       {in.current_jitter[0],in.current_jitter[1]},
                       {in.previous_jitter[0],in.previous_jitter[1]},0};
  size_t draw=0;
  for (const auto& g : geometry) {
    list->SetPipelineState(draw_pipelines[draw++]);
    const D3D12_RECT scissor=g.scissor_enabled ? g.scissor : rect;
    list->RSSetScissorRects(1,&scissor);
    constants.valid = g.correspondence_valid ? 1 : 0;
    list->SetGraphicsRoot32BitConstants(2,7,&constants,0);
    list->SetGraphicsRootShaderResourceView(0,g.currentClip->GetGPUVirtualAddress()+g.current_offset);
    auto* previous = g.correspondence_valid ? g.previousClip : g.currentClip;
    const uint64_t offset = g.correspondence_valid ? g.previous_offset : g.current_offset;
    list->SetGraphicsRootShaderResourceView(1,previous->GetGPUVirtualAddress()+offset);
    list->DrawInstanced(g.vertex_count,1,0,0);
  }
  m.outputs = {s.velocity.Get(),s.validity.Get(),in.width,in.height}; m.error = "";
  return S_OK;
}

TemporalMotionOutputs TemporalMotion::Outputs() const { return impl_->outputs; }
const char* TemporalMotion::LastError() const { return impl_->error; }
}
