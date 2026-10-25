#include "ray_tracing_scene.h"
#include "shadow_export_fixture.h"
#include "shaders/post_vertex_world0_gs.h"
#include "post_vertex_clip_capture.h"
#include "shaders/post_vertex_clip_gs.h"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace superman_returns::graphics::d3d12;

static void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
static void Check(HRESULT hr, const char* message) {
  if (FAILED(hr)) {
    std::cerr << message << " HRESULT=0x" << std::hex << uint32_t(hr) << std::dec << '\n';
    throw std::runtime_error(message);
  }
}
static void CheckRT(HRESULT hr, const RayTracingScene& scene) { Check(hr, scene.LastError()); }

struct GPU {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence;
  ComPtr<ID3D12InfoQueue> info;
  HANDLE event = nullptr;
  uint64_t value = 0;
  GPU() {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
      debug->EnableDebugLayer();
      ComPtr<ID3D12Debug1> debug1;
      if (SUCCEEDED(debug.As(&debug1))) debug1->SetEnableGPUBasedValidation(TRUE);
      std::cout << "D3D12 debug layer + GPU validation enabled\n";
    } else {
      std::cout << "D3D12 debug layer unavailable; hardware/readback assertions remain enabled\n";
    }
    ComPtr<IDXGIFactory6> factory;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
    for (UINT i = 0;; ++i) {
      ComPtr<IDXGIAdapter1> adapter;
      if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                            IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND) break;
      DXGI_ADAPTER_DESC1 desc{};
      Check(adapter->GetDesc1(&desc), "adapter desc");
      if (desc.VendorId != 0x10de || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
      Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)), "NVIDIA device");
      std::wcout << L"Adapter: " << desc.Description << L"\n";
      break;
    }
    Require(device != nullptr, "No NVIDIA hardware adapter; test NOT passed/skipped");
    device.As(&info);
    D3D12_COMMAND_QUEUE_DESC desc{};
    Check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "queue");
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "allocator");
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&list)), "list");
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
    event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Require(event != nullptr, "fence event");
  }
  ~GPU() { if (event) CloseHandle(event); }
  void Submit() {
    Check(list->Close(), "close");
    ID3D12CommandList* lists[]{list.Get()};
    queue->ExecuteCommandLists(1, lists);
    Check(queue->Signal(fence.Get(), ++value), "signal");
    Check(fence->SetEventOnCompletion(value, event), "fence completion");
    Require(WaitForSingleObject(event, 30000) == WAIT_OBJECT_0, "GPU fence timeout");
    Check(device->GetDeviceRemovedReason(), "device removed");
    if (info) {
      for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
        SIZE_T size = 0;
        info->GetMessage(i, nullptr, &size);
        std::vector<char> storage(size);
        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        Check(info->GetMessage(i, msg, &size), "debug message");
        if (msg->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
          std::cerr << msg->pDescription << '\n';
          throw std::runtime_error("D3D12 validation error");
        }
      }
      info->ClearStoredMessages();
    }
    Check(allocator->Reset(), "allocator reset after fence");
    Check(list->Reset(allocator.Get(), nullptr), "list reset");
  }
  ComPtr<ID3D12Resource> Buffer(uint64_t size, D3D12_HEAP_TYPE heap,
                               D3D12_RESOURCE_STATES state, bool uav = false) {
    D3D12_HEAP_PROPERTIES props{}; props.Type = heap;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = size;
    d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &d, state,
                                          nullptr, IID_PPV_ARGS(&result)), "buffer");
    return result;
  }
};
static void TestCasterExtraction(GPU& gpu);
static void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* r,
                       D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
  list->ResourceBarrier(1, &b);
}
template<class T> static void Upload(ID3D12Resource* r, std::span<const T> data) {
  void* dst = nullptr; D3D12_RANGE range{0, 0};
  Check(r->Map(0, &range, &dst), "upload map");
  std::memcpy(dst, data.data(), data.size_bytes()); r->Unmap(0, nullptr);
}

