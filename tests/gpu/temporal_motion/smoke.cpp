#include "temporal_motion.h"
#include "fixture_vs.h"
#include "fixture_ps.h"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace superman_returns::graphics::d3d12;
static constexpr UINT W = 32, H = 24;
static void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void Check(HRESULT hr, const char* why) {
  if (FAILED(hr)) {
    std::cerr << why << " HRESULT=0x" << std::hex << uint32_t(hr) << std::dec << '\n';
    throw std::runtime_error(why);
  }
}
static void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r,
                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
  list->ResourceBarrier(1, &b);
}
struct GPU {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence;
  ComPtr<ID3D12InfoQueue> info;
  HANDLE event = nullptr;
  UINT64 serial = 0;
  std::vector<ComPtr<ID3D12Resource>> staging;
  GPU() {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
      debug->EnableDebugLayer();
      ComPtr<ID3D12Debug1> debug1;
      if (SUCCEEDED(debug.As(&debug1))) debug1->SetEnableGPUBasedValidation(TRUE);
      std::cout << "D3D12 debug layer + GPU validation enabled\n";
    } else std::cout << "D3D12 debug layer unavailable; hardware assertions enabled\n";
    ComPtr<IDXGIFactory6> factory;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "factory");
    for (UINT i = 0;; ++i) {
      ComPtr<IDXGIAdapter1> adapter;
      if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                             IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND) break;
      DXGI_ADAPTER_DESC1 d{};
      Check(adapter->GetDesc1(&d), "adapter description");
      if (d.VendorId != 0x10de || (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
      if (std::wstring(d.Description).find(L"RTX 2060") == std::wstring::npos) continue;
      Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                              IID_PPV_ARGS(&device)), "RTX 2060 device");
      std::wcout << L"Hardware: " << d.Description << L'\n';
      break;
    }
    Require(device != nullptr, "RTX 2060 required; no WARP or silent skip");
    device.As(&info);
    D3D12_COMMAND_QUEUE_DESC q{};
    Check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)), "queue");
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&allocator)), "allocator");
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&list)), "list");
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
    event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Require(event != nullptr, "event");
  }
  ~GPU() { if (event) CloseHandle(event); }
  void Submit() {
    Check(list->Close(), "close");
    ID3D12CommandList* commands[]{list.Get()};
    queue->ExecuteCommandLists(1, commands);
    Check(queue->Signal(fence.Get(), ++serial), "signal");
    Check(fence->SetEventOnCompletion(serial, event), "fence event");
    Require(WaitForSingleObject(event, 30000) == WAIT_OBJECT_0, "GPU fence timeout");
    Check(device->GetDeviceRemovedReason(), "device removed");
    bool clean = true;
    if (info) {
      for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
        SIZE_T n = 0;
        Check(info->GetMessage(i, nullptr, &n), "debug size");
        std::vector<char> bytes(n);
        auto* m = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        Check(info->GetMessage(i, m, &n), "debug message");
        if (m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
          std::cerr << m->pDescription << '\n'; clean = false;
        }
      }
      info->ClearStoredMessages();
    }
    Require(clean, "D3D12 debug validation error");
    staging.clear();
    Check(allocator->Reset(), "fenced allocator reset");
    Check(list->Reset(allocator.Get(), nullptr), "list reset");
  }
  ComPtr<ID3D12Resource> Buffer(UINT64 bytes, D3D12_HEAP_TYPE type,
                              D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = type;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes;
    d.Height = d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state,
                                          nullptr, IID_PPV_ARGS(&r)), "buffer");
    return r;
  }
  template<class T> ComPtr<ID3D12Resource> Upload(const T& data, UINT64 prefix = 0) {
    auto upload = Buffer(sizeof(data) + prefix, D3D12_HEAP_TYPE_UPLOAD,
                         D3D12_RESOURCE_STATE_GENERIC_READ);
    void* dst = nullptr; D3D12_RANGE none{0, 0};
    Check(upload->Map(0, &none, &dst), "upload map");
    std::memset(dst, 0, static_cast<size_t>(prefix));
    std::memcpy(static_cast<char*>(dst) + prefix, &data, sizeof(data));
    upload->Unmap(0, nullptr);
    auto r = Buffer(sizeof(data) + prefix, D3D12_HEAP_TYPE_DEFAULT,
                    D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyBufferRegion(r.Get(), 0, upload.Get(), 0, sizeof(data) + prefix);
    Barrier(list.Get(), r.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    staging.push_back(upload);
    return r;
  }
};
struct Clip { float x, y, z, w; };
using Triangle = std::array<Clip, 3>;
static Triangle Fullscreen() { return {{{-1,-1,.5f,1}, {-1,3,.5f,1}, {3,-1,.5f,1}}}; }
static Triangle Shift(Triangle c, float x, float y) {
  for (auto& v : c) { v.x += x * 2 / W * v.w; v.y -= y * 2 / H * v.w; }
  return c;
}
struct Depth {
  ComPtr<ID3D12Resource> texture, coverage;
  ComPtr<ID3D12DescriptorHeap> heap, coverage_heap;
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12PipelineState> pso;
  Depth(GPU& gpu, ID3D12Resource* clip, UINT64 offset, DXGI_FORMAT typeless, bool occlude = false,
        UINT width = W, UINT height = H, UINT samples = 1,
        const MotionGeometry& raster = {}, bool record_coverage = false) {
    const DXGI_FORMAT typed = typeless == DXGI_FORMAT_R24G8_TYPELESS
                                 ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_D32_FLOAT;
    D3D12_HEAP_PROPERTIES props{}; props.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = width; d.Height = height;
    d.DepthOrArraySize = d.MipLevels = 1; d.SampleDesc.Count = samples;
    d.Format = typeless; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{}; clear.Format = typed; clear.DepthStencil.Depth = 1;
    Check(gpu.device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &d,
          D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&texture)), "depth texture");
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; hd.NumDescriptors = 1;
    Check(gpu.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "fixture DSV heap");
    D3D12_DEPTH_STENCIL_VIEW_DESC vd{}; vd.Format = typed;
    vd.ViewDimension = samples == 1 ? D3D12_DSV_DIMENSION_TEXTURE2D : D3D12_DSV_DIMENSION_TEXTURE2DMS;
    auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    gpu.device->CreateDepthStencilView(texture.Get(), &vd, handle);
    // Coplanar background makes a wrongly uncullled/unscissored motion draw
    // observable even where the original draw produced no depth or coverage.
    gpu.list->ClearDepthStencilView(handle, D3D12_CLEAR_FLAG_DEPTH,
                                   record_coverage ? .5f : 1.f, 0, 0, nullptr);
    D3D12_CPU_DESCRIPTOR_HANDLE color{};
    if (record_coverage) {
      auto color_desc=d; color_desc.Format=DXGI_FORMAT_R8_UINT;
      color_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      D3D12_CLEAR_VALUE zero{}; zero.Format=DXGI_FORMAT_R8_UINT;
      Check(gpu.device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&color_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,&zero,IID_PPV_ARGS(&coverage)), "original coverage");
      hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
      Check(gpu.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&coverage_heap)), "coverage RTV");
      color=coverage_heap->GetCPUDescriptorHandleForHeapStart();
      gpu.device->CreateRenderTargetView(coverage.Get(),nullptr,color);
      const float zeros[4]{}; gpu.list->ClearRenderTargetView(color,zeros,0,nullptr);
    }
    if (samples == 1) {
      D3D12_ROOT_PARAMETER param{};
      param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
      param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
      D3D12_ROOT_SIGNATURE_DESC rd{}; rd.NumParameters = 1; rd.pParameters = &param;
      ComPtr<ID3DBlob> blob, errors;
      Check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1,
                                        &blob, &errors), "fixture root serialize");
      Check(gpu.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                             IID_PPV_ARGS(&root)), "fixture root");
      D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
      pd.pRootSignature = root.Get(); pd.VS = {kFixtureVS, sizeof(kFixtureVS)};
      pd.SampleMask = UINT_MAX; pd.SampleDesc.Count = 1;
      pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
      pd.RasterizerState.CullMode = raster.cull_mode;
      pd.RasterizerState.FrontCounterClockwise = raster.front_counter_clockwise;
      pd.RasterizerState.DepthBias = raster.depth_bias;
      pd.RasterizerState.SlopeScaledDepthBias = raster.slope_scaled_depth_bias;
      pd.RasterizerState.DepthBiasClamp = raster.depth_bias_clamp;
      pd.RasterizerState.DepthClipEnable = TRUE;
      pd.DepthStencilState.DepthEnable = TRUE;
      pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
      pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
      pd.DSVFormat = typed; pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      if (record_coverage) {
        pd.PS={kFixturePS,sizeof(kFixturePS)};
        pd.NumRenderTargets=1; pd.RTVFormats[0]=DXGI_FORMAT_R8_UINT;
        auto& blend=pd.BlendState.RenderTarget[0];
        blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;
        blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
        blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
        blend.LogicOp=D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
      }
      Check(gpu.device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)), "fixture depth PSO");
      gpu.list->SetGraphicsRootSignature(root.Get()); gpu.list->SetPipelineState(pso.Get());
      gpu.list->SetGraphicsRootShaderResourceView(0, clip->GetGPUVirtualAddress() + offset);
      D3D12_VIEWPORT vp{0,0,float(width),float(height),0,1};
      D3D12_RECT sc{0,0,LONG(width),LONG(height)};
      if (raster.scissor_enabled) sc=raster.scissor;
      gpu.list->RSSetViewports(1, &vp); gpu.list->RSSetScissorRects(1, &sc);
      gpu.list->OMSetRenderTargets(record_coverage ? 1 : 0,record_coverage ? &color : nullptr,FALSE,&handle);
      gpu.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      gpu.list->DrawInstanced(3,1,0,0);
      if (occlude) {
        D3D12_RECT rect{0,0,LONG(width/2),LONG(height)};
        gpu.list->ClearDepthStencilView(handle, D3D12_CLEAR_FLAG_DEPTH, .25f, 0, 1, &rect);
        // Geometry .5 is LESS than .75, but must fail EQUAL. Catches LESS_EQUAL.
        rect={LONG(width/2),0,LONG(width),LONG(height/3)};
        gpu.list->ClearDepthStencilView(handle, D3D12_CLEAR_FLAG_DEPTH, .75f, 0, 1, &rect);
      }
    }
    Barrier(gpu.list.Get(), texture.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
             D3D12_RESOURCE_STATE_DEPTH_READ);
  }
};
static float Half(uint16_t h) {
  const float magnitude = (h & 0x7c00) ? std::ldexp(float(1024 + (h & 1023)), int((h >> 10) & 31) - 25)
                                     : std::ldexp(float(h & 1023), -24);
  return (h & 0x8000) ? -magnitude : magnitude;
}
struct Readback {
  ComPtr<ID3D12Resource> buffer;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
  UINT64 bytes = 0;
  Readback(GPU& gpu, ID3D12Resource* texture,
           D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RENDER_TARGET) {
    auto d = texture->GetDesc();
    gpu.device->GetCopyableFootprints(&d,0,1,0,&layout,nullptr,nullptr,&bytes);
    buffer = gpu.Buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    Barrier(gpu.list.Get(), texture, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.pResource = buffer.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = layout;
    src.pResource = texture; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    Barrier(gpu.list.Get(), texture, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
  }
  float DepthValue(UINT x, UINT y) {
    void* data = nullptr; D3D12_RANGE r{0,static_cast<SIZE_T>(bytes)};
    Check(buffer->Map(0,&r,&data), "depth map");
    float value;
    std::memcpy(&value,static_cast<char*>(data)+layout.Offset+y*layout.Footprint.RowPitch+x*4,4);
    D3D12_RANGE none{0,0}; buffer->Unmap(0,&none);
    return value;
  }
  std::array<float,2> Velocity(UINT x, UINT y) {
    void* data = nullptr; D3D12_RANGE r{0,static_cast<SIZE_T>(bytes)};
    Check(buffer->Map(0,&r,&data), "velocity map");
    uint16_t halves[2];
    std::memcpy(halves,static_cast<char*>(data)+layout.Offset+y*layout.Footprint.RowPitch+x*4,4);
    D3D12_RANGE none{0,0}; buffer->Unmap(0,&none);
    // Explicitly reject half NaN/Inf; Half() only decodes finite values.
    Require((halves[0]&0x7c00)!=0x7c00 && (halves[1]&0x7c00)!=0x7c00, "nonfinite FP16 output");
    return {Half(halves[0]),Half(halves[1])};
  }
  uint8_t Valid(UINT x, UINT y) {
    void* data = nullptr; D3D12_RANGE r{0,static_cast<SIZE_T>(bytes)};
    Check(buffer->Map(0,&r,&data), "coverage map");
    const auto value = *(static_cast<uint8_t*>(data)+layout.Offset+y*layout.Footprint.RowPitch+x);
    D3D12_RANGE none{0,0}; buffer->Unmap(0,&none);
    return value;
  }
};
static void Pixel(Readback& velocity, Readback& validity, UINT x, UINT y,
                  float dx, float dy, uint8_t valid = 1) {
  const auto v = velocity.Velocity(x,y);
  if (validity.Valid(x,y) != valid || std::abs(v[0]-dx) > .015f || std::abs(v[1]-dy) > .015f) {
    std::cerr << "pixel " << x << ',' << y << " got " << v[0] << ',' << v[1]
              << " coverage " << unsigned(validity.Valid(x,y)) << " expected "
              << dx << ',' << dy << " coverage " << unsigned(valid) << '\n';
    throw std::runtime_error("motion pixel mismatch");
  }
}
static void NoOutputs(const TemporalMotion& motion) {
  const auto o = motion.Outputs();
  Require(!o.velocity && !o.validity && !o.width && !o.height, "stale successful outputs exposed");
  Require(motion.LastError() && *motion.LastError(), "failure has no diagnostic");
}
static void Case(GPU& gpu, TemporalMotion& motion, const char* name, Triangle current,
                 Triangle previous, float dx, float dy, bool correspondence = true,
                 bool absent = false, bool occlude = false,
                 DXGI_FORMAT format = DXGI_FORMAT_R32_TYPELESS,
                 std::array<float,2> jitter = {}, std::array<float,2> previous_jitter = {},
                 bool varying = false, uint8_t valid = 1) {
  static UINT slot = 0;
  Check(motion.BeginFrame(slot++ % 3), motion.LastError());
  auto c = gpu.Upload(current,16);
  ComPtr<ID3D12Resource> p;
  if (!absent) p = gpu.Upload(previous,32);
  Depth depth(gpu,c.Get(),16,format,occlude);
  TemporalMotionInputs in{depth.texture.Get(),W,H,{jitter[0],jitter[1]},
                                               {previous_jitter[0],previous_jitter[1]}};
  MotionGeometry geometry{c.Get(),absent ? nullptr : p.Get(),16,32,3,correspondence};
  Check(motion.Render(gpu.list.Get(),in,{&geometry,1}), motion.LastError());
  auto out = motion.Outputs();
  Require(out.width==W && out.height==H && out.velocity && out.validity, "missing outputs");
  Require(out.velocity->GetDesc().Format==DXGI_FORMAT_R16G16_FLOAT &&
          out.validity->GetDesc().Format==DXGI_FORMAT_R8_UINT, "output formats");
  Readback velocity(gpu,out.velocity), coverage(gpu,out.validity);
  std::unique_ptr<Readback> depth_readback;
  if (occlude && format==DXGI_FORMAT_R32_TYPELESS)
    depth_readback=std::make_unique<Readback>(gpu,depth.texture.Get(),D3D12_RESOURCE_STATE_DEPTH_READ);
  // Drop the caller's input references before execution: the module must retain them.
  c.Reset(); p.Reset(); depth.texture.Reset();
  gpu.Submit();
  if (varying) {
    // Hand-derived screen barycentrics for the oversized triangle and shifts
    // (0,0),(8,-4),(-4,4), despite unequal current clip W=(1,2,4).
    Pixel(velocity,coverage,8,6,2.3854167f,-.92708333f);
    Pixel(velocity,coverage,24,18,-.61458333f,1.0729167f);
  } else {
    if (!correspondence) valid=0;
    Pixel(velocity,coverage,24,12,dx,dy,valid);
    if (occlude) {
      Pixel(velocity,coverage,8,12,0,0,0);
      Pixel(velocity,coverage,24,4,0,0,0);
      if (depth_readback)
        Require(depth_readback->DepthValue(8,12)==.25f &&
                depth_readback->DepthValue(24,12)==.5f &&
                depth_readback->DepthValue(24,4)==.75f, "depth input was written");
    }
    else Pixel(velocity,coverage,8,12,dx,dy,valid);
  }
  std::cout << "PASS: " << name << '\n';
}

static void Validation(GPU& gpu, TemporalMotion& motion) {
  Require(FAILED(motion.BeginFrame(3)), "slot 3 accepted"); NoOutputs(motion);
  Require(FAILED(motion.BeginFrame(UINT_MAX)), "huge slot accepted");
  Check(motion.BeginFrame(0),motion.LastError());
  auto c = gpu.Upload(Fullscreen(),16), p = gpu.Upload(Fullscreen(),32);
  Depth depth(gpu,c.Get(),16,DXGI_FORMAT_R32_TYPELESS);
  TemporalMotionInputs in{depth.texture.Get(),W,H};
  MotionGeometry g{c.Get(),p.Get(),16,32,3,true};
  auto bad = [&](const TemporalMotionInputs& i, const MotionGeometry& geometry) {
    Require(FAILED(motion.Render(gpu.list.Get(),i,{&geometry,1})), "malformed input accepted");
    NoOutputs(motion);
  };
  Require(FAILED(motion.Render(nullptr,in,{&g,1})), "null command list accepted");
  auto i=in; i.depth=nullptr; bad(i,g);
  i=in; i.width=0; bad(i,g);
  i=in; i.height=H+1; bad(i,g);
  i=in; i.width=UINT_MAX; bad(i,g);
  i=in; i.current_jitter[0]=std::numeric_limits<float>::infinity(); bad(i,g);
  i=in; i.previous_jitter[1]=std::numeric_limits<float>::quiet_NaN(); bad(i,g);
  auto b=g; b.currentClip=nullptr; bad(in,b);
  b=g; b.previousClip=nullptr; bad(in,b);
  b=g; b.vertex_count=0; bad(in,b);
  b=g; b.vertex_count=4; bad(in,b);
  b=g; b.vertex_count=UINT_MAX; bad(in,b);
  b=g; b.vertex_count=TemporalMotion::kMaxVertices+3; bad(in,b);
  b=g; b.current_offset=4; bad(in,b);
  b=g; b.previous_offset=8; bad(in,b);
  b=g; b.previousClip=nullptr; b.correspondence_valid=false; b.previous_offset=1; bad(in,b);
  b=g; b.current_offset=UINT64_MAX-15; bad(in,b);
  b=g; b.previous_offset=48; bad(in,b);
  b=g; b.current_offset=32; bad(in,b);
  b=g; b.currentClip=depth.texture.Get(); bad(in,b);
  b=g; b.cull_mode=static_cast<D3D12_CULL_MODE>(0); bad(in,b);
  b=g; b.cull_mode=static_cast<D3D12_CULL_MODE>(4); bad(in,b);
  b=g; b.slope_scaled_depth_bias=std::numeric_limits<float>::infinity(); bad(in,b);
  b=g; b.depth_bias_clamp=std::numeric_limits<float>::quiet_NaN(); bad(in,b);
  for (D3D12_RECT rect : {D3D12_RECT{-1,0,LONG(W),LONG(H)}, {0,-1,LONG(W),LONG(H)},
                         {0,0,LONG(W)+1,LONG(H)}, {0,0,LONG(W),LONG(H)+1},
                         {8,0,7,LONG(H)}, {0,8,LONG(W),7}}) {
    b=g; b.scissor_enabled=true; b.scissor=rect; bad(in,b);
  }
  i=in; i.depth=c.Get(); bad(i,g);
  Depth typed(gpu,c.Get(),16,DXGI_FORMAT_D32_FLOAT);
  i=in; i.depth=typed.texture.Get(); bad(i,g);
  Depth wrong_size(gpu,c.Get(),16,DXGI_FORMAT_R32_TYPELESS,false,W+1,H);
  i=in; i.depth=wrong_size.texture.Get(); bad(i,g);
  Depth msaa(gpu,c.Get(),16,DXGI_FORMAT_R32_TYPELESS,false,W,H,2);
  i=in; i.depth=msaa.texture.Get(); bad(i,g);
  std::vector<MotionGeometry> too_many(TemporalMotion::kMaxDraws+1,g);
  Require(FAILED(motion.Render(gpu.list.Get(),in,too_many)), "draw budget not bounded");
  // A real second device proves pointer/device checks without supplying a broken COM object.
  ComPtr<ID3D12Device> other;
  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter> warp;
  Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "validation factory");
  Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "foreign validation adapter");
  // WARP is used ONLY to create rejected foreign-device inputs, never to render.
  Check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&other)), "foreign device");
  D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC bd=c->GetDesc();
  ComPtr<ID3D12Resource> foreign;
  Check(other->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&foreign)), "foreign buffer");
  b=g; b.currentClip=foreign.Get(); bad(in,b);
  b=g; b.previousClip=foreign.Get(); bad(in,b);
  ComPtr<ID3D12Resource> foreign_depth;
  auto dd=depth.texture->GetDesc();
  Check(other->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&dd,
        D3D12_RESOURCE_STATE_DEPTH_READ,nullptr,IID_PPV_ARGS(&foreign_depth)), "foreign depth");
  i=in; i.depth=foreign_depth.Get(); bad(i,g);
  auto big=gpu.Buffer(uint64_t(TemporalMotion::kMaxVertices)*16,D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  // Round each half in triangle units: the new arena maximum is odd.
  MotionGeometry budget{big.Get(),big.Get(),0,0,(TemporalMotion::kMaxVertices/6+1)*3,true};
  const std::array<MotionGeometry,2> over_budget{budget,budget};
  Require(FAILED(motion.Render(gpu.list.Get(),in,over_budget)), "cumulative vertex budget exceeded");
  // Validate the entire batch before recording, including its last element.
  std::array<MotionGeometry,2> partial{g,g}; partial[1].currentClip=nullptr;
  Require(FAILED(motion.Render(gpu.list.Get(),in,partial)), "partial malformed batch accepted");
  Check(motion.Render(gpu.list.Get(),in,{&g,1}),motion.LastError());
  auto good=motion.Outputs();
  Readback v(gpu,good.velocity), valid(gpu,good.validity);
  Require(FAILED(motion.Render(gpu.list.Get(),in,{&g,1})), "second Render in slot accepted");
  NoOutputs(motion);
  Require(FAILED(motion.Initialize(gpu.device.Get())), "unsafe reinitialize accepted");
  NoOutputs(motion);
  gpu.Submit(); Pixel(v,valid,16,12,0,0);
  // Only after the real fence may BeginFrame release/reuse the slot and its descriptors.
  Check(motion.BeginFrame(0),motion.LastError());
  Check(motion.Render(gpu.list.Get(),in,{}),motion.LastError());
  auto empty=motion.Outputs(); Readback ev(gpu,empty.velocity), ec(gpu,empty.validity);
  gpu.Submit(); Pixel(ev,ec,16,12,0,0,0);
  std::cout << "PASS: resource/device/dimension/format/range/count budgets, failures invalidate outputs, fenced reset/empty clear\n";
}

