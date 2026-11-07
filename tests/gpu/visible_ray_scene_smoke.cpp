// GPU fixtures adapted from rt_scene_smoke.cpp in this repository.
#include "visible_ray_scene.h"
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
static void CheckRT(HRESULT hr, const VisibleRayScene& scene) { Check(hr, scene.LastError()); }

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
  TextureReadback(GPU& gpu, ID3D12Resource* texture,D3D12_RESOURCE_STATES before=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
    const auto d = texture->GetDesc();
    gpu.device->GetCopyableFootprints(&d,0,1,0,&layout,nullptr,nullptr,&size);
    buffer = gpu.Buffer(size,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    if(before!=D3D12_RESOURCE_STATE_COPY_SOURCE)Transition(gpu.list.Get(),texture,before,D3D12_RESOURCE_STATE_COPY_SOURCE);
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
    const size_t row_bytes=width*sizeof(float)*(format==DXGI_FORMAT_R32_FLOAT || format==DXGI_FORMAT_R32_UINT ? 1 : format==DXGI_FORMAT_R32G32_FLOAT ? 2 : 4);
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
    GPU gpu;VisibleRayScene scene;CheckRT(scene.Initialize(gpu.device.Get()),scene);
    constexpr uint32_t width=65,height=33;
    const std::array<float,48> clips{
      -1,-1,.3f,1, 1,-1,.7f,1, 1,1,.7f,1,
      -1,-1,.3f,1, 1,1,.7f,1, -1,1,.3f,1,
      .07f,-.2f,.25f,1, .16f,-.2f,.25f,1, .16f,.2f,.25f,1,
      .07f,-.2f,.25f,1, .16f,.2f,.25f,1, .07f,.2f,.25f,1};
    auto vertices=gpu.Buffer(sizeof(clips),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    Upload(vertices.Get(),std::span<const float>(clips));
    const VisibleRayGeometry geometry[]{
      {vertices.Get(),0,6,16,true,true,false},
      {vertices.Get(),6*16,6,16,true,true,false}};
    float identity[16]{};identity[0]=identity[5]=identity[10]=identity[15]=1;
    std::vector<float> depths(width*height),colors(width*height*4),invalid_hit_depth;
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
      const float wx=(float(x)+.5f)/width*2-1,wy=1-(float(y)+.5f)/height*2;
      const bool blocker=wx>=.07f && wx<=.16f && std::abs(wy)<=.2f;
      const auto p=y*width+x;depths[p]=blocker?.25f:.5f+.2f*wx;
      colors[p*4]=blocker?.8f:.1f;colors[p*4+1]=blocker?.1f:.2f;
      colors[p*4+2]=blocker?.05f:.8f;colors[p*4+3]=1;
    }
    invalid_hit_depth=depths;
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
      const float wx=(float(x)+.5f)/width*2-1,wy=1-(float(y)+.5f)/height*2;
      if(wx>=.07f && wx<=.16f && std::abs(wy)<=.2f)invalid_hit_depth[y*width+x]=1;
    }
    InputTexture depth(gpu,width,height,DXGI_FORMAT_R32_FLOAT,depths);
    InputTexture color(gpu,width,height,DXGI_FORMAT_R32G32B32A32_FLOAT,colors);
    VisibleRayInputs inputs;inputs.scene.depth=depth.texture.Get();inputs.scene.color=color.texture.Get();
    inputs.scene.width=width;inputs.scene.height=height;
    std::memcpy(inputs.scene.inv_view_projection,identity,64);std::memcpy(inputs.view_projection,identity,64);
    inputs.scene.camera[2]=-1;inputs.scene.direction_to_light[0]=.3846154f;inputs.scene.direction_to_light[2]=-.9230769f;
    inputs.scene.ray_bias=.0001f;inputs.scene.max_distance=10;inputs.scene.max_depth_delta=.02f;
    inputs.scene.inv_view_projection_valid=inputs.scene.camera_valid=inputs.scene.light_valid=true;
    inputs.scene.depth_valid=inputs.scene.color_valid=true;inputs.scene.enable_reflections=true;
    inputs.reflection_distance=10;inputs.depth_rejection=.02f;inputs.frame_serial=1;
    CheckRT(scene.BeginFrame(0),scene);
    auto invalid_geometry=geometry[0];invalid_geometry.opaque=false;
    Require(FAILED(scene.Build(gpu.list.Get(),{&invalid_geometry,1},identity)),"unattested opaque geometry accepted");
    CheckRT(scene.Build(gpu.list.Get(),geometry,identity),scene);
    auto invalid_inputs=inputs;invalid_inputs.view_projection[0]=2;
    Require(FAILED(scene.Trace(gpu.list.Get(),invalid_inputs)),"inconsistent final projection accepted");
    CheckRT(scene.Trace(gpu.list.Get(),inputs),scene);CheckRT(scene.Composite(gpu.list.Get()),scene);auto outputs=scene.Outputs();
    Require(outputs.width==width && outputs.height==height && outputs.shadow && outputs.reflection &&
        outputs.color && !outputs.history_used && outputs.color->GetDesc().Format==color.texture->GetDesc().Format,
        "Trace -> Composite lost output metadata or matching color format");
    VisibleRayScene reactive;CheckRT(reactive.Initialize(gpu.device.Get()),reactive);
    CheckRT(reactive.BeginFrame(0),reactive);CheckRT(reactive.Build(gpu.list.Get(),geometry,identity),reactive);
    CheckRT(reactive.Trace(gpu.list.Get(),inputs),reactive);
    TextureReadback shadow(gpu,outputs.shadow),reflection(gpu,outputs.reflection);
    TextureReadback composite(gpu,outputs.color,D3D12_RESOURCE_STATE_COPY_SOURCE);
    // Restore documented output state before this frame becomes history.
    Transition(gpu.list.Get(),outputs.shadow,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(gpu.list.Get(),outputs.reflection,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.Submit();
    Require(shadow.Pixel(width/2,height/2)<.6f,"converted post-VS blocker did not shadow receiver");
    Require(reflection.Pixel(width/2,height/2,3)>.9f,"lit-scene reflection hit not accepted");
    Require(std::abs(reflection.Pixel(width/2,height/2,0)-.8f)<.01f &&
        std::abs(reflection.Pixel(width/2,height/2,2)-.05f)<.01f,"reflection did not use actual retained lit hit color");
    const float attenuation=.35f+.65f*shadow.Pixel(width/2,height/2);
    Require(std::abs(composite.Pixel(width/2,height/2,0)-(.1f*attenuation*.96f+.8f*.04f))<.001f &&
        std::abs(composite.Pixel(width/2,height/2,2)-(.8f*attenuation*.96f+.05f*.04f))<.001f &&
        composite.Pixel(width/2,height/2,3)==1,"separate RT composite pixel/alpha mismatch");

    std::vector<float> animated=colors,zero_motion(width*height*2,0),valid(width*height);
    const uint32_t valid_bits=1;float valid_float;std::memcpy(&valid_float,&valid_bits,4);
    std::fill(valid.begin(),valid.end(),valid_float);
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
      if(depths[y*width+x]==.25f)animated[(y*width+x)*4]=x==37?.9f:.4f;
    }
    InputTexture animated_color(gpu,width,height,DXGI_FORMAT_R32G32B32A32_FLOAT,animated);
    InputTexture motion(gpu,width,height,DXGI_FORMAT_R32G32_FLOAT,zero_motion);
    InputTexture validity(gpu,width,height,DXGI_FORMAT_R32_UINT,valid);
    inputs.scene.color=animated_color.texture.Get();inputs.motion=motion.texture.Get();inputs.validity=validity.texture.Get();
    std::memcpy(inputs.previous_view_projection,identity,64);inputs.previous_projection_valid=true;
    inputs.reset_history=false;inputs.frame_serial=2;
    CheckRT(scene.BeginFrame(1),scene);CheckRT(scene.Build(gpu.list.Get(),geometry,identity),scene);
    CheckRT(scene.Trace(gpu.list.Get(),inputs),scene);outputs=scene.Outputs();
    Require(outputs.history_used,"consecutive M1-valid history not enabled");
    TextureReadback temporal(gpu,outputs.reflection);
    VisibleRayScene control;CheckRT(control.Initialize(gpu.device.Get()),control);CheckRT(control.BeginFrame(0),control);
    CheckRT(control.Build(gpu.list.Get(),geometry,identity),control);
    auto reset=inputs;reset.reset_history=true;CheckRT(control.Trace(gpu.list.Get(),reset),control);
    Require(!control.Outputs().history_used,"reset history incorrectly enabled");
    TextureReadback spatial(gpu,control.Outputs().reflection);
    InputTexture reactive_mask(gpu,width,height,DXGI_FORMAT_R32_UINT,valid);
    auto rejected_history=inputs;rejected_history.reactivity=reactive_mask.texture.Get();
    CheckRT(reactive.BeginFrame(1),reactive);CheckRT(reactive.Build(gpu.list.Get(),geometry,identity),reactive);
    CheckRT(reactive.Trace(gpu.list.Get(),rejected_history),reactive);
    Require(reactive.Outputs().history_used,"reactivity fixture lacks otherwise eligible history");
    TextureReadback reactive_color(gpu,reactive.Outputs().reflection);gpu.Submit();
    Require(std::abs(reactive_color.Pixel(width/2,height/2,0)-spatial.Pixel(width/2,height/2,0))<1e-5f,
        "reactive pixel reused history despite accumulated VFX rejection");
    std::cout<<"PASS: reactive mask rejects otherwise valid temporal history\n";
    std::cout<<"denoise red: temporal="<<temporal.Pixel(width/2,height/2,0)<<" spatial="<<spatial.Pixel(width/2,height/2,0)<<'\n';
    Require(temporal.Pixel(width/2,height/2,0)>spatial.Pixel(width/2,height/2,0)+.05f,
        "temporal filter did not blend clamped previous lit hit RGB");
    InputTexture hidden_depth(gpu,width,height,DXGI_FORMAT_R32_FLOAT,invalid_hit_depth);
    inputs.scene.depth=hidden_depth.texture.Get();inputs.frame_serial=3;inputs.reset_history=true;
    CheckRT(scene.BeginFrame(2),scene);CheckRT(scene.Build(gpu.list.Get(),geometry,identity),scene);
    CheckRT(scene.Trace(gpu.list.Get(),inputs),scene);outputs=scene.Outputs();
    TextureReadback rejected(gpu,outputs.reflection);gpu.Submit();
    Require(rejected.Pixel(width/2,height/2,3)==0,"hidden/clear-depth hit leaked retained color");
    // Keep the identical blocker geometry while changing only its character
    // instance mask. Character-shadow exclusion must not exclude reflections.
    const VisibleRayGeometry character_geometry[]{geometry[0],
        {vertices.Get(),6*16,6,16,true,true,true}};
    VisibleRayScene characters_on,characters_off;
    CheckRT(characters_on.Initialize(gpu.device.Get()),characters_on);
    CheckRT(characters_off.Initialize(gpu.device.Get()),characters_off);
    auto character_inputs=inputs;
    character_inputs.scene.depth=depth.texture.Get();
    character_inputs.scene.color=color.texture.Get();
    character_inputs.scene.exclude_character_shadows=false;
    CheckRT(characters_on.BeginFrame(0),characters_on);
    CheckRT(characters_on.Build(gpu.list.Get(),character_geometry,identity),characters_on);
    CheckRT(characters_on.Trace(gpu.list.Get(),character_inputs),characters_on);
    TextureReadback character_shadow(gpu,characters_on.Outputs().shadow);
    character_inputs.scene.exclude_character_shadows=true;
    CheckRT(characters_off.BeginFrame(0),characters_off);
    CheckRT(characters_off.Build(gpu.list.Get(),character_geometry,identity),characters_off);
    CheckRT(characters_off.Trace(gpu.list.Get(),character_inputs),characters_off);
    TextureReadback excluded_shadow(gpu,characters_off.Outputs().shadow);
    TextureReadback retained_character_reflection(gpu,characters_off.Outputs().reflection);
    gpu.Submit();
    Require(character_shadow.Pixel(width/2,height/2)<.6f &&
        excluded_shadow.Pixel(width/2,height/2)>.99f,
        "character BLAS did not obey the independent shadow inclusion mask");
    Require(retained_character_reflection.Pixel(width/2,height/2,3)>.9f,
        "character shadow exclusion incorrectly removed reflected geometry");
    std::cout<<"PASS: character BLAS shadow toggle preserves character reflections\n";
    // M5 applies these settings without restarting. Consecutive frame numbers
    // and identical output sizes must not authorize a different RT estimator.
    VisibleRayScene settings;CheckRT(settings.Initialize(gpu.device.Get()),settings);
    auto live=inputs;live.scene.depth=depth.texture.Get();live.scene.color=color.texture.Get();
    live.reset_history=false;live.previous_projection_valid=true;
    for(uint32_t frame=1;frame<=10;++frame) {
      if(frame==3) {live.reflection_distance=5;live.scene.color=animated_color.texture.Get();}
      if(frame==5)live.scene.exclude_character_shadows=true;
      if(frame==7)live.trace_scale=.5f;
      if(frame==9)live.scene.direction_to_light[0]=.2f;
      live.frame_serial=frame;
      CheckRT(settings.BeginFrame((frame-1)%3),settings);
      CheckRT(settings.Build(gpu.list.Get(),geometry,identity),settings);
      CheckRT(settings.Trace(gpu.list.Get(),live),settings);
      if(frame==3) {
        // Exercise the shader's actual result as well as the eligibility metadata.
        // The lit image changes every frame in gameplay; old RGB must not bleed
        // into the first frame of a new estimator configuration.
        VisibleRayScene oracle;CheckRT(oracle.Initialize(gpu.device.Get()),oracle);
        auto spatial_only=live;spatial_only.reset_history=true;
        CheckRT(oracle.BeginFrame(0),oracle);CheckRT(oracle.Build(gpu.list.Get(),geometry,identity),oracle);
        CheckRT(oracle.Trace(gpu.list.Get(),spatial_only),oracle);
        TextureReadback actual(gpu,settings.Outputs().reflection),expected(gpu,oracle.Outputs().reflection);
        Transition(gpu.list.Get(),settings.Outputs().reflection,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gpu.Submit();
        for(uint32_t component=0;component<4;++component)
          Require(std::abs(actual.Pixel(width/2,height/2,component)-expected.Pixel(width/2,height/2,component))<1e-5f,
              "live RT setting change blended stale RGB instead of the spatial reset output");
        std::cout<<"PASS: changed estimator GPU RGB/alpha matches independent spatial reset\n";
      } else gpu.Submit();
      Require(settings.Outputs().history_used==(frame%2==0),
          "live RT setting change reused incompatible history or did not recover next frame");
    }
    std::cout<<"PASS: live RT distance, character mask, scale and sunlight reset history for one frame\n";
    std::cout<<"PASS visible scene: GPU clip conversion, real blocker shadows, projected lit hit RGB, depth rejection, spatial/temporal/reset, separate composite RGB/alpha; fence/readback\n";
    return 0;
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