struct TextureReadback {
  ComPtr<ID3D12Resource> buffer;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
  uint64_t size = 0;
  TextureReadback(GPU& gpu, ID3D12Resource* texture) {
    const auto d = texture->GetDesc();
    gpu.device->GetCopyableFootprints(&d,0,1,0,&layout,nullptr,nullptr,&size);
    buffer = gpu.Buffer(size,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(gpu.list.Get(),texture,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource=buffer.Get();
    dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint=layout;
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource=texture;
    src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
  }
  float Pixel(uint32_t x,uint32_t y,uint32_t component=0) {
    void* data=nullptr; D3D12_RANGE read{0,static_cast<SIZE_T>(size)};
    Check(buffer->Map(0,&read,&data),"texture readback map");
    auto* row=reinterpret_cast<float*>(static_cast<char*>(data)+layout.Offset+y*layout.Footprint.RowPitch);
    uint32_t channels=layout.Footprint.Format==DXGI_FORMAT_R32_FLOAT ? 1 : 4;
    float value=row[x*channels+component];
    D3D12_RANGE no_write{0,0}; buffer->Unmap(0,&no_write);
    return value;
  }
};
struct InputTexture {
  ComPtr<ID3D12Resource> texture,upload;
  InputTexture(GPU& gpu,uint32_t width,uint32_t height,DXGI_FORMAT format,
               const std::vector<float>& values) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=width; d.Height=height;
    d.DepthOrArraySize=1; d.MipLevels=1; d.Format=format; d.SampleDesc.Count=1;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    Check(gpu.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)),"input texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{}; uint64_t size=0;
    gpu.device->GetCopyableFootprints(&d,0,1,0,&layout,nullptr,nullptr,&size);
    upload=gpu.Buffer(size,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* dst=nullptr; D3D12_RANGE no_read{0,0}; Check(upload->Map(0,&no_read,&dst),"texture upload");
    const size_t row_bytes=width*sizeof(float)*(format==DXGI_FORMAT_R32_FLOAT ? 1 : 4);
    for (uint32_t y=0;y<height;++y)
      std::memcpy(static_cast<char*>(dst)+layout.Offset+y*layout.Footprint.RowPitch,
                  reinterpret_cast<const char*>(values.data())+y*row_bytes,row_bytes);
    upload->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource=upload.Get();
    src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint=layout;
    D3D12_TEXTURE_COPY_LOCATION target{}; target.pResource=texture.Get();
    target.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu.list->CopyTextureRegion(&target,0,0,0,&src,nullptr);
    Transition(gpu.list.Get(),texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
};

int main() {
  try {
    GPU gpu;
    RayTracingScene scene;
    CheckRT(scene.Initialize(gpu.device.Get()), scene);
    Require(FAILED(scene.BeginFrame(3)), "invalid slot accepted");
    CheckRT(scene.BeginFrame(0), scene);
    Require(FAILED(scene.BuildScene(gpu.list.Get())), "empty scene accepted");
    // Receiver plane at z=0 and occluder at z=2. Prefix tests byte offset.
    const std::array<float, 30> vertices{
      999,999,999, -20,-20,0, 20,-20,0, 0,20,0,
      -1,-1,2, 1,-1,2, 0,1,2,
      2,-1,2, 4,-1,2, 3,1,2};
    auto vb = gpu.Buffer(sizeof(vertices), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    Upload(vb.Get(), std::span<const float>(vertices));
    RayTracingGeometry receiver{vb.Get(), 12, 3, false, {{0.2f,0.4f,0.6f}, true}};
    RayTracingGeometry blocker{vb.Get(), 48, 3, false, {{0.8f,0.1f,0.3f}, true}};
    RayTracingGeometry character{vb.Get(), 84, 3, true, {}};
    auto malformed = receiver; malformed.vertex_count = 4;
    Require(FAILED(scene.AddTriangles({&malformed,1})), "non-triangle count accepted");
    Require(FAILED(scene.BuildScene(nullptr,{&receiver,1})),"null build list accepted");
    const RayTracingGeometry geometry[]{receiver, blocker, character};
    CheckRT(scene.BuildScene(gpu.list.Get(), geometry), scene);
    Require(FAILED(scene.BuildScene(gpu.list.Get())), "second build accepted");
    const RayTracingRay rays[]{
      {{0,0,0.01f},0.001f,{0,0,1},10,3,{}}, // blocker hit t=1.99
      {{-3,0,0.01f},0.001f,{0,0,1},10,3,{}}, // miss
      {{3,0,0.01f},0.001f,{0,0,1},10,3,{}}, // character hit, invalid material
      {{3,0,0.01f},0.001f,{0,0,1},10,1,{}}, // exclude character -> miss
      {{0,0,1},0.001f,{0,0,-1},10,3,{}}}; // receiver hit
    auto result = gpu.Buffer(sizeof(RayTracingHit)*5, D3D12_HEAP_TYPE_DEFAULT,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
    auto readback = gpu.Buffer(sizeof(RayTracingHit)*5, D3D12_HEAP_TYPE_READBACK,
                               D3D12_RESOURCE_STATE_COPY_DEST);
    CheckRT(scene.TraceRays(gpu.list.Get(), rays, result.Get()), scene);
    Transition(gpu.list.Get(), result.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.list->CopyResource(readback.Get(), result.Get());
    gpu.Submit();
    RayTracingHit* hits = nullptr; D3D12_RANGE range{0, sizeof(RayTracingHit)*5};
    Check(readback->Map(0, &range, reinterpret_cast<void**>(&hits)), "readback map");
    Require(hits[0].hit == 1 && hits[0].instance_id == 1 && std::abs(hits[0].distance-1.99f)<0.001f,
            "blocker hit/distance mismatch");
    Require(hits[0].material_valid == 1 && std::abs(hits[0].albedo[0]-0.8f)<0.001f, "valid hit material lost");
    Require(hits[1].hit == 0 && hits[3].hit == 0, "miss/character exclusion mismatch");
    Require(hits[2].hit == 1 && hits[2].material_valid == 0, "character/invalid material mismatch");
    Require(hits[4].hit == 1 && hits[4].instance_id == 0, "receiver hit mismatch");
    D3D12_RANGE no_write{0,0}; readback->Unmap(0, &no_write);
    std::cout << "PASS: real GPU BLAS/TLAS hit/miss, distance, material, character mask, offset; fence readback\n";

    // Explicit synthetic receiver: world z=0, standard depth=.5, no game defaults.
    constexpr uint32_t w=9,h=5;
    std::vector<float> depths(w*h,0.5f),colors(w*h*4,0.25f);
    depths[0]=1;  // Clear/background must be marked invalid.
    InputTexture depth(gpu,w,h,DXGI_FORMAT_R32_FLOAT,depths);
    InputTexture color(gpu,w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,colors);
    RayTracingTraceInputs input{};
    input.depth=depth.texture.Get(); input.color=color.texture.Get(); input.width=w; input.height=h;
    input.inv_view_projection[0]=4.5f; input.inv_view_projection[5]=1;
    input.inv_view_projection[10]=1; input.inv_view_projection[11]=-0.5f; input.inv_view_projection[15]=1;
    input.camera[2]=100; input.direction_to_light[2]=1;
    input.ray_bias=0.001f; input.max_distance=10; input.max_depth_delta=0.01f;
    input.inv_view_projection_valid=input.camera_valid=input.light_valid=true;
    input.depth_valid=input.color_valid=true; input.enable_reflections=true;
    auto invalid=input; invalid.camera_valid=false;
    Require(FAILED(scene.Trace(gpu.list.Get(),invalid)),"unvalidated camera accepted");
    invalid=input; invalid.inv_view_projection[15]=0;
    Require(FAILED(scene.Trace(gpu.list.Get(),invalid)),"singular matrix accepted");
    // A D32_FLOAT view needs an R32_TYPELESS resource to expose R32_FLOAT SRV.
    // Typed depth resources are outside this module's supported input contract.
    auto typed_desc=depth.texture->GetDesc(); typed_desc.Format=DXGI_FORMAT_D32_FLOAT;
    typed_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_HEAP_PROPERTIES typed_heap{}; typed_heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> typed_depth;
    Check(gpu.device->CreateCommittedResource(&typed_heap,D3D12_HEAP_FLAG_NONE,&typed_desc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&typed_depth)),"typed depth fixture");
    invalid=input; invalid.depth=typed_depth.Get();
    Require(FAILED(scene.Trace(gpu.list.Get(),invalid)),"unsupported typed depth SRV cast accepted");
    CheckRT(scene.Trace(gpu.list.Get(),input),scene);
    auto outputs=scene.Outputs();
    RayTracingDepthInputs depth_probe{};
    depth_probe.depth=depth.texture.Get(); depth_probe.width=w; depth_probe.height=h;
    depth_probe.grid_width=w; depth_probe.grid_height=h;
    std::memcpy(depth_probe.inv_view_projection,input.inv_view_projection,sizeof(input.inv_view_projection));
    depth_probe.camera[2]=100;
    auto depth_results=gpu.Buffer(w*h*sizeof(RayTracingDepthHit),D3D12_HEAP_TYPE_DEFAULT,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
    auto depth_readback=gpu.Buffer(w*h*sizeof(RayTracingDepthHit),D3D12_HEAP_TYPE_READBACK,
                                   D3D12_RESOURCE_STATE_COPY_DEST);
    CheckRT(scene.ProbeDepth(gpu.list.Get(),depth_probe,depth_results.Get()),scene);
    Transition(gpu.list.Get(),depth_results.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.list->CopyResource(depth_readback.Get(),depth_results.Get());
    auto invalid_probe=depth_probe; invalid_probe.grid_width=0;
    Require(FAILED(scene.ProbeDepth(gpu.list.Get(),invalid_probe,depth_results.Get())),"empty depth grid accepted");
    invalid_probe=depth_probe; invalid_probe.inv_view_projection[15]=0;
    Require(FAILED(scene.ProbeDepth(gpu.list.Get(),invalid_probe,depth_results.Get())),"singular depth projection accepted");
    // Match the game's actual typeless D24S8 producer, including its depth plane SRV.
    auto d24_desc=depth.texture->GetDesc(); d24_desc.Format=DXGI_FORMAT_R24G8_TYPELESS;
    d24_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    ComPtr<ID3D12Resource> d24;
    Check(gpu.device->CreateCommittedResource(&typed_heap,D3D12_HEAP_FLAG_NONE,&d24_desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&d24)),"D24 receiver fixture");
    D3D12_DESCRIPTOR_HEAP_DESC dsv_desc{}; dsv_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV; dsv_desc.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> dsv_heap;
    Check(gpu.device->CreateDescriptorHeap(&dsv_desc,IID_PPV_ARGS(&dsv_heap)),"D24 receiver DSV heap");
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
    gpu.device->CreateDepthStencilView(d24.Get(),&dsv,dsv_heap->GetCPUDescriptorHandleForHeapStart());
    gpu.list->ClearDepthStencilView(dsv_heap->GetCPUDescriptorHandleForHeapStart(),D3D12_CLEAR_FLAG_DEPTH,0.5f,0,0,nullptr);
    Transition(gpu.list.Get(),d24.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto d24_results=gpu.Buffer(w*h*sizeof(RayTracingDepthHit),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
    auto d24_readback=gpu.Buffer(w*h*sizeof(RayTracingDepthHit),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    depth_probe.depth=d24.Get();
    CheckRT(scene.ProbeDepth(gpu.list.Get(),depth_probe,d24_results.Get()),scene);
    Transition(gpu.list.Get(),d24_results.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.list->CopyResource(d24_readback.Get(),d24_results.Get());
    auto d24_trace=input; d24_trace.depth=d24.Get();
    CheckRT(scene.Trace(gpu.list.Get(),d24_trace),scene);
    TextureReadback d24_shadow(gpu,scene.Outputs().shadow);
    Require(outputs.reflections_available && outputs.width==w && outputs.height==h,"trace output contract");
    TextureReadback shadow_low(gpu,outputs.shadow),reflection_low(gpu,outputs.reflection);
    // Second dispatch in same frame: descriptors/constants must remain immutable.
    input.exclude_character_shadows=true; input.exclude_character_reflections=true;
    input.shadow_quality=RayTracingShadowQuality::High; input.cone_half_angle=0.03f;
    CheckRT(scene.Trace(gpu.list.Get(),input),scene);
    outputs=scene.Outputs();
    TextureReadback shadow_high(gpu,outputs.shadow),reflection_high(gpu,outputs.reflection);
    auto penumbra_input=input; penumbra_input.inv_view_projection[3]=0.5f;
    CheckRT(scene.Trace(gpu.list.Get(),penumbra_input),scene);
    TextureReadback penumbra(gpu,scene.Outputs().shadow);
    auto reverse_depths=depths; reverse_depths[0]=0;
    InputTexture reverse_depth(gpu,w,h,DXGI_FORMAT_R32_FLOAT,reverse_depths);
    auto reverse_input=input; reverse_input.depth=reverse_depth.texture.Get(); reverse_input.clear_depth=0;
    reverse_input.inv_view_projection[10]=-1; reverse_input.inv_view_projection[11]=0.5f;
    CheckRT(scene.Trace(gpu.list.Get(),reverse_input),scene);
    TextureReadback reverse_shadow(gpu,scene.Outputs().shadow);
    gpu.Submit();
    RayTracingDepthHit* receiver_hits=nullptr;
    D3D12_RANGE depth_range{0,w*h*sizeof(RayTracingDepthHit)};
    Check(depth_readback->Map(0,&depth_range,reinterpret_cast<void**>(&receiver_hits)),"depth receiver readback");
    Require(receiver_hits[0].valid==0,"clear depth accepted as a receiver");
    const auto& matching_receiver=receiver_hits[2*w]; // x=-4, clear path to receiver plane.
    Require(matching_receiver.valid && matching_receiver.hit && matching_receiver.relative_error<0.0001f,
            "depth projection disagrees with actual BLAS receiver");
    const auto& occluded_receiver=receiver_hits[2*w+4];
    Require(occluded_receiver.valid && occluded_receiver.hit && occluded_receiver.relative_error>0.01f,
            "a nearer blocker must expose an inconsistent depth receiver");
    depth_readback->Unmap(0,&no_write);
    Check(d24_readback->Map(0,&depth_range,reinterpret_cast<void**>(&receiver_hits)),"D24 receiver readback");
    Require(receiver_hits[2*w].valid && receiver_hits[2*w].hit && receiver_hits[2*w].relative_error<0.0001f,
            "D24 depth plane disagrees with R32 receiver");
    d24_readback->Unmap(0,&no_write);
    Require(d24_shadow.Pixel(4,2)==0 && d24_shadow.Pixel(0,2)==1,"D24 shadow visibility differs from R32");
    std::cout << "PASS: actual GPU R32/D24 depth/BLAS agreement, clear rejection, invalid projection/grid, nearer-blocker disagreement\n";
    Require(shadow_low.Pixel(4,2)==0 && shadow_low.Pixel(0,2)==1,"low shadow hit/miss mismatch");
    Require(shadow_low.Pixel(7,2)==0 && shadow_high.Pixel(7,2)==1,"shadow character exclusion mismatch");
    Require(shadow_high.Pixel(4,2)==0 && shadow_high.Pixel(0,2)==1,"high cone shadow hit/miss mismatch");
    Require(penumbra.Pixel(3,2)>0 && penumbra.Pixel(3,2)<1,"cone sampling failed to produce known edge penumbra");
    Require(shadow_low.Pixel(0,0)==-1,"clear depth fabricated a receiver");
    Require(reverse_shadow.Pixel(0,0)==-1 && reverse_shadow.Pixel(4,2)==0,"reverse-Z reconstruction/clear depth mismatch");
    Require(reflection_low.Pixel(4,2,3)==1 && std::abs(reflection_low.Pixel(4,2,0)-0.8f)<0.001f,
            "reflection failed to shade valid actual RT hit");
    Require(reflection_low.Pixel(7,2,3)==0 && reflection_high.Pixel(7,2,3)==0,"reflection fabricated missing material");
    Require(reflection_low.Pixel(0,2,3)==0,"reflection fabricated miss environment");
    std::cout << "PASS: depth/color, standard/reverse-Z, low/high cone shadows + penumbra, actual RT hit reflections, invalid data\n";

    // Same resource pointer, changed contents, fresh slot: must not reuse stale BLAS.
    auto moved=vertices;
    for (size_t i=12;i<moved.size();i+=3) moved[i]+=100;
    Upload(vb.Get(),std::span<const float>(moved));
    CheckRT(scene.BeginFrame(1),scene);
    receiver.material.valid=blocker.material.valid=false;
    const RayTracingGeometry moved_geometry[]{receiver,blocker,character};
    CheckRT(scene.BuildScene(gpu.list.Get(),moved_geometry),scene);
    Transition(gpu.list.Get(),result.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    CheckRT(scene.TraceRays(gpu.list.Get(),rays,result.Get()),scene);
    Transition(gpu.list.Get(),result.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.list->CopyResource(readback.Get(),result.Get());
    CheckRT(scene.Trace(gpu.list.Get(),input),scene);
    outputs=scene.Outputs();
    Require(!outputs.reflections_available,"missing materials not reported as unavailable");
    TextureReadback no_materials(gpu,outputs.reflection);
    gpu.Submit();
    Check(readback->Map(0,&range,reinterpret_cast<void**>(&hits)),"changed geometry readback");
    Require(hits[0].hit==0 && hits[2].hit==0 && hits[4].hit==1,"stale BLAS reused for same resource pointer");
    readback->Unmap(0,&no_write);
    Require(no_materials.Pixel(4,2,3)==0,"unavailable reflection output must be invalid");
    // A valid character material must be shaded only if reflection mask includes it.
    Upload(vb.Get(),std::span<const float>(vertices));
    CheckRT(scene.BeginFrame(2),scene);
    character.material={{0.1f,0.9f,0.2f},true};
    const RayTracingGeometry valid_character[]{receiver,blocker,character};
    CheckRT(scene.BuildScene(gpu.list.Get(),valid_character),scene);
    input.exclude_character_reflections=false;
    CheckRT(scene.Trace(gpu.list.Get(),input),scene);
    TextureReadback character_reflection(gpu,scene.Outputs().reflection);
    input.exclude_character_reflections=true;
    CheckRT(scene.Trace(gpu.list.Get(),input),scene);
    TextureReadback excluded_reflection(gpu,scene.Outputs().reflection);
    gpu.Submit();
    Require(character_reflection.Pixel(7,2,3)==1 && std::abs(character_reflection.Pixel(7,2,1)-0.9f)<0.001f,
            "valid character reflection missing");
    Require(excluded_reflection.Pixel(7,2,3)==0,"character reflection exclusion ignored");
    CheckRT(scene.BeginFrame(0),scene);  // slot0's fence completed above.
    Require(scene.Outputs().shadow==nullptr,"BeginFrame kept stale output");
    Require(FAILED(scene.TraceRays(gpu.list.Get(),rays,result.Get())),"trace without new scene accepted");
    std::cout << "PASS: same-pointer vertex rebuild, unavailable materials, character reflection mask, fenced slot reset\n";
    TestCasterExtraction(gpu);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n'; return 1;
  }
}

static void TestCasterExtraction(GPU& gpu) {
  D3D12_ROOT_PARAMETER parameters[2]{};
  for(uint32_t i=0;i<2;++i) {
    parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[i].Descriptor={i*2,4};
  }
  D3D12_ROOT_SIGNATURE_DESC root_desc{2,parameters,0,nullptr,
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT};
  ComPtr<ID3DBlob> serialized,errors;
  Check(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors),"caster test root serialize");
  ComPtr<ID3D12RootSignature> root;
  Check(gpu.device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root)),"caster test root");
  const D3D12_INPUT_ELEMENT_DESC input{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
  const D3D12_SO_DECLARATION_ENTRY stream{0,"TEXCOORD",15,0,3,0}; const uint32_t stride=12;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature=root.Get(); pso_desc.VS={kShadowExportFixture,sizeof(kShadowExportFixture)};
  pso_desc.GS={kPostVertexWorld0GS,sizeof(kPostVertexWorld0GS)};
  pso_desc.InputLayout={&input,1}; pso_desc.StreamOutput={&stream,1,&stride,1,D3D12_SO_NO_RASTERIZED_STREAM};
  pso_desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pso_desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
  pso_desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.SampleMask=UINT_MAX; pso_desc.SampleDesc.Count=1;
  pso_desc.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
  pso_desc.DepthStencilState.FrontFace=pso_desc.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
  ComPtr<ID3D12PipelineState> pso;
  Check(gpu.device->CreateGraphicsPipelineState(&pso_desc,IID_PPV_ARGS(&pso)),"caster test PSO");
  const std::array<float,9> vertices{5,-7,11, 6,-7,11, -80,70,50};
  auto vb=gpu.Buffer(sizeof(vertices),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
  Upload(vb.Get(),std::span<const float>(vertices));
  auto constants=gpu.Buffer(4096,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
  auto shared=gpu.Buffer(512,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
  auto zero=gpu.Buffer(8,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
  const std::array<uint64_t,1> zeros{}; Upload(zero.Get(),std::span<const uint64_t>(zeros));
  auto output=gpu.Buffer(sizeof(vertices),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_STREAM_OUT);
  auto counter=gpu.Buffer(8,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
  auto readback=gpu.Buffer(sizeof(vertices)+8,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
  for(uint32_t mode=0;mode<5;++mode) {
    std::array<float,1024> c{};
    c[0]=c[5]=c[10]=c[15]=1;
    const float rows[3][4]{{.02f,0,.01f,.2f},{0,.03f,0,.1f},{.001f,-.002f,.004f,.3f}};
    std::memcpy(c.data()+16,rows,sizeof(rows));
    const float atlas[3][4]{{.1f,.2f,.3f,.5f},{.4f,.3f,.2f,.25f},{.05f,.1f,.15f,2}};
    std::memcpy(c.data()+28,atlas,sizeof(atlas)); c[40]=float(mode==4 ? 1 : mode); c[42]=.0125f;
    if(mode==4) c[31]=0; // Invalid atlas must emit degenerate zeros, never NaN positions.
    Upload(constants.Get(),std::span<const float>(c));
    std::array<float,128> s{}; s[73]=1.f/640;s[74]=-1.f/640;
    s[108]=.8f;s[109]=-.9f;s[110]=.1f;s[111]=-.2f;
    Upload(shared.Get(),std::span<const float>(s));
    gpu.list->CopyResource(counter.Get(),zero.Get());
    Transition(gpu.list.Get(),counter.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_STREAM_OUT);
    gpu.list->SetGraphicsRootSignature(root.Get());gpu.list->SetPipelineState(pso.Get());
    gpu.list->SetGraphicsRootConstantBufferView(0,constants->GetGPUVirtualAddress());
    gpu.list->SetGraphicsRootConstantBufferView(1,shared->GetGPUVirtualAddress());
    const D3D12_VERTEX_BUFFER_VIEW view{vb->GetGPUVirtualAddress(),sizeof(vertices),12};
    gpu.list->IASetVertexBuffers(0,1,&view);gpu.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_STREAM_OUTPUT_BUFFER_VIEW so{output->GetGPUVirtualAddress(),sizeof(vertices),counter->GetGPUVirtualAddress()};
    gpu.list->SOSetTargets(0,1,&so);gpu.list->DrawInstanced(3,1,0,0);gpu.list->SOSetTargets(0,0,nullptr);
    Transition(gpu.list.Get(),output.Get(),D3D12_RESOURCE_STATE_STREAM_OUT,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(gpu.list.Get(),counter.Get(),D3D12_RESOURCE_STATE_STREAM_OUT,D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.list->CopyBufferRegion(readback.Get(),0,output.Get(),0,sizeof(vertices));
    gpu.list->CopyBufferRegion(readback.Get(),sizeof(vertices),counter.Get(),0,8);
    Transition(gpu.list.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_STREAM_OUT);
    Transition(gpu.list.Get(),counter.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    gpu.Submit();
    const D3D12_RANGE range{0,sizeof(vertices)+8}; void* data=nullptr;
    Check(readback->Map(0,&range,&data),"caster test readback");
    uint64_t written=0; std::memcpy(&written,static_cast<const char*>(data)+sizeof(vertices),8);
    Require(written==sizeof(vertices),"caster GS did not emit exactly one triangle");
    const auto* xyz=static_cast<const float*>(data);
    for(size_t i=0;i<vertices.size();++i)
      Require(std::isfinite(xyz[i]) && std::abs(xyz[i]-(mode==4 ? 0 : vertices[i]))<.001f,
              "caster GS did not recover known world vertices");
    const D3D12_RANGE no_write{0,0};readback->Unmap(0,&no_write);
  }
  std::cout << "PASS: production caster GS GPU extraction, all atlas levels, pixel fix, outside-frustum vertex and invalid atlas\n";
  PostVertexClipCapture clips;
  Require(clips.Initialize(gpu.device.Get()),"clip capture allocation");
  Require(clips.Begin(gpu.list.Get(),0),"clip capture begin");
  Require(!clips.Reserve(0).has_value(),"empty clip range accepted");
  const auto clip_range=clips.Reserve(3);Require(clip_range.has_value(),"clip range reservation");
  const D3D12_SO_DECLARATION_ENTRY clip_stream{0,"TEXCOORD",15,0,4,0}; const uint32_t clip_stride=16;
  pso_desc.GS={kPostVertexClipGS,sizeof(kPostVertexClipGS)};
  pso_desc.StreamOutput={&clip_stream,1,&clip_stride,1,D3D12_SO_NO_RASTERIZED_STREAM};
  ComPtr<ID3D12PipelineState> clip_pso;
  Check(gpu.device->CreateGraphicsPipelineState(&pso_desc,IID_PPV_ARGS(&clip_pso)),"clip GS PSO");
  std::array<float,1024> c{};c[0]=c[5]=c[10]=c[15]=1;
  const float rows[3][4]{{.02f,0,.01f,.2f},{0,.03f,0,.1f},{.001f,-.002f,.004f,.3f}};
  std::memcpy(c.data()+16,rows,sizeof(rows));
  Upload(constants.Get(),std::span<const float>(c));
  gpu.list->SetGraphicsRootSignature(root.Get());gpu.list->SetPipelineState(clip_pso.Get());
  gpu.list->SetGraphicsRootConstantBufferView(0,constants->GetGPUVirtualAddress());
  gpu.list->SetGraphicsRootConstantBufferView(1,shared->GetGPUVirtualAddress());
  const D3D12_VERTEX_BUFFER_VIEW clip_vb{vb->GetGPUVirtualAddress(),sizeof(vertices),12};
  gpu.list->IASetVertexBuffers(0,1,&clip_vb);gpu.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  gpu.list->SOSetTargets(0,1,&clip_range->view);gpu.list->DrawInstanced(3,1,0,0);
  clips.Finish(gpu.list.Get());
  auto clip_readback=gpu.Buffer(48,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
  Transition(gpu.list.Get(),clips.Buffer(0),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
  gpu.list->CopyBufferRegion(clip_readback.Get(),0,clips.Buffer(0),clip_range->offset,48);
  Transition(gpu.list.Get(),clips.Buffer(0),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  gpu.Submit();
  void* clip_data=nullptr;const D3D12_RANGE clip_bytes{0,48};
  Check(clip_readback->Map(0,&clip_bytes,&clip_data),"exact clip readback");
  const auto* positions=static_cast<const float*>(clip_data);
  for(size_t i=0;i<3;++i) {
    const auto* world=vertices.data()+3*i;
    const float u=.02f*world[0]+.01f*world[2]+.2f,v=.03f*world[1]+.1f;
    const float expected[4]{(2*u-1)*.8f+.1f+1.f/640,(1-2*v)*-.9f-.2f-1.f/640,.5f,1};
    for(size_t axis=0;axis<4;++axis) Require(std::abs(positions[i*4+axis]-expected[axis])<1e-5f,
        "captured clip differs from exact original VS output");
  }
  const D3D12_RANGE no_write{0,0};clip_readback->Unmap(0,&no_write);
  std::cout << "PASS: exact original post-VS clip capture, raster fix and outside-frustum vertices preserved\n";
}