static void ThreeSlots(GPU& gpu, TemporalMotion& motion) {
  std::array<std::unique_ptr<Depth>,3> depth;
  std::array<std::unique_ptr<Readback>,3> velocity, validity;
  std::array<TemporalMotionOutputs,3> outputs;
  for (UINT slot=0;slot<3;++slot) {
    Check(motion.BeginFrame(slot),motion.LastError());
    Require(!motion.Outputs().velocity, "BeginFrame exposed previous outputs");
    auto clip=Fullscreen();
    for (auto& v:clip) v.z=.25f*float(slot+1);
    auto current=gpu.Upload(clip,16);
    auto previous=gpu.Upload(Shift(clip,float(slot+1),-float(slot+1)),32);
    depth[slot]=std::make_unique<Depth>(gpu,current.Get(),16,
        slot==1 ? DXGI_FORMAT_R24G8_TYPELESS : DXGI_FORMAT_R32_TYPELESS);
    TemporalMotionInputs in{depth[slot]->texture.Get(),W,H};
    MotionGeometry g{current.Get(),previous.Get(),16,32,3,true};
    Check(motion.Render(gpu.list.Get(),in,{&g,1}),motion.LastError());
    outputs[slot]=motion.Outputs();
    for (UINT earlier=0;earlier<slot;++earlier)
      Require(outputs[earlier].velocity!=outputs[slot].velocity &&
              outputs[earlier].validity!=outputs[slot].validity, "outputs alias between slots");
    velocity[slot]=std::make_unique<Readback>(gpu,outputs[slot].velocity);
    validity[slot]=std::make_unique<Readback>(gpu,outputs[slot].validity);
    // All three slots are recorded before a single submission/fence. Resources
    // and descriptor addresses must survive switches to the other two slots.
    depth[slot]->texture.Reset();
  }
  gpu.Submit();
  for (UINT slot=0;slot<3;++slot)
    Pixel(*velocity[slot],*validity[slot],16,12,float(slot+1),-float(slot+1));
  Check(motion.BeginFrame(0),motion.LastError());
  auto current=gpu.Upload(Fullscreen(),16);
  Depth resized(gpu,current.Get(),16,DXGI_FORMAT_R32_TYPELESS,false,W+8,H+4);
  TemporalMotionInputs in{resized.texture.Get(),W+8,H+4};
  Check(motion.Render(gpu.list.Get(),in,{}),motion.LastError());
  auto out=motion.Outputs();
  Require(out.width==W+8 && out.height==H+4, "fenced resize dimensions");
  Readback rv(gpu,out.velocity), rc(gpu,out.validity);
  gpu.Submit(); Pixel(rv,rc,W+7,H+3,0,0,0);
  std::cout << "PASS: three outstanding slots, retained inputs/descriptors, distinct outputs, fenced resize\n";
}

