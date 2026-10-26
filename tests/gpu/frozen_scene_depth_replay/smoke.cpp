// Standalone frozen-input DIAGNOSTIC. No original VS, motion/history claim,
// tolerance relaxation of production EQUAL, SDK calls, or changes to input files.
// CLI: --clips PATH --depth PATH --manifest PATH [--json PATH]
//      [--coverage-prefix PATH] [--reference-far 0|16777215]
// Or --self-test. GPU/parser controls ALWAYS precede real-data replay.
// R8 output: 0 uncovered, 1 manifest-known winner, 2 manifest-unknown winner.
// EQUAL forces coverage participation, not the category or temporal identity.
// Reduction hypotheses deliberately force Z writes and ignore original alpha,
// discard, blend/depth controls. Therefore mismatches are measurements, NOT
// assertions. Family EQUAL also forces identity validity: raster coverage only.
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "frozen_VS.h"
#include "frozen_PS.h"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
constexpr uint32_t kDepthMax = 0xffffff, kMaxVertices = 4194304, kMaxDraws = 4096;
constexpr uint64_t kMaxPixels = 16777216, kMaxFileBytes = 512ull * 1024 * 1024;
using Clip = std::array<float, 4>;
static void Require(bool value, const std::string& why) { if (!value) throw std::runtime_error(why); }
static void Check(HRESULT hr, const char* why) {
  if (FAILED(hr)) { std::ostringstream s; s << why << " HRESULT=0x" << std::hex << uint32_t(hr); throw std::runtime_error(s.str()); }
}
static uint32_t U32(const std::vector<uint8_t>& bytes, size_t offset) {
  Require(offset <= bytes.size() && bytes.size() - offset >= 4, "truncated u32");
  uint32_t v; std::memcpy(&v, bytes.data() + offset, 4); return v;
}
static std::vector<uint8_t> ReadFile(const fs::path& path, uint64_t limit = kMaxFileBytes) {
  const auto size = fs::file_size(path); Require(size <= limit, "input exceeds size bound: " + path.string());
  std::ifstream in(path, std::ios::binary); Require(bool(in), "open input: " + path.string());
  std::vector<uint8_t> result(size_t(size), uint8_t{});
  if (size) in.read(reinterpret_cast<char*>(result.data()), std::streamsize(size));
  Require(bool(in) && in.peek() == std::char_traits<char>::eof(), "input truncated or changed: " + path.string());
  return result;
}
struct DepthInput {
  uint32_t width = 0, height = 0, format = 0, pitch = 0;
  std::vector<uint32_t> packed;
};
struct Draw {
  uint32_t index = 0, vertices = 0, first = 0;
  uint64_t shader = 0;
  bool valid = false, front_ccw = false;
  D3D12_CULL_MODE cull = D3D12_CULL_MODE_NONE;
  int32_t bias = 0; float slope = 0;
  D3D12_RECT scissor{};
};
struct Scene { std::vector<Clip> clips; DepthInput depth; std::vector<Draw> draws; bool extra_controls = false; };
static void ValidateSize(uint32_t width, uint32_t height) {
  Require(width && height && width <= 16384 && height <= 16384 && uint64_t(width) * height <= kMaxPixels,
          "invalid or excessive target dimensions");
}
static std::vector<Clip> ParseClips(const std::vector<uint8_t>& bytes) {
  Require(bytes.size() >= 16 && U32(bytes, 0) == 0x53434c50 && U32(bytes, 4) == 1 && U32(bytes, 12) == 16,
          "clip magic/version/stride");
  const uint32_t count = U32(bytes, 8);
  Require(count && count <= kMaxVertices && bytes.size() == 16ull + uint64_t(count) * 16, "clip count/size mismatch");
  std::vector<Clip> clips(count); std::memcpy(clips.data(), bytes.data() + 16, size_t(count) * 16);
  for (const auto& v : clips) for (float component : v) Require(std::isfinite(component), "nonfinite frozen clip");
  return clips;
}
static DepthInput ParseDepth(const std::vector<uint8_t>& bytes) {
  Require(bytes.size() >= 16, "depth header truncated");
  DepthInput d; d.width = U32(bytes, 0); d.height = U32(bytes, 4); d.format = U32(bytes, 8); d.pitch = U32(bytes, 12);
  ValidateSize(d.width, d.height);
  // NVIDIA GetCopyableFootprints may label D24 plane 0 as R32_TYPELESS (39).
  // These are storage labels, never instructions to reinterpret the words as floats.
  Require(d.format == 39 || d.format == 44 || d.format == 45 || d.format == 46, "unsupported D24 plane-0 format");
  Require(d.pitch >= uint64_t(d.width) * 4 && d.pitch <= 4 * 16384 && d.pitch % 4 == 0, "invalid depth row pitch");
  const uint64_t tight_last = 16ull + uint64_t(d.pitch) * (d.height - 1) + uint64_t(d.width) * 4;
  const uint64_t padded_last = 16ull + uint64_t(d.pitch) * d.height;
  Require(bytes.size() == tight_last || bytes.size() == padded_last, "depth payload size/padding mismatch");
  d.packed.resize(size_t(d.width) * d.height);
  for (uint32_t y = 0; y < d.height; ++y)
    std::memcpy(d.packed.data() + size_t(y) * d.width, bytes.data() + 16 + size_t(y) * d.pitch, size_t(d.width) * 4);
  return d;
}
template<class T> static T Integer(const std::string& token, int base = 10) {
  Require(!token.empty(), "empty integer field"); T result{};
  const auto parsed = std::from_chars(token.data(), token.data() + token.size(), result, base);
  Require(parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size(), "invalid integer field: " + token);
  return result;
}
static float Float(const std::string& token) {
  float result{}; const auto parsed = std::from_chars(token.data(), token.data() + token.size(), result);
  Require(parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size() && std::isfinite(result), "invalid slope bias");
  return result;
}
static std::vector<std::string> Fields(std::string line) {
  if (!line.empty() && line.back() == '\r') line.pop_back();
  std::vector<std::string> result; size_t begin = 0;
  for (;;) { const auto end = line.find(',', begin); result.push_back(line.substr(begin, end - begin));
    if (end == std::string::npos) break; begin = end + 1; }
  return result;
}
static std::vector<Draw> ParseManifest(const std::string& text, uint32_t clip_count, uint32_t width, uint32_t height, bool& extras) {
  constexpr const char* base = "draw,offset,vertices,vs,valid,cull,front_ccw,depth_bias,slope_bias,left,top,right,bottom";
  std::istringstream in(text); std::string line; Require(bool(std::getline(in, line)), "empty manifest");
  const auto header = Fields(line), required = Fields(base);
  Require(header.size() >= 13 && header.size() <= 17 && std::equal(required.begin(), required.end(), header.begin()), "manifest header/schema");
  std::set<std::string> names;
  for (size_t i = 13; i < header.size(); ++i) {
    Require(header[i] == "depth_control" || header[i] == "blend_control" || header[i] == "color_control" ||
            header[i] == "color_mask" || header[i] == "blend" || header[i] == "control" || header[i] == "mask", "unknown extra manifest column");
    Require(names.insert(header[i]).second, "duplicate manifest column");
  }
  extras = header.size() > 13; uint64_t last_end = 0; std::vector<Draw> draws;
  while (std::getline(in, line)) {
    const auto f = Fields(line); Require(f.size() == header.size() && draws.size() < kMaxDraws, "manifest row width/draw bound");
    Draw d; d.index = Integer<uint32_t>(f[0]); const auto offset = Integer<uint64_t>(f[1]);
    d.vertices = Integer<uint32_t>(f[2]); d.shader = Integer<uint64_t>(f[3], 16);
    const auto valid = Integer<uint32_t>(f[4]), cull = Integer<uint32_t>(f[5]), front = Integer<uint32_t>(f[6]);
    Require(d.index == draws.size() && offset % 16 == 0 && d.vertices && d.vertices % 3 == 0 &&
            offset >= last_end && offset <= uint64_t(clip_count) * 16 &&
            uint64_t(d.vertices) * 16 <= uint64_t(clip_count) * 16 - offset, "manifest draw/range/order");
    Require(d.shader && valid <= 1 && front <= 1 && cull >= 1 && cull <= 3, "manifest flag/cull/shader");
    d.first = uint32_t(offset / 16); last_end = offset + uint64_t(d.vertices) * 16;
    d.valid = valid != 0; d.front_ccw = front != 0; d.cull = D3D12_CULL_MODE(cull);
    d.bias = Integer<int32_t>(f[7]); d.slope = Float(f[8]);
    d.scissor = {Integer<LONG>(f[9]), Integer<LONG>(f[10]), Integer<LONG>(f[11]), Integer<LONG>(f[12])};
    Require(d.scissor.left >= 0 && d.scissor.top >= 0 && d.scissor.left <= d.scissor.right &&
            d.scissor.top <= d.scissor.bottom && uint64_t(d.scissor.right) <= width && uint64_t(d.scissor.bottom) <= height,
            "manifest scissor outside target/inverted");
    for (size_t i = 13; i < f.size(); ++i) {
      auto token = f[i]; if (token.starts_with("0x") || token.starts_with("0X")) token.erase(0, 2);
      (void)Integer<uint32_t>(token, 16); // Retained metadata only; hypotheses intentionally force depth state.
    }
    draws.push_back(d);
  }
  Require(!draws.empty(), "manifest has no draws"); return draws;
}
static Scene LoadScene(const fs::path& clips, const fs::path& depth, const fs::path& manifest) {
  Scene scene; scene.clips = ParseClips(ReadFile(clips, 16ull + uint64_t(kMaxVertices) * 16));
  scene.depth = ParseDepth(ReadFile(depth)); const auto csv = ReadFile(manifest, 2 * 1024 * 1024);
  scene.draws = ParseManifest(std::string(csv.begin(), csv.end()), uint32_t(scene.clips.size()),
                              scene.depth.width, scene.depth.height, scene.extra_controls); return scene;
}
static void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
  if (from == to) return;
  D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to}; list->ResourceBarrier(1, &b);
}
struct GPU {
  ComPtr<ID3D12Device> device; ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence; ComPtr<ID3D12InfoQueue> info;
  HANDLE event = nullptr; uint64_t serial = 0; bool debug = false; std::string adapter_name;
  GPU() {
    ComPtr<ID3D12Debug> layer; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)))) { layer->EnableDebugLayer(); debug = true; }
    ComPtr<IDXGIFactory6> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
    for (UINT i = 0;; ++i) {
      ComPtr<IDXGIAdapter1> adapter;
      const auto hr = factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
      if (hr == DXGI_ERROR_NOT_FOUND) break; Check(hr, "enumerate hardware");
      DXGI_ADAPTER_DESC1 d{}; Check(adapter->GetDesc1(&d), "adapter description");
      if (d.VendorId != 0x10de || (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || std::wstring(d.Description).find(L"RTX 2060") == std::wstring::npos) continue;
      Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)), "RTX 2060 device");
      const int n = WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, nullptr, 0, nullptr, nullptr);
      std::vector<char> name(size_t(n), char{}); WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name.data(), n, nullptr, nullptr);
      adapter_name = name.data(); break;
    }
    Require(device != nullptr, "actual RTX 2060 required; no WARP/no skips"); (void)device.As(&info);
    D3D12_COMMAND_QUEUE_DESC q{}; Check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)), "queue");
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "allocator");
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "command list");
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
    event = CreateEventW(nullptr, FALSE, FALSE, nullptr); Require(event != nullptr, "fence event");
  }
  ~GPU() { if (event) CloseHandle(event); }
  void Submit() {
    Check(list->Close(), "close"); ID3D12CommandList* commands[]{list.Get()}; queue->ExecuteCommandLists(1, commands);
    Check(queue->Signal(fence.Get(), ++serial), "signal"); Check(fence->SetEventOnCompletion(serial, event), "fence completion");
    Require(WaitForSingleObject(event, 30000) == WAIT_OBJECT_0, "GPU fence timeout"); Check(device->GetDeviceRemovedReason(), "device removed");
    bool clean = true;
    if (info) {
      for (uint64_t i = 0; i < info->GetNumStoredMessages(); ++i) {
        SIZE_T size = 0; Check(info->GetMessage(i, nullptr, &size), "debug message size"); std::vector<char> bytes(size);
        auto* m = reinterpret_cast<D3D12_MESSAGE*>(bytes.data()); Check(info->GetMessage(i, m, &size), "debug message");
        if (m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) { std::cerr << m->pDescription << '\n'; clean = false; }
      }
      info->ClearStoredMessages();
    }
    Require(clean, "D3D12 validation error"); Check(allocator->Reset(), "allocator reset"); Check(list->Reset(allocator.Get(), nullptr), "list reset");
  }
  ComPtr<ID3D12Resource> Buffer(uint64_t bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
    Require(bytes > 0, "empty GPU buffer"); D3D12_HEAP_PROPERTIES heap{}; heap.Type = type; D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes; d.Height = 1; d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; ComPtr<ID3D12Resource> r;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)), "buffer"); return r;
  }
  ComPtr<ID3D12Resource> Upload(const void* data, uint64_t size) {
    auto r = Buffer(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped = nullptr; const D3D12_RANGE none{0,0}; Check(r->Map(0, &none, &mapped), "upload map");
    std::memcpy(mapped, data, size_t(size)); r->Unmap(0, nullptr); return r;
  }
};
struct Readback {
  ComPtr<ID3D12Resource> buffer; D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{}; uint64_t size = 0;
  Readback(GPU& gpu, ID3D12Resource* resource, D3D12_RESOURCE_STATES state) {
    const auto d = resource->GetDesc(); gpu.device->GetCopyableFootprints(&d, 0, 1, 0, &layout, nullptr, nullptr, &size);
    buffer = gpu.Buffer(size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(gpu.list.Get(), resource, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION to{}, from{}; to.pResource = buffer.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint = layout;
    from.pResource = resource; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr); Transition(gpu.list.Get(), resource, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
  }
  std::vector<uint8_t> Tight(uint32_t width, uint32_t height, uint32_t bpp) const {
    Require(layout.Footprint.Width == width && layout.Footprint.Height == height && layout.Footprint.RowPitch >= width * bpp, "readback footprint");
    const D3D12_RANGE range{0, SIZE_T(size)}; void* mapped = nullptr; Check(buffer->Map(0, &range, &mapped), "readback map");
    std::vector<uint8_t> result(size_t(width) * height * bpp);
    for (uint32_t y = 0; y < height; ++y) std::memcpy(result.data() + size_t(y) * width * bpp,
        static_cast<const uint8_t*>(mapped) + layout.Offset + size_t(y) * layout.Footprint.RowPitch, size_t(width) * bpp);
    const D3D12_RANGE none{0,0}; buffer->Unmap(0, &none); return result;
  }
};
struct Targets {
  ComPtr<ID3D12Resource> depth, coverage, depth_upload;
  ComPtr<ID3D12DescriptorHeap> dsv_heap, rtv_heap;
  D3D12_CPU_DESCRIPTOR_HANDLE dsv{}, rtv{};
  bool original = false;
  Targets(GPU& gpu, uint32_t width, uint32_t height, float clear, const DepthInput* source) : original(source != nullptr) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT; D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = width; d.Height = height; d.DepthOrArraySize = d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R24G8_TYPELESS; d.SampleDesc.Count = 1; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv{}; cv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; cv.DepthStencil.Depth = clear;
    const auto initial = original ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_DEPTH_WRITE;
    Check(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, initial, original ? nullptr : &cv, IID_PPV_ARGS(&depth)), "private D24");
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; hd.NumDescriptors = 1;
    Check(gpu.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsv_heap)), "DSV heap"); dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_DEPTH_STENCIL_VIEW_DESC vd{}; vd.Format = cv.Format; vd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    if (original) vd.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL;
    gpu.device->CreateDepthStencilView(depth.Get(), &vd, dsv);
    if (original) {
      Require(source->width == width && source->height == height, "source depth size");
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{}; uint64_t size = 0;
      gpu.device->GetCopyableFootprints(&d, 0, 1, 0, &layout, nullptr, nullptr, &size);
      std::vector<uint8_t> bytes(size_t(size), uint8_t{});
      for (uint32_t y = 0; y < height; ++y) std::memcpy(bytes.data() + layout.Offset + size_t(y) * layout.Footprint.RowPitch,
          source->packed.data() + size_t(y) * width, size_t(width) * 4);
      depth_upload = gpu.Upload(bytes.data(), size);
      D3D12_TEXTURE_COPY_LOCATION to{}, from{}; to.pResource = depth.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      from.pResource = depth_upload.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint = layout;
      gpu.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
      Transition(gpu.list.Get(), depth.Get(), initial, D3D12_RESOURCE_STATE_DEPTH_READ);
    } else gpu.list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, clear, 0x5a, 0, nullptr);
    d.Format = DXGI_FORMAT_R8_UINT; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE color{}; color.Format = d.Format;
    Check(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, &color, IID_PPV_ARGS(&coverage)), "R8 coverage");
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; Check(gpu.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_heap)), "RTV heap");
    rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart(); gpu.device->CreateRenderTargetView(coverage.Get(), nullptr, rtv);
    const float zero[4]{}; gpu.list->ClearRenderTargetView(rtv, zero, 0, nullptr);
  }
};
enum class Reduction { Forward, Reverse, Equal };
struct RasterKey {
  Reduction reduction; D3D12_CULL_MODE cull; bool ccw; int32_t bias; uint32_t slope;
  bool operator<(const RasterKey& b) const { return std::tie(reduction,cull,ccw,bias,slope) < std::tie(b.reduction,b.cull,b.ccw,b.bias,b.slope); }
};
struct Result { std::vector<uint32_t> depth; std::vector<uint8_t> coverage; uint64_t draws = 0, vertices = 0; uint32_t footprint_format = 0; };
struct Replay {
  GPU& gpu; ComPtr<ID3D12RootSignature> root; std::map<RasterKey, ComPtr<ID3D12PipelineState>> pipelines;
  explicit Replay(GPU& g) : gpu(g) {
    D3D12_ROOT_PARAMETER p[2]{}; p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[0].Descriptor = {0,0}; p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[1].Constants = {0,0,2}; p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rd{}; rd.NumParameters = 2; rd.pParameters = p;
    ComPtr<ID3DBlob> blob, error; Check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), "root serialization");
    Check(gpu.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)), "root signature");
  }
  ID3D12PipelineState* Pipeline(const Draw& draw, Reduction reduction) {
    RasterKey key{reduction,draw.cull,draw.front_ccw,draw.bias,std::bit_cast<uint32_t>(draw.slope)};
    auto found = pipelines.find(key); if (found != pipelines.end()) return found->second.Get();
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{}; d.pRootSignature = root.Get();
    d.VS = {kFrozenVS,sizeof(kFrozenVS)}; d.PS = {kFrozenPS,sizeof(kFrozenPS)};
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED; d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; d.RasterizerState.CullMode = draw.cull;
    d.RasterizerState.FrontCounterClockwise = draw.front_ccw; d.RasterizerState.DepthBias = draw.bias;
    d.RasterizerState.SlopeScaledDepthBias = draw.slope; d.RasterizerState.DepthClipEnable = TRUE;
    d.DepthStencilState.DepthEnable = TRUE;
    d.DepthStencilState.DepthWriteMask = reduction == Reduction::Equal ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
    d.DepthStencilState.DepthFunc = reduction == Reduction::Equal ? D3D12_COMPARISON_FUNC_EQUAL :
        reduction == Reduction::Reverse ? D3D12_COMPARISON_FUNC_GREATER_EQUAL : D3D12_COMPARISON_FUNC_LESS_EQUAL;
    d.DepthStencilState.FrontFace = d.DepthStencilState.BackFace = {D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; d.NumRenderTargets = 1; d.RTVFormats[0] = DXGI_FORMAT_R8_UINT;
    d.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT; d.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline; Check(gpu.device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pipeline)), "replay PSO");
    return pipelines.emplace(key, std::move(pipeline)).first->second.Get();
  }
  Result Run(const Scene& scene, ID3D12Resource* clips, Reduction reduction, bool known_only, std::optional<uint64_t> family = {}) {
    const auto width = scene.depth.width, height = scene.depth.height;
    Targets targets(gpu, width, height, reduction == Reduction::Forward ? 1.f : 0.f,
                     reduction == Reduction::Equal ? &scene.depth : nullptr);
    gpu.list->SetGraphicsRootSignature(root.Get()); gpu.list->SetDescriptorHeaps(0, nullptr);
    gpu.list->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    gpu.list->SetGraphicsRootShaderResourceView(0, clips->GetGPUVirtualAddress());
    gpu.list->OMSetRenderTargets(1, &targets.rtv, FALSE, &targets.dsv);
    const D3D12_VIEWPORT vp{0,0,float(width),float(height),0,1}; gpu.list->RSSetViewports(1, &vp);
    gpu.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Result result;
    for (const auto& draw : scene.draws) {
      if ((known_only && !draw.valid) || (family && draw.shader != *family)) continue;
      gpu.list->SetPipelineState(Pipeline(draw,reduction)); gpu.list->RSSetScissorRects(1, &draw.scissor);
      const uint32_t constants[]{draw.first, draw.valid ? 1u : 2u};
      gpu.list->SetGraphicsRoot32BitConstants(1, 2, constants, 0); gpu.list->DrawInstanced(draw.vertices, 1, 0, 0);
      ++result.draws; result.vertices += draw.vertices;
    }
    const auto depth_state = targets.original ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE;
    Readback depth(gpu, targets.depth.Get(), depth_state), coverage(gpu, targets.coverage.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    gpu.Submit(); const auto bytes = depth.Tight(width,height,4); result.depth.resize(size_t(width) * height);
    std::memcpy(result.depth.data(), bytes.data(), bytes.size()); result.coverage = coverage.Tight(width,height,1);
    result.footprint_format = uint32_t(depth.layout.Footprint.Format); return result;
  }
};
struct Metrics {
  uint64_t pixels = 0, covered = 0, uncovered = 0, known = 0, unknown = 0, source_far = 0;
  uint64_t uncovered_nonfar = 0, covered_nonfar = 0, covered_far = 0;
  uint64_t equal = 0, within1 = 0, within2 = 0, within16 = 0, positive = 0, negative = 0;
  uint64_t all_equal = 0, max_abs = 0; int64_t delta_sum = 0;
  std::array<uint64_t,7> bins{}; // |delta|: 0,1,2,3..16,17..255,256..4095,>=4096.
};
static Metrics Measure(const Result& replay, const DepthInput& source, uint32_t reference_far) {
  Require(replay.depth.size() == source.packed.size() && replay.coverage.size() == replay.depth.size(), "metric dimensions");
  Metrics m; m.pixels = source.packed.size();
  for (size_t i = 0; i < source.packed.size(); ++i) {
    const auto original = source.packed[i] & kDepthMax, actual = replay.depth[i] & kDepthMax;
    const bool is_far = original == reference_far; m.source_far += is_far; m.all_equal += original == actual;
    Require(replay.coverage[i] <= 2, "invalid GPU coverage category");
    if (!replay.coverage[i]) { ++m.uncovered; m.uncovered_nonfar += !is_far; continue; }
    ++m.covered; m.known += replay.coverage[i] == 1; m.unknown += replay.coverage[i] == 2;
    m.covered_far += is_far; m.covered_nonfar += !is_far;
    const int64_t delta = int64_t(actual) - original; const uint64_t absolute = uint64_t(delta < 0 ? -delta : delta);
    m.delta_sum += delta; m.positive += delta > 0; m.negative += delta < 0; m.max_abs = std::max(m.max_abs,absolute);
    m.equal += absolute == 0; m.within1 += absolute <= 1; m.within2 += absolute <= 2; m.within16 += absolute <= 16;
    ++m.bins[absolute == 0 ? 0 : absolute == 1 ? 1 : absolute == 2 ? 2 : absolute <= 16 ? 3 : absolute <= 255 ? 4 : absolute <= 4095 ? 5 : 6];
  }
  return m;
}
static std::string JsonString(const std::string& input) {
  std::ostringstream out; out << '"';
  for (unsigned char c : input) {
    if (c == '\\' || c == '"') out << '\\' << char(c);
    else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
    else out << char(c);
  }
  out << '"'; return out.str();
}
static void PrintMetrics(std::ostream& out, const Metrics& m) {
  out << "{\"pixels\":" << m.pixels << ",\"covered\":" << m.covered << ",\"uncovered\":" << m.uncovered
      << ",\"known_winner\":" << m.known << ",\"unknown_winner\":" << m.unknown << ",\"source_far\":" << m.source_far
      << ",\"uncovered_source_nonfar\":" << m.uncovered_nonfar << ",\"covered_source_nonfar\":" << m.covered_nonfar
      << ",\"covered_source_far\":" << m.covered_far << ",\"covered_equal\":" << m.equal << ",\"covered_within1\":" << m.within1
      << ",\"covered_within2\":" << m.within2 << ",\"covered_within16\":" << m.within16
      << ",\"positive_delta\":" << m.positive << ",\"negative_delta\":" << m.negative << ",\"delta_sum\":" << m.delta_sum
      << ",\"max_abs_delta\":" << m.max_abs << ",\"all_pixels_equal_including_uncovered\":" << m.all_equal << ",\"abs_delta_bins\":[";
  for (size_t i = 0; i < m.bins.size(); ++i) { if (i) out << ','; out << m.bins[i]; } out << "]}";
}
static Scene Synthetic(float z) {
  Scene s; s.depth.width = s.depth.height = 64; s.depth.format = 39; s.depth.pitch = 256; s.depth.packed.resize(4096);
  s.clips = {{-1,1,z,1},{3,1,z,1},{-1,-3,z,1}};
  Draw d; d.vertices = 3; d.shader = 1; d.valid = true; d.scissor = {0,0,64,64}; s.draws.push_back(d); return s;
}
template<class F> static void Rejects(F&& f, const char* why) {
  bool rejected = false; try { f(); } catch (const std::runtime_error&) { rejected = true; } Require(rejected, why);
}
static void ParserControls() {
  Rejects([] { (void)ParseClips({}); }, "empty clip input must reject");
  std::vector<uint8_t> clip(64); const uint32_t ch[]{0x53434c50,1,3,16}; std::memcpy(clip.data(),ch,16);
  const auto values = Synthetic(.5f).clips; std::memcpy(clip.data()+16,values.data(),48); Require(ParseClips(clip).size()==3,"clip parser control");
  auto bad = clip; bad[0] = 0; Rejects([&] { (void)ParseClips(bad); }, "bad clip magic must reject");
  bad = clip; bad.pop_back(); Rejects([&] { (void)ParseClips(bad); }, "truncated clips must reject");
  bad = clip; const float nan = std::numeric_limits<float>::quiet_NaN(); std::memcpy(bad.data()+16,&nan,4);
  Rejects([&] { (void)ParseClips(bad); }, "nonfinite clips must reject");
  const std::string header="draw,offset,vertices,vs,valid,cull,front_ccw,depth_bias,slope_bias,left,top,right,bottom\n";
  bool extras=false; Require(ParseManifest(header+"0,0,3,abcd,1,1,0,0,0,0,0,64,64\n",3,64,64,extras).size()==1,"manifest parser control");
  for (const auto* row : {"0,16,3,abcd,1,1,0,0,0,0,0,64,64\n", "0,0,3,abcd,1,4,0,0,0,0,0,64,64\n",
                         "0,0,3,abcd,1,1,0,0,nan,0,0,64,64\n", "0,0,3,abcd,1,1,0,0,0,0,0,65,64\n"})
    Rejects([&] { (void)ParseManifest(header+row,3,64,64,extras); }, "bad manifest must reject");
  std::vector<uint8_t> depth(16+256); const uint32_t dh[]{3,2,39,128}; std::memcpy(depth.data(),dh,16);
  const uint32_t sentinel=12345; std::memcpy(depth.data()+16+128,&sentinel,4);
  Require(ParseDepth(depth).packed[3]==sentinel,"padded depth rows and payload offset");
  depth.resize(16+128+12); Require(ParseDepth(depth).packed[3]==sentinel,"tight last depth row");
  depth.pop_back(); Rejects([&] { (void)ParseDepth(depth); }, "truncated depth must reject");
}
static void SelfTest(GPU& gpu, Replay& replay) {
  ParserControls(); auto scene = Synthetic(.5f); auto upload = gpu.Upload(scene.clips.data(),scene.clips.size()*sizeof(Clip));
  auto flat = replay.Run(scene,upload.Get(),Reduction::Forward,false);
  Require(std::all_of(flat.coverage.begin(),flat.coverage.end(),[](uint8_t v){return v==1;}),"matching flat coverage must cover 4096 centers");
  const auto expected = uint32_t(std::llround(.5 * kDepthMax));
  for (auto v : flat.depth) Require(std::abs(int64_t(v&kDepthMax)-expected)<=1,"D24 flat quantization");
  scene.depth.packed=flat.depth; auto equal=replay.Run(scene,upload.Get(),Reduction::Equal,false);
  Require(Measure(equal,scene.depth,0).equal==4096 && Measure(equal,scene.depth,0).covered==4096,"uploaded original D24 EQUAL control");
  auto unknown_scene=scene; unknown_scene.draws[0].valid=false;
  const auto unknown_equal=replay.Run(unknown_scene,upload.Get(),Reduction::Equal,false);
  const auto unknown_metrics=Measure(unknown_equal,scene.depth,0);
  Require(unknown_metrics.covered==4096 && unknown_metrics.unknown==4096 && unknown_metrics.known==0,
          "EQUAL unknown geometry participates without being relabeled known");
  for (const auto reduction : {Reduction::Forward,Reduction::Reverse}) {
    auto empty = scene; empty.draws.clear(); auto clear=replay.Run(empty,upload.Get(),reduction,false);
    const uint32_t word = reduction==Reduction::Forward ? kDepthMax : 0;
    for (auto v : clear.depth) Require((v&kDepthMax)==word,"plane-0 lower24 near/far clear packing");
    Require(std::all_of(clear.coverage.begin(),clear.coverage.end(),[](uint8_t v){return v==0;}),"clear must be uncovered");
    auto matching=replay.Run(scene,upload.Get(),reduction,false);
    Require(Measure(matching,scene.depth,0).equal==4096,"matching flat reduction control");
    auto layers=Synthetic(.25f); const auto far_layer=Synthetic(.75f); layers.clips.insert(layers.clips.end(),far_layer.clips.begin(),far_layer.clips.end());
    auto second=layers.draws[0]; second.index=1;second.first=3;second.valid=false;layers.draws.push_back(second);
    auto layer_upload=gpu.Upload(layers.clips.data(),layers.clips.size()*sizeof(Clip)); auto layered=replay.Run(layers,layer_upload.Get(),reduction,false);
    const uint32_t depth=uint32_t(std::llround((reduction==Reduction::Forward?.25:.75)*kDepthMax));
    for(size_t i=0;i<layered.depth.size();++i) {
      Require(std::abs(int64_t(layered.depth[i]&kDepthMax)-depth)<=1,"near/far depth reduction winner");
      Require(layered.coverage[i]==(reduction==Reduction::Forward?1:2),"known/unknown reduction winner");
    }
    auto known=replay.Run(layers,layer_upload.Get(),reduction,true); for(auto v:known.coverage) Require(v==1,"known-only excludes unknown draw");
  }
  for(float z : {.25f,.75f}) {
    auto control=Synthetic(z); auto v=gpu.Upload(control.clips.data(),control.clips.size()*sizeof(Clip));
    auto actual=replay.Run(control,v.Get(),Reduction::Reverse,false); const auto m=Measure(actual,scene.depth,0);
    Require((z<.5f?m.negative:m.positive)==4096 && m.within16==0,"positive/negative delta controls");
  }
  auto padded=scene; padded.clips[0][3]=0; auto p=gpu.Upload(padded.clips.data(),padded.clips.size()*sizeof(Clip));
  Require(Measure(replay.Run(padded,p.Get(),Reduction::Forward,false),scene.depth,0).covered==0,"zero-W triangle must not cover pixels");
  auto shifted=flat; shifted.depth[0]=(flat.depth[0]&kDepthMax)+1; shifted.depth[1]=(flat.depth[1]&kDepthMax)-2;
  shifted.depth[2]=(flat.depth[2]&kDepthMax)+16; shifted.depth[3]=(flat.depth[3]&kDepthMax)-17;
  const auto histogram=Measure(shifted,scene.depth,0);
  Require(histogram.equal==4092 && histogram.within1==4093 && histogram.within2==4094 && histogram.within16==4095 &&
          histogram.positive==2 && histogram.negative==2,"quantum histogram inclusive bounds");
}
struct Options { fs::path clips, depth, manifest, json, coverage; uint32_t reference_far=0; bool self_only=false; };
static fs::path Utf8Path(const std::string& value) { return fs::path(std::u8string(value.begin(),value.end())); }
static Options Arguments(int argc,char** argv) {
  Options o;
  for(int i=1;i<argc;++i) {
    const std::string flag=argv[i]; if(flag=="--self-test") {o.self_only=true;continue;}
    Require(i+1<argc,"missing value for "+flag); const std::string value=argv[++i];
    if(flag=="--clips") o.clips=Utf8Path(value); else if(flag=="--depth") o.depth=Utf8Path(value);
    else if(flag=="--manifest") o.manifest=Utf8Path(value); else if(flag=="--json") o.json=Utf8Path(value);
    else if(flag=="--coverage-prefix") o.coverage=Utf8Path(value); else if(flag=="--reference-far") o.reference_far=Integer<uint32_t>(value);
    else throw std::runtime_error("unknown argument: "+flag);
  }
  const bool any=!o.clips.empty()||!o.depth.empty()||!o.manifest.empty();
  Require((!any&&o.self_only)||(!o.clips.empty()&&!o.depth.empty()&&!o.manifest.empty()),
      "usage: --clips PATH --depth PATH --manifest PATH [--json PATH] [--coverage-prefix PATH]; or --self-test");
  Require(o.reference_far==0||o.reference_far==kDepthMax,"reference far must be 0 or 16777215"); return o;
}
static void SafeOutput(const fs::path& output,const Options& options) {
  for(const auto& input:{options.clips,options.depth,options.manifest}) if(!input.empty()) {
    Require(fs::absolute(output).lexically_normal()!=fs::absolute(input).lexically_normal(),"output would overwrite input");
    if(fs::exists(output)) Require(!fs::equivalent(output,input),"output aliases input");
  }
}
static void CoverageFile(const fs::path& path,const Result& r,const Scene& scene,const Options& o) {
  SafeOutput(path,o); if(path.has_parent_path()) fs::create_directories(path.parent_path());
  std::ofstream out(path,std::ios::binary); Require(bool(out),"open coverage output");
  const uint32_t header[]{scene.depth.width,scene.depth.height,uint32_t(DXGI_FORMAT_R8_UINT),scene.depth.width};
  out.write(reinterpret_cast<const char*>(header),sizeof(header));out.write(reinterpret_cast<const char*>(r.coverage.data()),std::streamsize(r.coverage.size()));
  Require(bool(out),"write coverage output");
}
int main(int argc,char** argv) {
  try {
    const auto options=Arguments(argc,argv); std::optional<Scene> scene;
    if(!options.clips.empty()) scene=LoadScene(options.clips,options.depth,options.manifest);
    GPU gpu; Replay replay(gpu); SelfTest(gpu,replay);
    std::ostringstream json;
    json<<"{\"schema\":1,\"diagnostic_only\":true,\"temporal_history_proven\":false,\"adapter\":"<<JsonString(gpu.adapter_name)
        <<",\"debug_layer\":"<<(gpu.debug?"true":"false")<<",\"synthetic_controls\":\"passed\",\"delta_definition\":\"replay_lower24_minus_original_lower24\""
        <<",\"reference_far\":"<<options.reference_far;
    if(scene) {
      const auto& s=*scene; uint64_t valid_draws=0,zero_w_triangles=0; std::map<uint64_t,uint64_t> families;
      for(const auto& d:s.draws) {valid_draws+=d.valid;++families[d.shader];for(uint32_t i=0;i<d.vertices;i+=3)
        zero_w_triangles+=s.clips[d.first+i][3]==0||s.clips[d.first+i+1][3]==0||s.clips[d.first+i+2][3]==0;}
      Require(families.size()<=128,"family diagnostic bound exceeded");
      uint32_t minimum=kDepthMax,maximum=0;uint64_t high_byte_nonzero=0;
      for(auto v:s.depth.packed) {minimum=std::min(minimum,v&kDepthMax);maximum=std::max(maximum,v&kDepthMax);high_byte_nonzero+=(v>>24)!=0;}
      auto clips=gpu.Upload(s.clips.data(),s.clips.size()*sizeof(Clip));
      json<<",\"width\":"<<s.depth.width<<",\"height\":"<<s.depth.height<<",\"source_format\":"<<s.depth.format<<",\"source_row_pitch\":"<<s.depth.pitch
          <<",\"source_min_lower24\":"<<minimum<<",\"source_max_lower24\":"<<maximum<<",\"source_high_byte_nonzero\":"<<high_byte_nonzero
          <<",\"clip_vertices\":"<<s.clips.size()<<",\"draws\":"<<s.draws.size()<<",\"known_draws\":"<<valid_draws<<",\"zero_w_triangles\":"<<zero_w_triangles
          <<",\"manifest_extra_controls_present\":"<<(s.extra_controls?"true":"false")
          <<",\"assumptions\":\"full viewport; frozen current float4 only; recorded cull/front/bias/scissor; reductions force depth writes and omit alpha/discard/blend; EQUAL against uploaded original forces coverage validity, never temporal identity\",\"reductions\":[";
      bool first=true;
      for(auto reduction:{Reduction::Reverse,Reduction::Forward}) for(bool known:{true,false}) {
        const std::string name=std::string(reduction==Reduction::Reverse?"GEQUAL_clear0_":"LEQUAL_clear1_")+(known?"known":"all");
        const auto result=replay.Run(s,clips.Get(),reduction,known); if(!first)json<<',';first=false;
        json<<"{\"mode\":"<<JsonString(name)<<",\"draws\":"<<result.draws<<",\"vertices\":"<<result.vertices<<",\"readback_format\":"<<result.footprint_format<<",\"metrics\":";
        PrintMetrics(json,Measure(result,s.depth,options.reference_far));json<<'}';
        if(!options.coverage.empty()) CoverageFile(fs::path(options.coverage.string()+"_"+name+".raw"),result,s,options);
      }
      json<<"],\"equal_forced_coverage\":"; const auto all=replay.Run(s,clips.Get(),Reduction::Equal,false);
      PrintMetrics(json,Measure(all,s.depth,options.reference_far));
      if(!options.coverage.empty()) CoverageFile(fs::path(options.coverage.string()+"_EQUAL_all.raw"),all,s,options);
      json<<",\"equal_families\":[";first=true;
      for(const auto& [family,draws]:families) {
        const auto result=replay.Run(s,clips.Get(),Reduction::Equal,false,family);if(!first)json<<',';first=false;
        std::ostringstream name;name<<std::hex<<std::uppercase<<family;
        json<<"{\"vs\":"<<JsonString(name.str())<<",\"draws\":"<<draws<<",\"vertices\":"<<result.vertices<<",\"metrics\":";
        PrintMetrics(json,Measure(result,s.depth,options.reference_far));json<<'}';
        if(!options.coverage.empty()) CoverageFile(fs::path(options.coverage.string()+"_EQUAL_vs"+name.str()+".raw"),result,s,options);
      }
      json<<']';
    }
    json<<"}\n"; const auto output=json.str();
    if(!options.json.empty()) {SafeOutput(options.json,options);if(options.json.has_parent_path())fs::create_directories(options.json.parent_path());
      std::ofstream file(options.json,std::ios::binary);file<<output;Require(bool(file),"write JSON output");}
    std::cout<<output;return 0;
  } catch(const std::exception& e) {std::cerr<<"frozen_scene_depth_replay: "<<e.what()<<'\n';return 1;}
}
