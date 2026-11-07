#include "visible_ray_scene.h"
#include "../pipeline_creation_audit.h"
#include "shaders/visible_ray_ConvertClips.h"
#include "shaders/visible_ray_TraceVisible.h"
#include "shaders/visible_ray_DenoiseVisible.h"
#include "shaders/visible_ray_CompositeVisible.h"
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

namespace superman_returns::graphics::d3d12 {
using Microsoft::WRL::ComPtr;
namespace {
struct Failure {HRESULT hr;const char* message;};
void Check(HRESULT hr,const char* message) {if(FAILED(hr))throw Failure{hr,message};}
void Require(bool condition,const char* message) {if(!condition)throw Failure{E_INVALIDARG,message};}
void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}
void Uav(ID3D12GraphicsCommandList* list,ID3D12Resource* resource) {
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=resource;list->ResourceBarrier(1,&b);
}
bool Matrix(const float* p) {
  if(!p)return false;
  double m[4][4];for(int r=0;r<4;++r)for(int c=0;c<4;++c) {if(!std::isfinite(p[r*4+c]))return false;m[r][c]=p[r*4+c];}
  for(int c=0;c<4;++c) {
    int pivot=c;for(int r=c+1;r<4;++r)if(std::abs(m[r][c])>std::abs(m[pivot][c]))pivot=r;
    if(std::abs(m[pivot][c])<1e-20)return false;
    for(int k=0;k<4;++k)std::swap(m[c][k],m[pivot][k]);
    for(int r=c+1;r<4;++r) {const double s=m[r][c]/m[c][c];for(int k=c;k<4;++k)m[r][k]-=s*m[c][k];}
  }
  return true;
}
struct Constants {
  float inverse[16],vp[16],previous[16];
  float camera[3];uint32_t flags;
  float light[3];uint32_t samples;
  uint32_t width,height,trace_width,trace_height;
  float bias,distance,clear_depth,max_delta;
  float shadow_cone,reflection_distance,reflection_cone,depth_rejection;
  uint32_t reflection_samples;float history_weight;uint32_t seed,padding;
  float current_jitter[2],previous_jitter[2];
};
static_assert(sizeof(Constants)==304);
// Exclude camera, jitter and stochastic seed: they are handled by reprojection.
// Compare named values, never struct padding or the upload-buffer bytes.
struct HistorySettings {
  uint32_t flags,samples,reflection_samples;
  float trace_scale,bias,distance,clear_depth,max_delta;
  float shadow_cone,reflection_distance,reflection_cone,depth_rejection,history_weight;
  std::array<float,3> light;
  bool operator==(const HistorySettings&) const = default;
};
struct ConvertConstants {float inverse[16];uint32_t count,offset,stride,padding;};
}