static void InvalidDrawOverwrites(GPU& gpu, TemporalMotion& motion) {
  Check(motion.BeginFrame(0),motion.LastError());
  auto full=gpu.Upload(Fullscreen(),16);
  auto previous=gpu.Upload(Shift(Fullscreen(),3,2),32);
  Triangle triangle{{{-.5f,-.5f,.5f,1},{0,.75f,.5f,1},{.5f,-.5f,.5f,1}}};
  auto triangle_buffer=gpu.Upload(triangle,16);
  Depth depth(gpu,full.Get(),16,DXGI_FORMAT_R32_TYPELESS);
  TemporalMotionInputs in{depth.texture.Get(),W,H};
  std::array<MotionGeometry,2> geometry{{{full.Get(),previous.Get(),16,32,3,true},
                                      {triangle_buffer.Get(),nullptr,16,0,3}}};
  Require(!geometry[1].correspondence_valid, "default correspondence must be false");
  Check(motion.Render(gpu.list.Get(),in,geometry),motion.LastError());
  auto out=motion.Outputs(); Readback v(gpu,out.velocity), coverage(gpu,out.validity);
  gpu.Submit();
  Pixel(v,coverage,16,12,0,0,0); // Skipping the unattested draw leaves stale valid motion here.
  Pixel(v,coverage,2,12,3,2,1);
  Check(motion.BeginFrame(0),motion.LastError());
  MotionGeometry one{triangle_buffer.Get(),previous.Get(),16,32,3,true};
  Check(motion.Render(gpu.list.Get(),in,{&one,1}),motion.LastError());
  out=motion.Outputs(); Readback cleared(gpu,out.velocity), mask(gpu,out.validity);
  gpu.Submit(); Pixel(cleared,mask,2,12,0,0,0); // Uncovered region must clear every frame.
  std::cout << "PASS: unattested current geometry overwrites prior motion; uncovered regions clear\n";
}

