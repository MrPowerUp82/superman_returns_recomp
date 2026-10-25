#include "ray_tracing_scene.h"
#include "shaders/rt_scene_TraceRays.h"
#include "shaders/rt_scene_TraceScene.h"
#include "shaders/depth_receivers.h"
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// Original implementation; no third-party source copied.
// Official references consulted 2026-10-24:
// https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html
// https://devblogs.microsoft.com/directx/dxr-1-1/
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_raytracing_geometry_triangles_desc
// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist4-buildraytracingaccelerationstructure
// Sample reference (not incorporated), MIT license:
// https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12Raytracing
// https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/LICENSE

namespace superman_returns::graphics::d3d12 {
using Microsoft::WRL::ComPtr;
namespace {
struct Failure { HRESULT hr; const char* message; };
void Check(HRESULT hr, const char* message) { if (FAILED(hr)) throw Failure{hr,message}; }
void Require(bool condition, const char* message, HRESULT hr = E_INVALIDARG) {
  if (!condition) throw Failure{hr,message};
}
bool Finite3(const float* v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
float Length2(const float* v) { return v[0]*v[0]+v[1]*v[1]+v[2]*v[2]; }
void UAVBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource) {
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = resource;
  list->ResourceBarrier(1,&b);
}
struct Constants {
  float inv_vp[16]{};
  float camera[3]{}; uint32_t flags = 0;
  float light[3]{}; uint32_t samples = 0;
  uint32_t width = 0, height = 0, ray_count = 0, geometry_count = 0;
  float bias = 0, max_distance = 0, cone_angle = 0, clear_depth = 0;
  float max_depth_delta = 0, padding[3]{};
};
static_assert(sizeof(Constants) == 144);
bool Invertible(const float* matrix) {
  double m[4][4];
  for (int r=0;r<4;++r) for (int c=0;c<4;++c) m[r][c]=matrix[r*4+c];
  for (int c=0;c<4;++c) {
    int pivot=c;
    for (int r=c+1;r<4;++r) if (std::abs(m[r][c])>std::abs(m[pivot][c])) pivot=r;
    if (m[pivot][c] == 0) return false;
    for (int k=0;k<4;++k) std::swap(m[c][k],m[pivot][k]);
    for (int r=c+1;r<4;++r) {
      double scale=m[r][c]/m[c][c];
      for (int k=c;k<4;++k) m[r][k]-=scale*m[c][k];
    }
  }
  return true;
}
}  // namespace

struct RayTracingScene::Impl {
  struct Frame {
    std::vector<RayTracingGeometry> geometry;
    std::vector<ComPtr<ID3D12Resource>> resources;
    std::vector<ComPtr<ID3D12DescriptorHeap>> heaps;
    ComPtr<ID3D12Resource> tlas, materials;
    bool built = false, has_materials = false;
    RayTracingOutputs outputs{};
  };
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12Device5> device5;
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12PipelineState> query_pipeline, scene_pipeline,depth_pipeline;
  std::array<Frame,kFrameSlots> frames;
  int active = -1;
  std::string error;
  template<class F> HRESULT Run(F&& function) {
    try { function(); error.clear(); return S_OK; }
    catch (const Failure& f) { error=f.message; return f.hr; }
    catch (const std::bad_alloc&) { error="DXR allocation failed"; return E_OUTOFMEMORY; }
  }
  Frame& Current(bool built = false) {
    Require(device5 != nullptr,"Initialize must succeed first",E_UNEXPECTED);
    Require(active>=0,"BeginFrame must be called first",E_UNEXPECTED);
    auto& f=frames[active];
    if (built) Require(f.built,"BuildScene must succeed before tracing",E_UNEXPECTED);
    return f;
  }
  void SameDevice(ID3D12DeviceChild* child) {
    ComPtr<ID3D12Device> owner;
    Check(child->GetDevice(IID_PPV_ARGS(&owner)),"Cannot query resource/list device");
    Require(owner.Get()==device.Get(),"Resource/list belongs to another D3D12 device");
  }
  ComPtr<ID3D12GraphicsCommandList4> List4(ID3D12GraphicsCommandList* list) {
    Require(list!=nullptr,"Null command list");
    Require(list->GetType()==D3D12_COMMAND_LIST_TYPE_DIRECT || list->GetType()==D3D12_COMMAND_LIST_TYPE_COMPUTE,
            "DXR needs a DIRECT or COMPUTE command list");
    SameDevice(list);
    ComPtr<ID3D12GraphicsCommandList4> result;
    Check(list->QueryInterface(IID_PPV_ARGS(&result)),"ID3D12GraphicsCommandList4 unavailable");
    return result;
  }
  ComPtr<ID3D12Resource> Buffer(uint64_t size, D3D12_HEAP_TYPE heap,
                               D3D12_RESOURCE_STATES state, bool uav = false) {
    Require(size>0,"Zero-size DXR allocation",E_FAIL);
    D3D12_HEAP_PROPERTIES props{}; props.Type=heap;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width=size;
    desc.Height=1; desc.DepthOrArraySize=1; desc.MipLevels=1;
    desc.SampleDesc.Count=1; desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags=uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,
                                          IID_PPV_ARGS(&result)),"DXR buffer allocation failed");
    return result;
  }
  ComPtr<ID3D12Resource> Upload(const void* data, size_t size) {
    auto result=Buffer(size,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* destination=nullptr; D3D12_RANGE no_read{0,0};
    Check(result->Map(0,&no_read,&destination),"DXR upload map failed");
    std::memcpy(destination,data,size);
    D3D12_RANGE written{0,size}; result->Unmap(0,&written);
    return result;
  }
  ComPtr<ID3D12Resource> Texture(uint32_t width,uint32_t height,DXGI_FORMAT format) {
    D3D12_HEAP_PROPERTIES props{}; props.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width=width; desc.Height=height;
    desc.DepthOrArraySize=1; desc.MipLevels=1; desc.SampleDesc.Count=1; desc.Format=format;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&result)),"DXR output texture allocation failed");
    return result;
  }
  void Bind(ID3D12GraphicsCommandList* list,Frame& f,const Constants& c,ID3D12PipelineState* pso) {
    list->SetComputeRootSignature(root.Get());
    list->SetPipelineState(pso);
    list->SetComputeRoot32BitConstants(0,sizeof(c)/4,&c,0);
    list->SetComputeRootShaderResourceView(1,f.tlas->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(2,f.materials->GetGPUVirtualAddress());
  }
};