struct VisibleRayScene::Impl {
  struct Frame {
    std::vector<ComPtr<ID3D12Resource>> keep;
    std::vector<ComPtr<ID3D12DescriptorHeap>> heaps;
    ComPtr<ID3D12Resource> shadow,reflection,depth;
    ComPtr<ID3D12Resource> constants,source_color,source_depth;
    std::array<DXGI_FORMAT,10> formats{};
    VisibleRayOutputs outputs{};
    HistorySettings history_settings{};
    float inverse[16]{};
    bool built=false,traced=false,composited=false;
  };
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12RootSignature> convert_root,trace_root;
  ComPtr<ID3D12PipelineState> convert_pso,trace_pso,filter_pso,composite_pso;
  RayTracingScene scene;
  std::array<Frame,3> frames;
  int active=-1,previous_slot=-1;
  uint64_t previous_serial=0;
  std::string error;
  template<class F> HRESULT Run(F&& function) {
    try {function();error.clear();return S_OK;}
    catch(const Failure& f) {if(active>=0)frames[active].outputs={};error=f.message;return f.hr;}
    catch(const std::bad_alloc&) {if(active>=0)frames[active].outputs={};error="Visible DXR allocation failed";return E_OUTOFMEMORY;}
  }
  void SceneCheck(HRESULT hr) {Check(hr,scene.LastError());}
  Frame& Current(ID3D12GraphicsCommandList* list) {
    Require(device && active>=0,"Initialize and BeginFrame required");
    Require(list && (list->GetType()==D3D12_COMMAND_LIST_TYPE_DIRECT || list->GetType()==D3D12_COMMAND_LIST_TYPE_COMPUTE),"DIRECT/COMPUTE list required");
    SameDevice(list);return frames[active];
  }
  void SameDevice(ID3D12DeviceChild* child) {
    Require(child!=nullptr,"Null resource/list");ComPtr<ID3D12Device> owner;
    Check(child->GetDevice(IID_PPV_ARGS(&owner)),"Cannot query input device");Require(owner.Get()==device.Get(),"Input belongs to another device");
  }
  ComPtr<ID3D12Resource> Resource(uint64_t width,uint32_t height,DXGI_FORMAT format,bool upload=false) {
    D3D12_HEAP_PROPERTIES props{};props.Type=upload?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};d.Dimension=height?D3D12_RESOURCE_DIMENSION_TEXTURE2D:D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width=width;d.Height=height?height:1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Format=format;
    d.Layout=height?D3D12_TEXTURE_LAYOUT_UNKNOWN:D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags=upload?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&d,upload?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&result)),"Visible DXR resource allocation failed");return result;
  }
  ComPtr<ID3D12RootSignature> Root(const D3D12_ROOT_SIGNATURE_DESC& desc) {
    ComPtr<ID3DBlob> blob,errors;ComPtr<ID3D12RootSignature> result;
    Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors),"Visible DXR root serialization failed");
    Check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&result)),"Visible DXR root allocation failed");return result;
  }
  ComPtr<ID3D12PipelineState> Pipeline(ID3D12RootSignature* root,const void* shader,size_t size) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=root;desc.CS={shader,size};
    ComPtr<ID3D12PipelineState> result;graphics::RecordRuntimePipelineCreate();Check(device->CreateComputePipelineState(&desc,IID_PPV_ARGS(&result)),"Visible DXR compute pipeline failed");return result;
  }
  DXGI_FORMAT TextureInput(ID3D12Resource* resource,uint32_t width,uint32_t height) {
    SameDevice(resource);const auto d=resource->GetDesc();
    Require(d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.Width==width && d.Height==height &&
        d.DepthOrArraySize==1 && d.MipLevels==1 && d.SampleDesc.Count==1 && !(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE),"Visible DXR input shape/SRV mismatch");return d.Format;
  }
  ComPtr<ID3D12DescriptorHeap> Heap(const std::array<ID3D12Resource*,10>& inputs,const std::array<DXGI_FORMAT,10>& formats,
                                   const std::array<ID3D12Resource*,3>& outputs,ID3D12Resource* counters=nullptr) {
    D3D12_DESCRIPTOR_HEAP_DESC d{};d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;d.NumDescriptors=14;d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> result;Check(device->CreateDescriptorHeap(&d,IID_PPV_ARGS(&result)),"Visible DXR descriptor allocation failed");
    auto handle=result->GetCPUDescriptorHandleForHeapStart();const auto step=device->GetDescriptorHandleIncrementSize(d.Type);
    for(size_t i=0;i<inputs.size();++i) {
      D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=formats[i];srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
      srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
      device->CreateShaderResourceView(inputs[i],&srv,handle);handle.ptr+=step;
    }
    for(size_t i=0;i<outputs.size();++i) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
      uav.Format=i==1?(outputs[i]?outputs[i]->GetDesc().Format:DXGI_FORMAT_R32G32B32A32_FLOAT):DXGI_FORMAT_R32_FLOAT;
      device->CreateUnorderedAccessView(outputs[i],nullptr,&uav,handle);handle.ptr+=step;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC counter_view{};counter_view.Format=DXGI_FORMAT_R32_TYPELESS;
    counter_view.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;counter_view.Buffer.NumElements=2;
    counter_view.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
    device->CreateUnorderedAccessView(counters,nullptr,&counter_view,handle);
    return result;
  }
};