static void RasterCase(GPU& gpu, TemporalMotion& motion, const char* name,
                       MotionGeometry raster, bool reverse, bool sloped,
                       DXGI_FORMAT format, bool center_covered = true) {
  Check(motion.BeginFrame(0),motion.LastError());
  auto clip=Fullscreen();
  if (sloped) { clip[0].z=.25f; clip[1].z=.5f; clip[2].z=.75f; }
  if (reverse) std::swap(clip[1],clip[2]);
  auto current=gpu.Upload(clip,16), previous=gpu.Upload(Shift(clip,-2,-1),32);
  auto background=gpu.Upload(Fullscreen(),16);
  auto background_previous=gpu.Upload(Shift(Fullscreen(),3,2),32);
  Depth depth(gpu,current.Get(),16,format,false,W,H,1,raster,true);
  TemporalMotionInputs in{depth.texture.Get(),W,H};
  raster.currentClip=current.Get(); raster.previousClip=previous.Get();
  raster.current_offset=16; raster.previous_offset=32;
  raster.vertex_count=3; raster.correspondence_valid=true;
  const std::array<MotionGeometry,2> geometry{{
      {background.Get(),background_previous.Get(),16,32,3,true},raster}};
  Check(motion.Render(gpu.list.Get(),in,geometry),motion.LastError());
  auto out=motion.Outputs();
  Readback velocity(gpu,out.velocity), valid(gpu,out.validity), original(gpu,depth.coverage.Get());
  gpu.Submit();
  Require(original.Valid(16,12)==uint8_t(center_covered), "original raster fixture winding/coverage mismatch");
  for (UINT y=0;y<H;++y) for (UINT x=0;x<W;++x) {
    const bool foreground=original.Valid(x,y)!=0;
    Pixel(velocity,valid,x,y,foreground ? -2.f : 3.f,foreground ? -1.f : 2.f);
  }
  std::cout << "PASS: original raster coverage/depth matched: " << name << '\n';
}