RayTracingScene::RayTracingScene() : impl_(std::make_unique<Impl>()) {}
RayTracingScene::~RayTracingScene() = default;
const char* RayTracingScene::LastError() const { return impl_->error.c_str(); }

HRESULT RayTracingScene::Initialize(ID3D12Device* device) {
  return impl_->Run([&] {
    Require(device!=nullptr,"Initialize: null device");
    Require(impl_->device5==nullptr,"Initialize may only succeed once",E_UNEXPECTED);
    ComPtr<ID3D12Device5> device5;
    Check(device->QueryInterface(IID_PPV_ARGS(&device5)),"ID3D12Device5 unavailable");
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    Check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,&options,sizeof(options)),"DXR tier query failed");
    Require(options.RaytracingTier>=D3D12_RAYTRACING_TIER_1_1,"DXR tier 1.1 required",DXGI_ERROR_UNSUPPORTED);
    D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_5};
    Check(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&sm,sizeof(sm)),"Shader Model 6.5 query failed");
    Require(sm.HighestShaderModel>=D3D_SHADER_MODEL_6_5,"Shader Model 6.5 required",DXGI_ERROR_UNSUPPORTED);
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,3,0,0};
    ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,2};
    D3D12_ROOT_PARAMETER parameters[6]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants={0,0,sizeof(Constants)/4};
    for (uint32_t i=1;i<=3;++i) {
      parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
      parameters[i].Descriptor={i-1,0};
    }
    parameters[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[4].Descriptor={2,0};
    parameters[5].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[5].DescriptorTable={2,ranges};
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters=6; desc.pParameters=parameters;
    ComPtr<ID3DBlob> blob, errors;
    Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors),"DXR root signature serialization failed");
    ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),
          "DXR root signature creation failed");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature=root.Get(); pso.CS={rt_scene_TraceRays,sizeof(rt_scene_TraceRays)};
    ComPtr<ID3D12PipelineState> queries,scene,receivers;
    Check(device->CreateComputePipelineState(&pso,IID_PPV_ARGS(&queries)),"DXR TraceRays cs_6_5 pipeline creation failed");
    pso.CS={rt_scene_TraceScene,sizeof(rt_scene_TraceScene)};
    Check(device->CreateComputePipelineState(&pso,IID_PPV_ARGS(&scene)),"DXR TraceScene cs_6_5 pipeline creation failed");
    pso.CS={kDepthReceivers,sizeof(kDepthReceivers)};
    Check(device->CreateComputePipelineState(&pso,IID_PPV_ARGS(&receivers)),"DXR ProbeDepth cs_6_5 pipeline creation failed");
    impl_->device=device; impl_->device5=std::move(device5); impl_->root=std::move(root);
    impl_->query_pipeline=std::move(queries); impl_->scene_pipeline=std::move(scene);
    impl_->depth_pipeline=std::move(receivers);
  });
}
HRESULT RayTracingScene::BeginFrame(uint32_t slot) {
  return impl_->Run([&] {
    Require(impl_->device5!=nullptr,"Initialize must succeed first",E_UNEXPECTED);
    Require(slot<kFrameSlots,"Frame slot outside [0,3)");
    impl_->frames[slot]=Impl::Frame{}; impl_->active=static_cast<int>(slot);
  });
}
HRESULT RayTracingScene::AddTriangles(std::span<const RayTracingGeometry> geometry) {
  return impl_->Run([&] {
    auto& frame=impl_->Current();
    Require(!frame.built,"Cannot add geometry after BuildScene; BeginFrame required",E_UNEXPECTED);
    Require(!geometry.empty(),"No geometry supplied");
    Require(geometry.size()<=D3D12_RAYTRACING_MAX_INSTANCES_PER_TOP_LEVEL_ACCELERATION_STRUCTURE-frame.geometry.size(),
            "Too many DXR instances");
    for (const auto& g:geometry) {
      Require(g.vertices!=nullptr,"Null vertex buffer"); impl_->SameDevice(g.vertices);
      const auto d=g.vertices->GetDesc();
      Require(d.Dimension==D3D12_RESOURCE_DIMENSION_BUFFER && !(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE),
              "Vertices must be a shader-readable buffer");
      Require(g.vertex_count>=3 && g.vertex_count%3==0,"Vertex count must be a nonempty triangle list");
      Require(g.vertex_count/3<=D3D12_RAYTRACING_MAX_PRIMITIVES_PER_BOTTOM_LEVEL_ACCELERATION_STRUCTURE,
              "Triangle count exceeds DXR BLAS primitive limit");
      Require(g.offset%4==0 && g.offset<=d.Width && uint64_t(g.vertex_count)*12<=d.Width-g.offset,
              "Vertex offset/count outside buffer or not float-aligned");
      Require(g.vertices->GetGPUVirtualAddress()!=0,"Vertex buffer has no GPU address");
      D3D12_HEAP_PROPERTIES props{}; D3D12_HEAP_FLAGS heap_flags{};
      Check(g.vertices->GetHeapProperties(&props,&heap_flags),"Vertex heap query failed");
      Require(props.Type!=D3D12_HEAP_TYPE_READBACK,"Readback buffer cannot be BLAS input");
      if (g.material.valid) {
        Require(Finite3(g.material.albedo),"Valid material albedo must be finite");
        for (float a:g.material.albedo) Require(a>=0 && a<=1,"Valid material albedo must be linear RGB [0,1]");
      }
    }
    frame.geometry.reserve(frame.geometry.size()+geometry.size());
    frame.resources.reserve(frame.resources.size()+geometry.size());
    for (const auto& g:geometry) { frame.geometry.push_back(g); frame.resources.emplace_back(g.vertices); }
  });
}
HRESULT RayTracingScene::BuildScene(ID3D12GraphicsCommandList* list,std::span<const RayTracingGeometry> geometry) {
  HRESULT hr=AddTriangles(geometry);
  if (FAILED(hr)) return hr;
  auto& frame=impl_->frames[impl_->active];
  const auto old_geometry=frame.geometry.size()-geometry.size();
  const auto old_resources=frame.resources.size()-geometry.size();
  hr=BuildScene(list);
  if (FAILED(hr)) {
    // BuildScene records nothing until validation and every allocation succeed.
    // A retry of this convenience overload must not append duplicate instances.
    frame.geometry.resize(old_geometry); frame.resources.resize(old_resources);
  }
  return hr;
}
HRESULT RayTracingScene::BuildScene(ID3D12GraphicsCommandList* list) {
  return impl_->Run([&] {
    auto& frame=impl_->Current();
    Require(!frame.built,"Only one BuildScene per slot frame",E_UNEXPECTED);
    Require(!frame.geometry.empty(),"Cannot build empty DXR scene");
    auto list4=impl_->List4(list);
    const auto count=static_cast<uint32_t>(frame.geometry.size());
    struct Bottom {
      D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
      D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
      ComPtr<ID3D12Resource> result,scratch;
    };
    std::vector<Bottom> bottoms(count);
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances(count);
    std::vector<std::array<float,4>> materials(count);
    std::vector<ComPtr<ID3D12Resource>> keep;
    keep.reserve(count*2+4);
    bool has_materials=false;
    for (uint32_t i=0;i<count;++i) {
      const auto& g=frame.geometry[i]; auto& b=bottoms[i];
      b.geometry.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
      b.geometry.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
      b.geometry.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT;
      b.geometry.Triangles.VertexCount=g.vertex_count;
      b.geometry.Triangles.VertexBuffer={g.vertices->GetGPUVirtualAddress()+g.offset,12};
      b.inputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
      b.inputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
      b.inputs.NumDescs=1; b.inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; b.inputs.pGeometryDescs=&b.geometry;
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
      impl_->device5->GetRaytracingAccelerationStructurePrebuildInfo(&b.inputs,&info);
      Require(info.ResultDataMaxSizeInBytes && info.ScratchDataSizeInBytes,"BLAS prebuild returned zero sizes",E_FAIL);
      b.result=impl_->Buffer(info.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,true);
      b.scratch=impl_->Buffer(info.ScratchDataSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
      keep.push_back(b.result); keep.push_back(b.scratch);
      auto& instance=instances[i];
      instance.Transform[0][0]=instance.Transform[1][1]=instance.Transform[2][2]=1;
      instance.InstanceID=i; instance.InstanceMask=g.character ? 2 : 1;
      instance.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
      instance.AccelerationStructure=b.result->GetGPUVirtualAddress();
      materials[i]={0,0,0,0};
      if (g.material.valid) {
        materials[i]={g.material.albedo[0],g.material.albedo[1],g.material.albedo[2],1}; has_materials=true;
      }
    }
    auto instance_buffer=impl_->Upload(instances.data(),instances.size()*sizeof(instances[0]));
    auto material_buffer=impl_->Upload(materials.data(),materials.size()*sizeof(materials[0]));
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top{};
    top.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    top.NumDescs=count; top.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; top.InstanceDescs=instance_buffer->GetGPUVirtualAddress();
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    impl_->device5->GetRaytracingAccelerationStructurePrebuildInfo(&top,&info);
    Require(info.ResultDataMaxSizeInBytes && info.ScratchDataSizeInBytes,"TLAS prebuild returned zero sizes",E_FAIL);
    auto tlas=impl_->Buffer(info.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,
                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,true);
    auto scratch=impl_->Buffer(info.ScratchDataSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
    keep.push_back(instance_buffer); keep.push_back(scratch); keep.push_back(tlas); keep.push_back(material_buffer);
    frame.resources.reserve(frame.resources.size()+keep.size());
    // All allocation/validation finishes before recording any build commands.
    for (const auto& b:bottoms) {
      D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
      build.Inputs=b.inputs; build.DestAccelerationStructureData=b.result->GetGPUVirtualAddress();
      build.ScratchAccelerationStructureData=b.scratch->GetGPUVirtualAddress();
      list4->BuildRaytracingAccelerationStructure(&build,0,nullptr); UAVBarrier(list,b.result.Get());
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs=top; build.DestAccelerationStructureData=tlas->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData=scratch->GetGPUVirtualAddress();
    list4->BuildRaytracingAccelerationStructure(&build,0,nullptr); UAVBarrier(list,tlas.Get());
    for (auto& resource:keep) frame.resources.push_back(std::move(resource));
    frame.tlas=std::move(tlas); frame.materials=std::move(material_buffer);
    frame.has_materials=has_materials; frame.built=true;
  });
}
HRESULT RayTracingScene::TraceRays(ID3D12GraphicsCommandList* list,std::span<const RayTracingRay> rays,ID3D12Resource* results) {
  return impl_->Run([&] {
    auto& frame=impl_->Current(true); auto list4=impl_->List4(list);
    Require(!rays.empty() && rays.size()<=65535*64,"Ray batch must contain 1..4194240 rays");
    Require(results!=nullptr,"Null ray results buffer"); impl_->SameDevice(results);
    const auto d=results->GetDesc();
    Require(d.Dimension==D3D12_RESOURCE_DIMENSION_BUFFER && (d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) &&
            d.Width>=rays.size()*sizeof(RayTracingHit),"Ray results need a sufficiently large UAV buffer");
    for (const auto& ray:rays) {
      Require(Finite3(ray.origin) && Finite3(ray.direction) && std::abs(Length2(ray.direction)-1)<0.001f,
              "Ray origin/direction must be finite; direction must be unit length");
      Require(std::isfinite(ray.t_min) && std::isfinite(ray.t_max) && ray.t_min>=0 && ray.t_max>ray.t_min,"Invalid ray interval");
      Require(ray.mask>0 && (ray.mask&~3u)==0,"Ray mask must select world=1 and/or character=2");
    }
    auto upload=impl_->Upload(rays.data(),rays.size_bytes());
    frame.resources.reserve(frame.resources.size()+2);
    frame.resources.push_back(upload); frame.resources.emplace_back(results);
    Constants c{}; c.ray_count=static_cast<uint32_t>(rays.size()); c.geometry_count=static_cast<uint32_t>(frame.geometry.size());
    impl_->Bind(list,frame,c,impl_->query_pipeline.Get());
    list->SetComputeRootShaderResourceView(3,upload->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(4,results->GetGPUVirtualAddress());
    list->Dispatch((c.ray_count+63)/64,1,1); UAVBarrier(list,results);
  });
}
HRESULT RayTracingScene::Trace(ID3D12GraphicsCommandList* list,const RayTracingTraceInputs& in) {
  return impl_->Run([&] {
    auto& frame=impl_->Current(true); auto list4=impl_->List4(list);
    Require(in.depth_valid && in.color_valid && in.inv_view_projection_valid && in.camera_valid && in.light_valid,
            "Trace requires explicitly validated depth, color, invVP, camera and light");
    Require(in.depth && in.color,"Trace needs depth and color resources");
    Require(in.width>=2 && in.height>=2 && in.width<=D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION &&
            in.height<=D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION,"Trace dimensions must be 2..16384");
    impl_->SameDevice(in.depth); impl_->SameDevice(in.color);
    auto depth=in.depth->GetDesc(), color=in.color->GetDesc();
    for (const auto& d:{depth,color}) {
      Require(d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.Width==in.width && d.Height==in.height &&
              d.DepthOrArraySize==1 && d.SampleDesc.Count==1 && !(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE),
              "Trace inputs must be matching single-sample shader-readable 2D textures");
    }
    const bool d24=depth.Format==DXGI_FORMAT_R24G8_TYPELESS;
    Require(d24 || depth.Format==DXGI_FORMAT_R32_FLOAT || depth.Format==DXGI_FORMAT_R32_TYPELESS,
            "Trace depth requires R24G8_TYPELESS or R32_FLOAT/TYPELESS");
    Require(color.Format==DXGI_FORMAT_R8G8B8A8_UNORM || color.Format==DXGI_FORMAT_R16G16B16A16_FLOAT ||
            color.Format==DXGI_FORMAT_R32G32B32A32_FLOAT,"Unsupported color SRV format");
    for (float v:in.inv_view_projection) Require(std::isfinite(v),"Nonfinite invVP");
    Require(Invertible(in.inv_view_projection),"Singular invVP");
    Require(Finite3(in.camera) && Finite3(in.direction_to_light),"Nonfinite camera/light");
    const float light2=Length2(in.direction_to_light);
    Require(std::isfinite(light2) && light2>1e-16f,"Zero/invalid light direction");
    Require(std::isfinite(in.ray_bias) && std::isfinite(in.max_distance) && in.ray_bias>0 &&
            in.max_distance>in.ray_bias,"Invalid trace ray interval/bias");
    Require(std::isfinite(in.cone_half_angle) && in.cone_half_angle>=0 && in.cone_half_angle<1.5707963f,"Invalid light cone half angle");
    Require(in.clear_depth==0 || in.clear_depth==1,"Clear depth must explicitly select 0 or 1");
    Require(std::isfinite(in.max_depth_delta) && in.max_depth_delta>=0 && in.max_depth_delta<=1,"Invalid depth-normal threshold");
    const auto samples=static_cast<uint32_t>(in.shadow_quality);
    Require(samples==4 || samples==16,"Shadow quality must be Low or High");
    auto shadow=impl_->Texture(in.width,in.height,DXGI_FORMAT_R32_FLOAT);
    auto reflection=impl_->Texture(in.width,in.height,DXGI_FORMAT_R32G32B32A32_FLOAT);
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap_desc.NumDescriptors=4;
    heap_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    Check(impl_->device->CreateDescriptorHeap(&heap_desc,IID_PPV_ARGS(&heap)),"DXR descriptor heap allocation failed");
    auto handle=heap->GetCPUDescriptorHandleForHeapStart();
    const auto stride=impl_->device->GetDescriptorHandleIncrementSize(heap_desc.Type);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Format=d24 ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : DXGI_FORMAT_R32_FLOAT;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels=1;
    impl_->device->CreateShaderResourceView(in.depth,&srv,handle); handle.ptr+=stride;
    srv.Format=color.Format; impl_->device->CreateShaderResourceView(in.color,&srv,handle); handle.ptr+=stride;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D; uav.Format=DXGI_FORMAT_R32_FLOAT;
    impl_->device->CreateUnorderedAccessView(shadow.Get(),nullptr,&uav,handle); handle.ptr+=stride;
    uav.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    impl_->device->CreateUnorderedAccessView(reflection.Get(),nullptr,&uav,handle);
    frame.resources.reserve(frame.resources.size()+4); frame.heaps.reserve(frame.heaps.size()+1);
    frame.resources.emplace_back(in.depth); frame.resources.emplace_back(in.color);
    frame.resources.push_back(shadow); frame.resources.push_back(reflection); frame.heaps.push_back(heap);
    Constants c{};
    std::memcpy(c.inv_vp,in.inv_view_projection,sizeof(c.inv_vp)); std::memcpy(c.camera,in.camera,sizeof(c.camera));
    for (int i=0;i<3;++i) c.light[i]=in.direction_to_light[i]/std::sqrt(light2);
    c.flags=(in.exclude_character_shadows ? 1u : 0u) | (in.exclude_character_reflections ? 2u : 0u) |
            (in.enable_reflections && frame.has_materials ? 4u : 0u);
    c.width=in.width; c.height=in.height; c.samples=samples; c.geometry_count=static_cast<uint32_t>(frame.geometry.size());
    c.bias=in.ray_bias; c.max_distance=in.max_distance; c.cone_angle=in.cone_half_angle;
    c.clear_depth=in.clear_depth; c.max_depth_delta=in.max_depth_delta;
    impl_->Bind(list,frame,c,impl_->scene_pipeline.Get());
    ID3D12DescriptorHeap* heaps[]{heap.Get()}; list->SetDescriptorHeaps(1,heaps);
    list->SetComputeRootDescriptorTable(5,heap->GetGPUDescriptorHandleForHeapStart());
    list->Dispatch((in.width+7)/8,(in.height+7)/8,1); UAVBarrier(list,shadow.Get()); UAVBarrier(list,reflection.Get());
    frame.outputs={shadow.Get(),reflection.Get(),in.width,in.height,in.enable_reflections && frame.has_materials};
  });
}
RayTracingOutputs RayTracingScene::Outputs() const {
  return impl_->active>=0 ? impl_->frames[impl_->active].outputs : RayTracingOutputs{};
}
ID3D12Resource* RayTracingScene::AccelerationStructure() const {
  return impl_->active>=0 && impl_->frames[impl_->active].built ? impl_->frames[impl_->active].tlas.Get() : nullptr;
}
HRESULT RayTracingScene::ProbeDepth(ID3D12GraphicsCommandList* list,const RayTracingDepthInputs& in,ID3D12Resource* results) {
  return impl_->Run([&] {
    auto& frame=impl_->Current(true); auto list4=impl_->List4(list);
    Require(in.depth && results,"Depth probe needs depth and results");
    impl_->SameDevice(in.depth); impl_->SameDevice(results);
    Require(in.width && in.height && in.width<=16384 && in.height<=16384 &&
            in.grid_width && in.grid_height && in.grid_width<=in.width && in.grid_height<=in.height &&
            uint64_t(in.grid_width)*in.grid_height<=4194240,"Invalid depth probe dimensions");
    const auto d=in.depth->GetDesc(),r=results->GetDesc();
    Require(d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.Width==in.width && d.Height==in.height &&
            d.DepthOrArraySize==1 && d.SampleDesc.Count==1 && !(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE),
            "Depth probe requires a matching shader-readable single-sample 2D depth texture");
    const bool d24=d.Format==DXGI_FORMAT_R24G8_TYPELESS;
    Require(d24 || d.Format==DXGI_FORMAT_R32_FLOAT || d.Format==DXGI_FORMAT_R32_TYPELESS,
            "Unsupported depth probe format; use R24G8_TYPELESS or R32_FLOAT/TYPELESS");
    Require(r.Dimension==D3D12_RESOURCE_DIMENSION_BUFFER && (r.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) &&
            r.Width>=uint64_t(in.grid_width)*in.grid_height*sizeof(RayTracingDepthHit),"Depth probe results UAV too small");
    for(float value:in.inv_view_projection) Require(std::isfinite(value),"Nonfinite probe invVP");
    Require(Invertible(in.inv_view_projection) && Finite3(in.camera),"Invalid probe camera/projection");
    Require(in.clear_depth==0 || in.clear_depth==1,"Depth probe clear must be zero or one");
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; desc.NumDescriptors=4;
    desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    Check(impl_->device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap)),"Depth probe descriptor heap failed");
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Format=d24 ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : DXGI_FORMAT_R32_FLOAT;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels=1;
    impl_->device->CreateShaderResourceView(in.depth,&srv,heap->GetCPUDescriptorHandleForHeapStart());
    frame.resources.reserve(frame.resources.size()+2); frame.heaps.reserve(frame.heaps.size()+1);
    frame.resources.emplace_back(in.depth); frame.resources.emplace_back(results); frame.heaps.push_back(heap);
    Constants c{}; std::memcpy(c.inv_vp,in.inv_view_projection,sizeof(c.inv_vp));
    std::memcpy(c.camera,in.camera,sizeof(c.camera)); c.width=in.width; c.height=in.height;
    c.flags=in.grid_width; c.samples=in.grid_height; c.clear_depth=in.clear_depth;
    impl_->Bind(list,frame,c,impl_->depth_pipeline.Get());
    ID3D12DescriptorHeap* heaps[]{heap.Get()}; list->SetDescriptorHeaps(1,heaps);
    list->SetComputeRootDescriptorTable(5,heap->GetGPUDescriptorHandleForHeapStart());
    list->SetComputeRootUnorderedAccessView(4,results->GetGPUVirtualAddress());
    list->Dispatch((in.grid_width+7)/8,(in.grid_height+7)/8,1); UAVBarrier(list,results);
  });
}
}  // namespace superman_returns::graphics::d3d12