VisibleRayScene::VisibleRayScene():impl_(std::make_unique<Impl>()) {}
VisibleRayScene::~VisibleRayScene()=default;
const char* VisibleRayScene::LastError() const {return impl_->error.c_str();}
VisibleRayOutputs VisibleRayScene::Outputs() const {return impl_->active>=0?impl_->frames[impl_->active].outputs:VisibleRayOutputs{};}
ID3D12Resource* VisibleRayScene::AccelerationStructure() const {return impl_->scene.AccelerationStructure();}
HRESULT VisibleRayScene::Initialize(ID3D12Device* device) {
  return impl_->Run([&] {
    Require(device && !impl_->device,"Initialize needs a device and may succeed once");
    impl_->SceneCheck(impl_->scene.Initialize(device));impl_->device=device;
    D3D12_ROOT_PARAMETER convert[3]{};convert[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;convert[0].Constants={0,0,sizeof(ConvertConstants)/4};
    convert[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;convert[1].Descriptor={0,0};
    convert[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;convert[2].Descriptor={0,0};
    D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=convert;
    impl_->convert_root=impl_->Root(desc);impl_->convert_pso=impl_->Pipeline(impl_->convert_root.Get(),kVisibleRayConvert,sizeof(kVisibleRayConvert));
    D3D12_DESCRIPTOR_RANGE ranges[2]{{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,10,1,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,4,0,0,10}};
    D3D12_ROOT_PARAMETER trace[3]{};trace[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;trace[0].Descriptor={0,0};
    trace[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;trace[1].Descriptor={0,0};
    trace[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;trace[2].DescriptorTable={2,ranges};
    desc.pParameters=trace;impl_->trace_root=impl_->Root(desc);
    impl_->trace_pso=impl_->Pipeline(impl_->trace_root.Get(),kVisibleRayTrace,sizeof(kVisibleRayTrace));
    impl_->filter_pso=impl_->Pipeline(impl_->trace_root.Get(),kVisibleRayDenoise,sizeof(kVisibleRayDenoise));
    impl_->composite_pso=impl_->Pipeline(impl_->trace_root.Get(),kVisibleRayComposite,sizeof(kVisibleRayComposite));
  });
}
HRESULT VisibleRayScene::BeginFrame(uint32_t slot) {
  return impl_->Run([&] {
    Require(impl_->device && slot<3,"Invalid visible DXR frame slot");
    if(impl_->previous_slot==int(slot))impl_->previous_slot=-1;
    impl_->frames[slot]=Impl::Frame{};impl_->active=int(slot);
    impl_->SceneCheck(impl_->scene.BeginFrame(slot));
  });
}
HRESULT VisibleRayScene::Build(ID3D12GraphicsCommandList* list,std::span<const VisibleRayGeometry> geometry,const float* inverse) {
  return impl_->Run([&] {
    auto& f=impl_->Current(list);f.outputs={};Require(!f.built,"One visible scene build per BeginFrame");Require(Matrix(inverse),"Invalid final inverse projection");
    Require(geometry.size()<=4096,"Too many visible scene spans");
    std::vector<RayTracingGeometry> triangles;std::vector<const VisibleRayGeometry*> selected;
    uint64_t total=0;
    for(const auto& g:geometry) {
      if(!g.opaque || !g.same_camera)continue;
      impl_->SameDevice(g.clips);const auto d=g.clips->GetDesc();
      Require(d.Dimension==D3D12_RESOURCE_DIMENSION_BUFFER && !(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE),"Clip input must be an SRV buffer");
      Require(g.vertex_count && g.vertex_count%3==0 && g.stride>=16 && g.stride%4==0 && g.offset%4==0,"Invalid post-VS clip triangle layout");
      Require(g.offset<=UINT32_MAX && g.offset<=d.Width && uint64_t(g.vertex_count-1)*g.stride+16<=d.Width-g.offset &&
          uint64_t(g.vertex_count-1)*g.stride+16<=UINT32_MAX-g.offset,"Clip byte-address range overflow");
      total+=g.vertex_count;Require(total<=kMaxVertices,"Visible clip arena exceeds 64MiB triangle budget");
      selected.push_back(&g);
    }
    Require(!selected.empty(),"No attested opaque same-camera geometry");
    // One bounded 48MiB world arena and at most two BLAS builds per frame.
    // Preserve every draw span while grouping world/character instance masks.
    auto world=impl_->Resource(total*12,0,DXGI_FORMAT_UNKNOWN);
    std::array<RayTracingGeometry,2> groups{};uint64_t world_offset=0;
    for(const auto* g:selected) {
      auto& group=groups[g->character?1:0];
      if(!group.vertices) {group.vertices=world.Get();group.offset=world_offset;group.vertex_count=g->vertex_count;group.character=g->character;}
      else group.more.push_back({world_offset,g->vertex_count});
      world_offset+=uint64_t(g->vertex_count)*12;f.keep.emplace_back(g->clips);
    }
    for(const auto& group:groups)if(group.vertices)triangles.push_back(group);
    f.keep.push_back(world);impl_->SceneCheck(impl_->scene.AddTriangles(triangles));world_offset=0;
    for(size_t i=0;i<selected.size();++i) {
      const auto& g=*selected[i];
      ConvertConstants constants{};std::memcpy(constants.inverse,inverse,64);constants.count=g.vertex_count;constants.offset=uint32_t(g.offset);constants.stride=g.stride;constants.padding=uint32_t(world_offset);
      list->SetComputeRootSignature(impl_->convert_root.Get());list->SetPipelineState(impl_->convert_pso.Get());
      list->SetComputeRoot32BitConstants(0,sizeof(constants)/4,&constants,0);list->SetComputeRootShaderResourceView(1,g.clips->GetGPUVirtualAddress());
      list->SetComputeRootUnorderedAccessView(2,world->GetGPUVirtualAddress());list->Dispatch((g.vertex_count/3+63)/64,1,1);
      world_offset+=uint64_t(g.vertex_count)*12;
    }
    Uav(list,world.Get());Transition(list,world.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    impl_->SceneCheck(impl_->scene.BuildScene(list));std::memcpy(f.inverse,inverse,64);f.built=true;
  });
}
HRESULT VisibleRayScene::Trace(ID3D12GraphicsCommandList* list,const VisibleRayInputs& in) {
  return impl_->Run([&] {
    auto& f=impl_->Current(list);f.outputs={};const auto& s=in.scene;
    Require(f.built && !f.traced,"Trace requires a freshly built visible scene");
    Require(s.width>=2 && s.height>=2 && s.width<=8192 && s.height<=8192 && uint64_t(s.width)*s.height<=16777216,"Visible trace size outside budget");
    Require(s.depth_valid && s.color_valid && s.inv_view_projection_valid && s.camera_valid,"Visible trace requires attested scene/camera inputs");
    Require(Matrix(s.inv_view_projection) && Matrix(in.view_projection) && std::memcmp(f.inverse,s.inv_view_projection,64)==0,"Trace/build camera mismatch");
    for(int r=0;r<4;++r)for(int col=0;col<4;++col) {
      double product=0;for(int k=0;k<4;++k)product+=double(in.view_projection[r*4+k])*s.inv_view_projection[k*4+col];
      Require(std::abs(product-(r==col?1:0))<.02,"Final projection/inverse mismatch");
    }
    for(float v:s.camera)Require(std::isfinite(v),"Nonfinite visible camera");
    Require(s.clear_depth==0 || s.clear_depth==1,"Visible depth clear must be 0 or 1");
    Require(std::isfinite(s.ray_bias) && s.ray_bias>0 && std::isfinite(s.max_distance) && s.max_distance>s.ray_bias,"Invalid visible shadow ray interval");
    Require(std::isfinite(s.max_depth_delta) && s.max_depth_delta>=0 && s.max_depth_delta<=1,"Invalid visible depth discontinuity threshold");
    Require(in.trace_scale==1 || in.trace_scale==.5f || in.trace_scale==.25f,"Trace scale must be 1/.5/.25");
    Require(in.reflection_rays==1 || in.reflection_rays==2 || in.reflection_rays==4,"Reflection ray count must be 1/2/4");
    Require(std::isfinite(in.reflection_distance) && in.reflection_distance>s.ray_bias,"Invalid reflection distance");
    Require(std::isfinite(in.depth_rejection) && in.depth_rejection>0 && in.depth_rejection<=.1f &&
        std::isfinite(in.history_weight) && in.history_weight>=0 && in.history_weight<=.95f,"Invalid visible temporal rejection/weight");
    Require(std::isfinite(s.cone_half_angle) && s.cone_half_angle>=0 && s.cone_half_angle<1.57f &&
        std::isfinite(in.reflection_cone_angle) && in.reflection_cone_angle>=0 && in.reflection_cone_angle<1.57f,"Invalid ray cone");
    const auto samples=uint32_t(s.shadow_quality);Require(samples==4 || samples==16,"Visible shadow quality must select 4/16 rays");
    float light_length=0;for(float v:s.direction_to_light) {Require(std::isfinite(v),"Nonfinite sun direction");light_length+=v*v;}
    Require(!in.enable_shadows || (s.light_valid && light_length>1e-16f && std::isfinite(light_length)),"Attested nonzero sunlight required");
    DXGI_FORMAT depth_format=impl_->TextureInput(s.depth,s.width,s.height),color_format=impl_->TextureInput(s.color,s.width,s.height);
    Require(depth_format==DXGI_FORMAT_R24G8_TYPELESS || depth_format==DXGI_FORMAT_R32_TYPELESS || depth_format==DXGI_FORMAT_R32_FLOAT,"Unsupported visible depth format");
    if(depth_format==DXGI_FORMAT_R24G8_TYPELESS)depth_format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;else depth_format=DXGI_FORMAT_R32_FLOAT;
    Require(color_format==DXGI_FORMAT_R8G8B8A8_UNORM || color_format==DXGI_FORMAT_R16G16B16A16_FLOAT || color_format==DXGI_FORMAT_R32G32B32A32_FLOAT,"Unsupported lit-scene color format");
    DXGI_FORMAT motion_format=DXGI_FORMAT_R16G16_FLOAT,valid_format=DXGI_FORMAT_R8_UINT;
    if(in.reactivity) Require(impl_->TextureInput(in.reactivity,s.width,s.height)==DXGI_FORMAT_R32_UINT,"Reactivity must be R32_UINT at scene size");
    for(float jitter:{in.current_jitter[0],in.current_jitter[1],in.previous_jitter[0],in.previous_jitter[1]})
      Require(std::isfinite(jitter) && std::abs(jitter)<=.5f,"Invalid raster jitter");
    Require(bool(in.motion)==bool(in.validity),"Motion and validity must be supplied together");
    if(in.motion) {
      motion_format=impl_->TextureInput(in.motion,s.width,s.height);valid_format=impl_->TextureInput(in.validity,s.width,s.height);
      Require(motion_format==DXGI_FORMAT_R16G16_FLOAT || motion_format==DXGI_FORMAT_R32G32_FLOAT,"Unsupported motion format");
      Require(valid_format==DXGI_FORMAT_R8_UINT || valid_format==DXGI_FORMAT_R32_UINT,"Unsupported motion validity format");
    }
    const uint32_t tw=uint32_t(std::ceil(s.width*in.trace_scale)),th=uint32_t(std::ceil(s.height*in.trace_scale));
    auto raw_shadow=impl_->Resource(tw,th,DXGI_FORMAT_R32_FLOAT),raw_reflection=impl_->Resource(tw,th,DXGI_FORMAT_R32G32B32A32_FLOAT);
    auto shadow=impl_->Resource(s.width,s.height,DXGI_FORMAT_R32_FLOAT),reflection=impl_->Resource(s.width,s.height,DXGI_FORMAT_R32G32B32A32_FLOAT),depth=impl_->Resource(s.width,s.height,DXGI_FORMAT_R32_FLOAT);
    Constants c{};std::memcpy(c.inverse,s.inv_view_projection,64);std::memcpy(c.vp,in.view_projection,64);std::memcpy(c.previous,in.previous_view_projection,64);std::memcpy(c.camera,s.camera,12);
    c.flags=(in.enable_shadows?1u:0u)|(s.enable_reflections?2u:0u)|(s.exclude_character_shadows?4u:0u)|(s.exclude_character_reflections?8u:0u);
    for(int i=0;i<3;++i)c.light[i]=light_length>1e-16f?s.direction_to_light[i]/std::sqrt(light_length):(i==1?1.f:0.f);
    c.samples=samples;c.width=s.width;c.height=s.height;c.trace_width=tw;c.trace_height=th;c.bias=s.ray_bias;c.distance=s.max_distance;
    c.clear_depth=s.clear_depth;c.max_delta=s.max_depth_delta;c.shadow_cone=s.cone_half_angle;c.reflection_distance=in.reflection_distance;
    c.reflection_cone=in.reflection_cone_angle;c.depth_rejection=in.depth_rejection;c.reflection_samples=in.reflection_rays;c.history_weight=in.history_weight;c.seed=uint32_t(in.frame_serial);
    const HistorySettings settings{c.flags,c.samples,c.reflection_samples,in.trace_scale,
        c.bias,c.distance,c.clear_depth,c.max_delta,c.shadow_cone,c.reflection_distance,
        c.reflection_cone,c.depth_rejection,c.history_weight,{c.light[0],c.light[1],c.light[2]}};
    auto* previous=impl_->previous_slot>=0?&impl_->frames[impl_->previous_slot]:nullptr;
    const bool history=!in.reset_history && in.frame_serial && in.frame_serial==impl_->previous_serial+1 &&
        in.previous_projection_valid && Matrix(in.previous_view_projection) && in.motion && previous &&
        previous->outputs.width==s.width && previous->outputs.height==s.height &&
        previous->history_settings==settings;
    c.flags|=(history?16u:0u)|(in.reactivity?32u:0u);
    std::memcpy(c.current_jitter,in.current_jitter,8);std::memcpy(c.previous_jitter,in.previous_jitter,8);
    auto constants=impl_->Resource((sizeof(c)+255)&~uint64_t(255),0,DXGI_FORMAT_UNKNOWN,true);
    void* mapped=nullptr;D3D12_RANGE no_read{0,0};Check(constants->Map(0,&no_read,&mapped),"Visible constants map failed");std::memcpy(mapped,&c,sizeof(c));constants->Unmap(0,nullptr);
    std::array<ID3D12Resource*,10> inputs{s.depth,s.color,in.motion,in.validity,raw_shadow.Get(),raw_reflection.Get(),history?previous->shadow.Get():nullptr,history?previous->reflection.Get():nullptr,history?previous->depth.Get():nullptr,in.reactivity};
    std::array<DXGI_FORMAT,10> formats{depth_format,color_format,motion_format,valid_format,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32_UINT};
    auto trace_heap=impl_->Heap(inputs,formats,{raw_shadow.Get(),raw_reflection.Get(),nullptr});
    auto counts=impl_->Resource(8,0,DXGI_FORMAT_UNKNOWN);
    auto zero=impl_->Resource(8,0,DXGI_FORMAT_UNKNOWN,true);void* zero_mapped=nullptr;
    Check(zero->Map(0,nullptr,&zero_mapped),"Visible DXR counter initialization failed");
    std::memset(zero_mapped,0,8);zero->Unmap(0,nullptr);
    Transition(list,counts.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyBufferRegion(counts.Get(),0,zero.Get(),0,8);
    Transition(list,counts.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    f.keep.push_back(counts);f.keep.push_back(zero);
    auto filter_heap=impl_->Heap(inputs,formats,{shadow.Get(),reflection.Get(),depth.Get()},counts.Get());
    // Retain every referenced resource before recording. This also retains the
    // previous frame's history through THIS frame's fence, not only its own.
    for(auto* input:inputs)if(input)f.keep.emplace_back(input);
    f.keep.push_back(raw_shadow);f.keep.push_back(raw_reflection);f.keep.push_back(shadow);f.keep.push_back(reflection);f.keep.push_back(depth);f.keep.push_back(constants);
    f.heaps.push_back(trace_heap);f.heaps.push_back(filter_heap);
    auto bind=[&](ID3D12DescriptorHeap* heap,ID3D12PipelineState* pso) {
      list->SetComputeRootSignature(impl_->trace_root.Get());list->SetPipelineState(pso);list->SetComputeRootConstantBufferView(0,constants->GetGPUVirtualAddress());
      list->SetComputeRootShaderResourceView(1,impl_->scene.AccelerationStructure()->GetGPUVirtualAddress());
      list->SetDescriptorHeaps(1,&heap);list->SetComputeRootDescriptorTable(2,heap->GetGPUDescriptorHandleForHeapStart());
    };
    bind(trace_heap.Get(),impl_->trace_pso.Get());list->Dispatch((tw+7)/8,(th+7)/8,1);
    for(auto* r:{raw_shadow.Get(),raw_reflection.Get()}) {Uav(list,r);Transition(list,r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
    bind(filter_heap.Get(),impl_->filter_pso.Get());list->Dispatch((s.width+7)/8,(s.height+7)/8,1);
    for(auto* r:{shadow.Get(),reflection.Get(),depth.Get()}) {Uav(list,r);Transition(list,r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
    f.shadow=shadow;f.reflection=reflection;f.depth=depth;f.outputs={shadow.Get(),reflection.Get(),s.width,s.height,history};f.traced=true;
    Uav(list,counts.Get());Transition(list,counts.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    f.outputs.counters=counts.Get();
    f.constants=constants;f.source_color=s.color;f.source_depth=s.depth;f.formats=formats;
    f.history_settings=settings;
    impl_->previous_slot=impl_->active;impl_->previous_serial=in.frame_serial;
  });
}
HRESULT VisibleRayScene::Composite(ID3D12GraphicsCommandList* list) {
  return impl_->Run([&] {
    auto& f=impl_->Current(list);Require(f.traced && !f.composited,"Composite requires one successful uncomposited Trace");
    const auto desc=f.source_color->GetDesc();
    auto color=impl_->Resource(desc.Width,desc.Height,desc.Format);
    std::array<ID3D12Resource*,10> inputs{f.source_depth.Get(),f.source_color.Get(),nullptr,nullptr,f.shadow.Get(),f.reflection.Get(),nullptr,nullptr,nullptr,nullptr};
    auto heap=impl_->Heap(inputs,f.formats,{nullptr,color.Get(),nullptr});
    f.keep.push_back(color);f.heaps.push_back(heap);
    list->SetComputeRootSignature(impl_->trace_root.Get());list->SetPipelineState(impl_->composite_pso.Get());
    list->SetComputeRootConstantBufferView(0,f.constants->GetGPUVirtualAddress());list->SetComputeRootShaderResourceView(1,impl_->scene.AccelerationStructure()->GetGPUVirtualAddress());
    auto* descriptors=heap.Get();list->SetDescriptorHeaps(1,&descriptors);list->SetComputeRootDescriptorTable(2,heap->GetGPUDescriptorHandleForHeapStart());
    list->Dispatch((uint32_t(desc.Width)+7)/8,(desc.Height+7)/8,1);Uav(list,color.Get());
    Transition(list,color.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    f.outputs.color=color.Get();f.composited=true;
  });
}
}