static void RasterStates(GPU& gpu, TemporalMotion& motion) {
  for (auto format : {DXGI_FORMAT_R32_TYPELESS,DXGI_FORMAT_R24G8_TYPELESS}) {
    MotionGeometry r{};
    r.cull_mode=D3D12_CULL_MODE_BACK;
    RasterCase(gpu,motion,"BACK/clockwise",r,false,false,format,true);
    RasterCase(gpu,motion,"BACK/reversed",r,true,false,format,false);
    r.front_counter_clockwise=true;
    RasterCase(gpu,motion,"BACK/CCW/reversed",r,true,false,format,true);
    RasterCase(gpu,motion,"BACK/CCW/clockwise",r,false,false,format,false);
    r.cull_mode=D3D12_CULL_MODE_FRONT;
    RasterCase(gpu,motion,"FRONT/CCW/clockwise",r,false,false,format,true);
    RasterCase(gpu,motion,"FRONT/CCW/reversed",r,true,false,format,false);
    r={}; r.scissor_enabled=true; r.scissor={4,3,20,17};
    RasterCase(gpu,motion,"bounded scissor",r,false,false,format);
    r.scissor={0,0,0,LONG(H)};
    RasterCase(gpu,motion,"empty scissor",r,false,false,format,false);
    r={}; r.depth_bias=512;
    RasterCase(gpu,motion,"integer bias 512",r,false,false,format);
    r.depth_bias=1024;
    RasterCase(gpu,motion,"integer bias 1024",r,false,false,format);
    r.slope_scaled_depth_bias=.5f;
    RasterCase(gpu,motion,"slope-scaled bias",r,false,true,format);
    r.depth_bias_clamp=.0002f;
    RasterCase(gpu,motion,"positive bias clamp",r,false,true,format);
    r={}; r.depth_bias=-1024; r.slope_scaled_depth_bias=-.5f; r.depth_bias_clamp=-.0002f;
    RasterCase(gpu,motion,"negative bias/clamp",r,false,true,format);
  }
}

static void ScissorReset(GPU& gpu, TemporalMotion& motion) {
  Check(motion.BeginFrame(0),motion.LastError());
  auto current=gpu.Upload(Fullscreen(),16), previous=gpu.Upload(Shift(Fullscreen(),3,2),32);
  Depth depth(gpu,current.Get(),16,DXGI_FORMAT_R32_TYPELESS);
  TemporalMotionInputs in{depth.texture.Get(),W,H};
  MotionGeometry full{current.Get(),previous.Get(),16,32,3,true};
  auto partial=full; partial.scissor_enabled=true; partial.scissor={0,0,LONG(W/2),LONG(H)};
  // The disabled flag must ignore its RECT, and restore full bounds after the
  // preceding draw's explicit partial scissor, within the SAME Render call.
  full.scissor={-10,-10,-5,-5};
  const std::array<MotionGeometry,2> geometry{partial,full};
  Check(motion.Render(gpu.list.Get(),in,geometry),motion.LastError());
  auto out=motion.Outputs(); Readback v(gpu,out.velocity), valid(gpu,out.validity);
  gpu.Submit(); Pixel(v,valid,24,12,3,2); Pixel(v,valid,8,12,3,2);
  std::cout << "PASS: full scissor restored after partial draw; disabled RECT ignored\n";
}

static void RasterBudget(GPU& gpu) {
  TemporalMotion bounded;
  Check(bounded.Initialize(gpu.device.Get()),bounded.LastError());
  Check(bounded.BeginFrame(0),bounded.LastError());
  auto current=gpu.Upload(Fullscreen(),16), previous=gpu.Upload(Fullscreen(),32);
  Depth depth(gpu,current.Get(),16,DXGI_FORMAT_R32_TYPELESS);
  TemporalMotionInputs in{depth.texture.Get(),W,H};
  MotionGeometry g{current.Get(),previous.Get(),16,32,3,true};
  // Two defaults are already cached. The last new bias requests state #129.
  std::vector<MotionGeometry> states(TemporalMotion::kMaxRasterStates-1,g);
  for (size_t i=0;i<states.size();++i) states[i].depth_bias=static_cast<int32_t>(i+1);
  Require(FAILED(bounded.Render(gpu.list.Get(),in,states)), "raster cache capacity not enforced");
  NoOutputs(bounded);
  Require(std::string(bounded.LastError()).find("cache limit")!=std::string::npos,
          "cache budget failure must be explicit");
  // A failed preflight must not consume the frame or break existing PSOs.
  g.slope_scaled_depth_bias=-0.f; g.depth_bias_clamp=-0.f;
  Check(bounded.Render(gpu.list.Get(),in,{&g,1}),bounded.LastError());
  auto out=bounded.Outputs(); Readback v(gpu,out.velocity), valid(gpu,out.validity);
  gpu.Submit(); Pixel(v,valid,16,12,0,0);
  Check(bounded.BeginFrame(1),bounded.LastError());
  g.depth_bias=static_cast<int32_t>(TemporalMotion::kMaxRasterStates-1);
  Require(FAILED(bounded.Render(gpu.list.Get(),in,{&g,1})), "cache limit lost across frame slots");
  NoOutputs(bounded);
  g.depth_bias=0;
  Check(bounded.Render(gpu.list.Get(),in,{&g,1}),bounded.LastError());
  gpu.Submit();
  std::cout << "PASS: lifetime raster cache cap, failed preflight retry, signed-zero state reuse across slots\n";
}

static void VertexBudget(GPU& gpu, TemporalMotion& motion) {
  // Independent, literal producer contract: 64 MiB / float4, rounded down to
  // whole triangles. A 16-byte prefix plus 4,194,303 vertices fills the arena.
  constexpr uint64_t arena_bytes=64ull*1024*1024;
  constexpr uint32_t max_vertices=4194303;
  static_assert((uint64_t(max_vertices)+1)*sizeof(Clip)==arena_bytes);
  auto clips = [&](const Triangle& visible_tail) {
    auto upload=gpu.Buffer(arena_bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped=nullptr; D3D12_RANGE none{0,0};
    Check(upload->Map(0,&none,&mapped), "64 MiB clip upload map");
    auto* data=static_cast<Clip*>(mapped);
    std::fill_n(data,arena_bytes/sizeof(Clip),Clip{0,0,.5f,1});
    // Most triangles have zero area to bound fragment work. The last triangle
    // is visible and moving: its GPU velocity proves loads reach the arena end.
    std::memcpy(data+arena_bytes/sizeof(Clip)-3,visible_tail.data(),sizeof(visible_tail));
    upload->Unmap(0,nullptr);
    auto buffer=gpu.Buffer(arena_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    gpu.list->CopyBufferRegion(buffer.Get(),0,upload.Get(),0,arena_bytes);
    Barrier(gpu.list.Get(),buffer.Get(),D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.staging.push_back(upload);
    return buffer;
  };
  auto current=clips(Fullscreen()), previous=clips(Shift(Fullscreen(),3,-2));
  Depth depth(gpu,current.Get(),arena_bytes-3*sizeof(Clip),DXGI_FORMAT_R32_TYPELESS);
  TemporalMotionInputs in{depth.texture.Get(),W,H};
  MotionGeometry g{current.Get(),previous.Get(),16,16,max_vertices,true};
  Check(motion.BeginFrame(0),motion.LastError());
  HRESULT rendered=motion.Render(gpu.list.Get(),in,{&g,1});
  Check(rendered,motion.LastError());
  auto out=motion.Outputs(); Readback v(gpu,out.velocity), valid(gpu,out.validity);
  gpu.Submit(); Pixel(v,valid,8,6,3,-2); Pixel(v,valid,24,18,3,-2);

  Check(motion.BeginFrame(1),motion.LastError());
  auto reject = [&](const MotionGeometry& geometry) {
    Require(FAILED(motion.Render(gpu.list.Get(),in,{&geometry,1})), "vertex/range overflow accepted");
    NoOutputs(motion);
  };
  auto bad=g; bad.vertex_count=max_vertices+1; reject(bad); // Leftover float4 is not a triangle.
  bad=g; bad.current_offset=32; reject(bad); // Aligned but extends 16 bytes beyond current buffer.
  bad=g; bad.previous_offset=32; reject(bad);
  auto oversized=gpu.Buffer(arena_bytes+64,D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  bad={oversized.Get(),oversized.Get(),0,0,max_vertices+3,true};
  reject(bad); // Range really fits: reject the vertex budget, not resource bounds.
  bad.vertex_count=UINT_MAX; reject(bad); // Divisible by 3, cannot wrap byte/count arithmetic.
  std::array<MotionGeometry,2> aggregate{g,g}; aggregate[1].vertex_count=3;
  Require(FAILED(motion.Render(gpu.list.Get(),in,aggregate)), "cumulative max+3 accepted");
  NoOutputs(motion);
  // Both split counts are valid triangle lists and sum to the literal maximum.
  // The second draw has a large root-SRV offset and again ends at byte 64 MiB.
  aggregate[0].vertex_count=2097150;
  aggregate[1].vertex_count=2097153;
  aggregate[1].current_offset=aggregate[1].previous_offset=16+uint64_t(2097150)*16;
  rendered=motion.Render(gpu.list.Get(),in,aggregate);
  Check(rendered,motion.LastError());
  out=motion.Outputs(); Readback split_v(gpu,out.velocity), split_valid(gpu,out.validity);
  gpu.Submit(); Pixel(split_v,split_valid,8,6,3,-2); Pixel(split_v,split_valid,24,18,3,-2);
  std::cout << "PASS: 4,194,303 GPU vertices (single and cumulative split), moving final triangle at 64 MiB, "
               "max+1/max+3/UINT_MAX/range overflow rejected\n";
}

int main() {
  try {
    GPU gpu;
    TemporalMotion motion;
    TemporalMotion uninitialized;
    Require(FAILED(uninitialized.BeginFrame(0)), "uninitialized BeginFrame accepted");
    Require(FAILED(uninitialized.Render(gpu.list.Get(),{},{})), "uninitialized Render accepted");
    Require(FAILED(uninitialized.Initialize(nullptr)), "null device accepted");
    NoOutputs(uninitialized);
    Check(motion.Initialize(gpu.device.Get()),motion.LastError());
    Require(FAILED(motion.Render(gpu.list.Get(),{},{})), "Render before BeginFrame accepted");
    NoOutputs(motion);
    VertexBudget(gpu,motion);
    auto c=Fullscreen();
    Case(gpu,motion,"camera translation: previous minus current in pixels",c,Shift(c,3,-2),3,-2);
    Case(gpu,motion,"object translation: opposite signs and non-square units",c,Shift(c,-4,5),-4,5);
    auto perspective=c;
    for (UINT k=0;k<3;++k) {
      const float w=float(1u<<k);
      perspective[k].x*=w; perspective[k].y*=w; perspective[k].z*=w; perspective[k].w=w;
    }
    auto deformation=perspective;
    deformation[1].x+=8.f*2/W*deformation[1].w;
    deformation[1].y-= -4.f*2/H*deformation[1].w;
    deformation[2].x+= -4.f*2/W*deformation[2].w;
    deformation[2].y-=4.f*2/H*deformation[2].w;
    Case(gpu,motion,"deformation + perspective-correct previous clip",perspective,deformation,0,0,
         true,false,false,DXGI_FORMAT_R32_TYPELESS,{},{},true);
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 vrs{};
    if (SUCCEEDED(gpu.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6,&vrs,sizeof(vrs))) &&
        vrs.VariableShadingRateTier!=D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED) {
      ComPtr<ID3D12GraphicsCommandList5> list5;
      Check(gpu.list.As(&list5), "VRS command list");
      const D3D12_SHADING_RATE_COMBINER combiners[]{D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
                                                  D3D12_SHADING_RATE_COMBINER_PASSTHROUGH};
      list5->RSSetShadingRate(D3D12_SHADING_RATE_2X2,combiners);
      Case(gpu,motion,"inherited 2x2 VRS must not coarsen velocity",perspective,deformation,0,0,
           true,false,false,DXGI_FORMAT_R32_TYPELESS,{},{},true);
    } else std::cout << "LIMIT: VRS regression unavailable on this device\n";
    Case(gpu,motion,"actual pixel jitter cancels (legacy offset already in clips)",
         Shift(c,-.25f,-.875f),Shift(c,-1.f,-.375f),0,0,true,false,false,
         DXGI_FORMAT_R32_TYPELESS,{.25f,-.375f},{-.5f,.125f});
    Case(gpu,motion,"unattested correspondence emits zero/coverage0",c,Shift(c,3,2),0,0,false,false);
    Case(gpu,motion,"absent previous allowed only unattested",c,c,0,0,false,true);
    for (float w : {0.f,-1.f,std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::quiet_NaN()}) {
      auto invalid=c; for(auto& v:invalid) v.w=w;
      Case(gpu,motion,"invalid previous W is finite zero/coverage0",c,invalid,0,0,true,false,false,
           DXGI_FORMAT_R32_TYPELESS,{},{},false,0);
    }
    auto invalid=c; for(auto& v:invalid) v.x=std::numeric_limits<float>::quiet_NaN();
    Case(gpu,motion,"nonfinite previous XY rejected",c,invalid,0,0,true,false,false,
         DXGI_FORMAT_R32_TYPELESS,{},{},false,0);
    invalid=c; for(auto& v:invalid) v.x=1e6f;
    Case(gpu,motion,"FP16 overflow rejected",c,invalid,0,0,true,false,false,
         DXGI_FORMAT_R32_TYPELESS,{},{},false,0);
    Case(gpu,motion,"D32 EQUAL occlusion",c,Shift(c,2,1),2,1,true,false,true);
    Case(gpu,motion,"D24S8 EQUAL occlusion + read-only stencil",c,Shift(c,-2,-1),-2,-1,
         true,false,true,DXGI_FORMAT_R24G8_TYPELESS);
    RasterStates(gpu,motion);
    ScissorReset(gpu,motion);
    RasterBudget(gpu);
    Validation(gpu,motion);
    ThreeSlots(gpu,motion);
    InvalidDrawOverwrites(gpu,motion);
    gpu.Submit();
    std::cout << "PASS: temporal-motion production GPU backend; synthetic clip fixtures only, no gameplay/DLSS readiness claim\n";
    return 0;
  } catch(const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n'; return 1;
  }
}
