// Regression: original translated perspective/strip raster depth must survive
// production SO capture -> production TemporalMotion's EQUAL depth comparison.
// Build outside the checkout; no translated HLSL substitute or regenerated headers.
#include "post_vertex_clip_capture.h"
#include "temporal_motion.h"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace superman_returns::graphics::d3d12;
constexpr UINT W=128,H=128;
static void Require(bool value,const char* why) {if(!value) throw std::runtime_error(why);}
static void Check(HRESULT hr,const char* why) {
  if(FAILED(hr)) {std::cerr<<why<<" HRESULT=0x"<<std::hex<<uint32_t(hr)<<std::dec<<'\n';throw std::runtime_error(why);}
}
static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,
                       D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}
struct GPU {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence;
  ComPtr<ID3D12InfoQueue> info;
  HANDLE event=nullptr;
  UINT64 serial=0;
  GPU() {
    ComPtr<ID3D12Debug> debug;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
      debug->EnableDebugLayer();ComPtr<ID3D12Debug1> validation;
      if(SUCCEEDED(debug.As(&validation))) {validation->SetEnableGPUBasedValidation(TRUE);std::cout<<"D3D12 debug layer + GPU validation enabled\n";}
      else std::cout<<"D3D12 debug layer enabled; GPU validation interface unavailable\n";
    } else std::cout<<"D3D12 debug layer unavailable; hardware/readback assertions remain mandatory\n";
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
    for(UINT i=0;;++i) {
      ComPtr<IDXGIAdapter1> adapter;
      const HRESULT hr=factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter));
      if(hr==DXGI_ERROR_NOT_FOUND) break;
      Check(hr,"enumerate hardware");DXGI_ADAPTER_DESC1 d{};Check(adapter->GetDesc1(&d),"adapter description");
      if(d.VendorId!=0x10de || (d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) ||
         std::wstring(d.Description).find(L"RTX 2060")==std::wstring::npos) continue;
      Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)),"RTX 2060 D3D12 device");
      std::wcout<<L"Hardware: "<<d.Description<<L'\n';break;
    }
    Require(device!=nullptr,"Actual RTX 2060 required; no WARP/no skips");device.As(&info);
    D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"command list");
    Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
    event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"fence event");
  }
  ~GPU() {if(event) CloseHandle(event);}
  void Submit() {
    Check(list->Close(),"close");ID3D12CommandList* commands[]{list.Get()};queue->ExecuteCommandLists(1,commands);
    Check(queue->Signal(fence.Get(),++serial),"signal");Check(fence->SetEventOnCompletion(serial,event),"fence completion");
    Require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"GPU fence timeout");Check(device->GetDeviceRemovedReason(),"device removed");
    bool clean=true;
    if(info) {
      for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
        SIZE_T size=0;Check(info->GetMessage(i,nullptr,&size),"debug message size");std::vector<char> bytes(size);
        auto* m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Check(info->GetMessage(i,m,&size),"debug message");
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) {std::cerr<<m->pDescription<<'\n';clean=false;}
      }
      info->ClearStoredMessages();
    }
    Require(clean,"D3D12 validation error");Check(allocator->Reset(),"fenced allocator reset");
    Check(list->Reset(allocator.Get(),nullptr),"fenced list reset");
  }
  ComPtr<ID3D12Resource> Buffer(UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;
    d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> r;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)),"buffer");return r;
  }
  template<class T> ComPtr<ID3D12Resource> Upload(const T& values) {
    auto r=Buffer(sizeof(values),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* data=nullptr;const D3D12_RANGE none{0,0};Check(r->Map(0,&none,&data),"immutable upload map");
    std::memcpy(data,&values,sizeof(values));r->Unmap(0,nullptr);return r;
  }
};
struct Depth {
  ComPtr<ID3D12Resource> texture;
  ComPtr<ID3D12DescriptorHeap> heap;
  D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
  explicit Depth(GPU& gpu) {
    D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=W;d.Height=H;d.DepthOrArraySize=d.MipLevels=1;
    d.Format=DXGI_FORMAT_R24G8_TYPELESS;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;clear.DepthStencil.Depth=1;
    Check(gpu.device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_DEPTH_WRITE,
          &clear,IID_PPV_ARGS(&texture)),"D24 depth");
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=1;
    Check(gpu.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"DSV heap");dsv=heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_DEPTH_STENCIL_VIEW_DESC vd{};vd.Format=clear.Format;vd.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
    gpu.device->CreateDepthStencilView(texture.Get(),&vd,dsv);
    gpu.list->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,1,0,0,nullptr);
  }
};
struct Readback {
  ComPtr<ID3D12Resource> buffer;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
  UINT64 size=0;
  Readback(GPU& gpu,ID3D12Resource* texture,D3D12_RESOURCE_STATES state) {
    const auto d=texture->GetDesc();gpu.device->GetCopyableFootprints(&d,0,1,0,&layout,nullptr,nullptr,&size);
    buffer=gpu.Buffer(size,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(gpu.list.Get(),texture,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=buffer.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint=layout;src.pResource=texture;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Transition(gpu.list.Get(),texture,D3D12_RESOURCE_STATE_COPY_SOURCE,state);
  }
  std::vector<uint8_t> Bytes() const {
    const D3D12_RANGE range{0,SIZE_T(size)};void* data=nullptr;Check(buffer->Map(0,&range,&data),"readback map");
    const auto* first=static_cast<const uint8_t*>(data);std::vector<uint8_t> result(first,first+size);
    const D3D12_RANGE none{0,0};buffer->Unmap(0,&none);return result;
  }
};
struct Position {float x,y,z;};
struct Clip {float x,y,z,w;};
struct CapturedConstants {
  std::filesystem::path path;
  std::array<float,1024> values{};
};
static std::vector<CapturedConstants> LoadCapturedConstants(const std::filesystem::path& repo) {
  std::vector<CapturedConstants> result;
  const std::array<const char*,6> folders{"motion_depth_before_clear_2510","motion_position_words_2510",
    "motion_input_evidence_2510","motion_candidate_geometry_2510","motion_color_lineage_2510","motion_families_rt_2510"};
  for(const auto* folder:folders) {
    const auto directory=repo/"logs"/folder;if(!std::filesystem::exists(directory)) continue;
    std::vector<std::filesystem::path> files;
    for(const auto& entry:std::filesystem::directory_iterator(directory))
      if(entry.path().filename().string().starts_with("motion_inputs_p2_mesh")&&entry.path().extension()==".bin") files.push_back(entry.path());
    std::sort(files.begin(),files.end());
    if(files.empty()) {std::cout<<"No captured VS banks in "<<directory.string()<<'\n';continue;}
    CapturedConstants captured;captured.path=files.front();std::ifstream input(captured.path,std::ios::binary);
    std::array<uint32_t,4> header{};input.read(reinterpret_cast<char*>(header.data()),sizeof(header));
    Require(input&&header[0]==0x4d564450&&header[1]==1,"captured constants header/schema");
    input.read(reinterpret_cast<char*>(captured.values.data()),sizeof(captured.values));Require(bool(input),"truncated captured VS bank");
    for(float value:captured.values) Require(std::isfinite(value),"nonfinite captured VS bank");
    Require(captured.values[16]>=6&&captured.values[17]==0,"captured first-instance remap must fit six generated vertices");
    std::cout<<"Captured bank: "<<captured.path.string()<<" c4="<<captured.values[16]<<','<<captured.values[17]<<'\n';
    result.push_back(captured);
  }
  Require(!result.empty(),"real captured VS constants are required; no silent synthetic fallback");return result;
}
// Fixture-only HLSL is embedded here; generated source/DXIL/disassembly stay in
// the standalone build. Never rewrite/recompile the original translated VS.
static std::vector<char> CompileFixture(const std::string& source,const char* entry,const char* profile) {
  wchar_t executable[32768]{};Require(GetModuleFileNameW(nullptr,executable,32768)!=0,"fixture executable path");
  const auto build=std::filesystem::path(executable).parent_path().parent_path();
  const auto repo=std::filesystem::absolute(__FILE__).parent_path().parent_path().parent_path().parent_path();
  const auto dxc=repo/".tools/vulkan-validation/Bin/dxc.exe";
  const auto stem=build/(std::string("embedded_fixture_")+entry);
  const auto hlsl=stem.string()+".hlsl",dxil=stem.string()+".dxil",listing=stem.string()+".ll";
  {std::ofstream output(hlsl,std::ios::binary);output<<source;Require(bool(output),"write build-local embedded HLSL");}
  const std::string ep(entry),target(profile);
  std::wstring command=L"\""+dxc.wstring()+L"\" -E "+std::wstring(ep.begin(),ep.end())+L" -T "+
    std::wstring(target.begin(),target.end())+L" -O3 -WX -Fo \""+std::filesystem::path(dxil).wstring()+
    L"\" -Fc \""+std::filesystem::path(listing).wstring()+L"\" \""+std::filesystem::path(hlsl).wstring()+L"\"";
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  Require(CreateProcessW(dxc.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,
          "launch bundled DXC for fixture-only shaders");
  const DWORD wait=WaitForSingleObject(process.hProcess,30000);DWORD exit_code=1;GetExitCodeProcess(process.hProcess,&exit_code);
  CloseHandle(process.hThread);CloseHandle(process.hProcess);
  Require(wait==WAIT_OBJECT_0&&exit_code==0,"compile build-local fixture shader");
  std::ifstream input(dxil,std::ios::binary);Require(bool(input),"compiled fixture DXIL");
  return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
static std::string VaryingDeclaration() {
  std::string s="struct V {float4 position:SV_Position;";
  for(UINT i=0;i<16;++i) s+="float4 t"+std::to_string(i)+":TEXCOORD"+std::to_string(i)+";";
  s+="float4 c0:COLOR0;float4 c1:COLOR1;float4 n0:NORMAL0;float4 n1:NORMAL1;};\n";return s;
}
static std::array<double,3> SolveWorld(const std::array<float,1024>& c,double x,double y,double w) {
  double a[3][4]{};const UINT rows[3]{0,1,3};const double rhs[3]{x,y,w};
  for(UINT r=0;r<3;++r) {for(UINT col=0;col<3;++col) a[r][col]=c[rows[r]*4+col];a[r][3]=rhs[r]-c[rows[r]*4+3];}
  for(UINT col=0;col<3;++col) {
    UINT pivot=col;for(UINT r=col+1;r<3;++r) if(std::abs(a[r][col])>std::abs(a[pivot][col])) pivot=r;
    Require(std::abs(a[pivot][col])>1e-12,"singular captured camera");
    for(UINT k=col;k<4;++k) std::swap(a[col][k],a[pivot][k]);
    const double scale=a[col][col];for(UINT k=col;k<4;++k) a[col][k]/=scale;
    for(UINT r=0;r<3;++r) if(r!=col) {const double f=a[r][col];for(UINT k=col;k<4;++k) a[r][k]-=f*a[col][k];}
  }
  return {a[0][3],a[1][3],a[2][3]};
}
static void GenerateCapturedScene(const std::array<float,1024>& c,std::array<Position,8>& positions,UINT seed) {
  const double rx=c[404],ry=c[405],rz=c[406],det=rx*rx-ry*rz;
  Require(std::isfinite(det)&&std::abs(det)>1e-8,"singular captured instance transform");
  constexpr double nx[5]{-.76137,-.76137,.72319,.72319,.94107};
  constexpr double ny[5]{-.72413,.77123,-.72413,.77123,-.08131};
  constexpr double depths[5]{573.317,1321.193,2901.713,4613.117,1831.971};
  for(UINT i=0;i<5;++i) {
    const double w=depths[i]*(1+.137*seed);const auto world=SolveWorld(c,nx[i]*w,ny[i]*w,w);
    const double x=world[0]-c[400],z=world[2]-c[402];
    positions[i]={float((rx*x-rz*z)/det),float(world[1]-c[401]),float((-ry*x+rx*z)/det)};
    Require(std::isfinite(positions[i].x)&&std::isfinite(positions[i].y)&&std::isfinite(positions[i].z),"generated local position");
  }
}
static Clip ExpectedCapturedClip(const Position& p,const std::array<float,1024>& c,float hx,float hy) {
  const double world[4]{double(c[404])*p.x+double(c[406])*p.z+c[400],double(p.y)+c[401],
                        double(c[405])*p.x+double(c[404])*p.z+c[402],1};
  double clip[4]{};for(UINT r=0;r<4;++r) for(UINT k=0;k<4;++k) clip[r]+=world[k]*c[r*4+k];
  return {float(clip[0]+hx*clip[3]),float(clip[1]+hy*clip[3]),float(clip[2]),float(clip[3])};
}
static float Half(uint16_t h) {
  const float m=(h&0x7c00) ? std::ldexp(float(1024+(h&1023)),int((h>>10)&31)-25) : std::ldexp(float(h&1023),-24);
  return (h&0x8000) ? -m : m;
}
enum class DepthTiming { Original, SnapshotBeforeClear, ClearedOriginal };
struct Case {
  std::string name;
  bool strip=false,half_pixel=false;
  MotionGeometry raster{};
  bool depth_controls=false;
  DepthTiming depth_timing=DepthTiming::Original;
  const CapturedConstants* captured=nullptr;
  bool near_one_depth=false;
  UINT seed=0;
};
struct Fixture {
  GPU& gpu;
  std::vector<char> original_vs;
  ComPtr<ID3D12RootSignature> root;
  PostVertexClipCapture capture;
  TemporalMotion motion;
  std::vector<char> varying_ps;
  explicit Fixture(GPU& g,const char* shader):gpu(g) {
    std::ifstream input(shader,std::ios::binary);Require(bool(input),"open actual translated DXIL");
    original_vs.assign(std::istreambuf_iterator<char>(input),{});Require(!original_vs.empty(),"empty original DXIL");
    std::cout<<"Original VS: 2C45BFFE76D77398.vs.dxil ("<<original_vs.size()<<" bytes), no HLSL rewrite\n";
    // NativeRoot bindings consumed by the shipped shader, with SO explicitly allowed.
    D3D12_ROOT_PARAMETER p[3]{};
    for(UINT i=0;i<2;++i) {p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;p[i].Descriptor={i*2,4};p[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;}
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,UINT_MAX,0,5,0};
    p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[2].DescriptorTable={1,&range};p[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC desc{3,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT};
    ComPtr<ID3DBlob> blob,error;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"native bindings serialize");
    Check(gpu.device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"native bindings root");
    Require(capture.Initialize(gpu.device.Get()),"production clip capture initialize");Check(motion.Initialize(gpu.device.Get()),"production motion initialize");
    std::string pixel=VaryingDeclaration()+"float4 Pixel(V v):SV_Target {float4 sum=v.c0+v.c1+v.n0+v.n1;";
    for(UINT i=0;i<16;++i) pixel+="sum+=v.t"+std::to_string(i)+";";
    varying_ps=CompileFixture(pixel+"return sum*.0000001+v.position;}","Pixel","ps_6_0");
  }
  bool Run(const Case& test) {
    // Unequal positive W. Camera is X=x,Y=y,Z=1.0001*z-.1,W=z.
    // Strip triangles include odd parity; the list includes opposite windings
    // so front/back culling both retain independently visible geometry.
    std::array<Position,8> positions{{
      {-.76f*.73f,-.72f*.73f,.73f},{-.76f*1.37f,.77f*1.37f,1.37f},
      {.72f*2.91f,-.72f*2.91f,2.91f},{.72f*4.63f,.77f*4.63f,4.63f},
      {.94f*1.83f,-.08f*1.83f,1.83f},{0,0,1},{0,0,1},{0,0,1}}};
    const std::array<uint32_t,8> remap=test.strip ? std::array<uint32_t,8>{0,1,2,3,4,0,0,0}
                                                   : std::array<uint32_t,8>{0,1,2,2,3,1,0,0};
    const UINT count=test.strip ? 5u:6u,expanded=test.strip ? 9u:6u;
    std::array<float,1024> constants{};
    constants[0]=constants[5]=1;constants[10]=1.0001f;constants[11]=-.1f;constants[14]=1;
    constants[16]=float(count);constants[101*4]=1; // c100=0, c101.xyz=(1,0,0): identity world.
    constants[229*4+2]=1; // Keep unrelated ramp math finite.
    if(test.captured) {
      constants=test.captured->values; // All 1024 captured words, including c4 and instance rows, unchanged.
      if(test.near_one_depth) {
        // Labelled stress variant: preserve captured X/Y/W rows and actual
        // instance rotation/translation; replace ONLY camera Z with Z=1.000001*W-.1.
        for(UINT k=0;k<4;++k) constants[8+k]=float(double(constants[12+k])*1.000001);
        constants[11]-=.1f;
      }
      GenerateCapturedScene(constants,positions,test.seed);
    }
    std::array<uint32_t,1024> shared{};
    auto scalar=[&](UINT word,float value){std::memcpy(&shared[word],&value,4);};
    scalar(108,1);scalar(109,1); // ScreenXform.xy=1, zw=0.
    const float hx=test.half_pixel ? 1.f/W:0,hy=test.half_pixel ? -1.f/H:0;
    scalar(73,hx);scalar(74,hy);
    // shader_common srVertexFetchImpl: {SRV, byteOffset, stride, XDK type}.
    shared[128]=0;shared[130]=12;shared[131]=0x2A23B9; // raw float3
    shared[132]=1;shared[134]=4;shared[135]=0x2C82A1; // numeric uint (packed=false)
    auto vb=gpu.Upload(positions),indices=gpu.Upload(remap),cb=gpu.Upload(constants),sb=gpu.Upload(shared);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors=2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;ComPtr<ID3D12DescriptorHeap> srv;
    Check(gpu.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv)),"bindless raw SRV heap");
    auto handle=srv->GetCPUDescriptorHandleForHeapStart();
    const UINT increment=gpu.device->GetDescriptorHandleIncrementSize(hd.Type);
    for(auto* buffer:{vb.Get(),indices.Get()}) {
      D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R32_TYPELESS;
      sd.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      sd.Buffer.NumElements=UINT(buffer->GetDesc().Width/4);sd.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
      gpu.device->CreateShaderResourceView(buffer,&sd,handle);handle.ptr+=increment;
    }
    Depth depth(gpu);
    ComPtr<ID3D12Resource> color;
    ComPtr<ID3D12DescriptorHeap> color_heap;
    D3D12_CPU_DESCRIPTOR_HANDLE color_rtv{};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={original_vs.data(),original_vs.size()};
    pd.SampleMask=UINT_MAX;pd.SampleDesc.Count=1;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    auto& rs=pd.RasterizerState;rs.FillMode=D3D12_FILL_MODE_SOLID;rs.CullMode=test.raster.cull_mode;
    rs.FrontCounterClockwise=test.raster.front_counter_clockwise;rs.DepthClipEnable=TRUE;
    rs.DepthBias=test.raster.depth_bias;rs.SlopeScaledDepthBias=test.raster.slope_scaled_depth_bias;rs.DepthBiasClamp=test.raster.depth_bias_clamp;
    pd.DepthStencilState.DepthEnable=TRUE;pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS;pd.DSVFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;
    if(test.captured) {
      D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_DEFAULT;auto desc=depth.texture->GetDesc();
      desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      Check(gpu.device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,
            nullptr,IID_PPV_ARGS(&color)),"original varying consumer target");
      D3D12_DESCRIPTOR_HEAP_DESC rh{};rh.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;rh.NumDescriptors=1;
      Check(gpu.device->CreateDescriptorHeap(&rh,IID_PPV_ARGS(&color_heap)),"original RTV heap");
      color_rtv=color_heap->GetCPUDescriptorHandleForHeapStart();gpu.device->CreateRenderTargetView(color.Get(),nullptr,color_rtv);
      pd.PS={varying_ps.data(),varying_ps.size()};pd.NumRenderTargets=1;pd.RTVFormats[0]=desc.Format;
      auto& blend=pd.BlendState.RenderTarget[0];blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;
      blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
      blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    ComPtr<ID3D12PipelineState> original_pso,so_pso;
    Check(gpu.device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&original_pso)),"actual translated VS raster PSO");
    gpu.list->SetGraphicsRootSignature(root.Get());gpu.list->SetPipelineState(original_pso.Get());
    ID3D12DescriptorHeap* heaps[]{srv.Get()};gpu.list->SetDescriptorHeaps(1,heaps);
    gpu.list->SetGraphicsRootConstantBufferView(0,cb->GetGPUVirtualAddress());gpu.list->SetGraphicsRootConstantBufferView(1,sb->GetGPUVirtualAddress());
    gpu.list->SetGraphicsRootDescriptorTable(2,srv->GetGPUDescriptorHandleForHeapStart());
    const D3D12_VIEWPORT vp{0,0,float(W),float(H),0,1};const D3D12_RECT sc{0,0,LONG(W),LONG(H)};
    gpu.list->RSSetViewports(1,&vp);gpu.list->RSSetScissorRects(1,&sc);
    gpu.list->IASetPrimitiveTopology(test.strip ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    gpu.list->OMSetRenderTargets(test.captured ? 1u:0u,test.captured ? &color_rtv:nullptr,FALSE,&depth.dsv);gpu.list->DrawInstanced(count,1,0,0);
    std::optional<Depth> direct_depth;
    std::optional<Readback> direct_depth_readback;
    std::optional<PostVertexClipCapture::Range> direct_range;
    ComPtr<ID3D12PipelineState> direct_pso;
    ComPtr<ID3D12QueryHeap> so_queries;
    ComPtr<ID3D12Resource> so_statistics;
    if(test.captured) {
      // Instrument the VS->PS link without inserting a GS. SO itself can alter
      // driver specialization, so independently compare this draw's D24 depth
      // to the uninstrumented raster above; never label these floats an
      // observation from that uninstrumented draw without that limitation.
      direct_depth.emplace(gpu);Require(capture.Begin(gpu.list.Get(),1),"direct VS capture begin");
      direct_range=capture.Reserve(expanded);Require(direct_range.has_value(),"direct VS capture range");
      const D3D12_SO_DECLARATION_ENTRY position{0,"SV_POSITION",0,0,4,0};const UINT clip_stride=16;
      auto direct_pd=pd;direct_pd.StreamOutput={&position,1,&clip_stride,1,0};
      Check(gpu.device->CreateGraphicsPipelineState(&direct_pd,IID_PPV_ARGS(&direct_pso)),"rasterizing VS->PS with direct SV_Position SO");
      D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_SO_STATISTICS;query.Count=1;
      Check(gpu.device->CreateQueryHeap(&query,IID_PPV_ARGS(&so_queries)),"direct VS SO statistics");
      so_statistics=gpu.Buffer(sizeof(D3D12_QUERY_DATA_SO_STATISTICS),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
      gpu.list->SetPipelineState(direct_pso.Get());gpu.list->OMSetRenderTargets(1,&color_rtv,FALSE,&direct_depth->dsv);
      gpu.list->SOSetTargets(0,1,&direct_range->view);
      gpu.list->BeginQuery(so_queries.Get(),D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0,0);
      gpu.list->DrawInstanced(count,1,0,0);
      gpu.list->EndQuery(so_queries.Get(),D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0,0);
      capture.Finish(gpu.list.Get());
      gpu.list->ResolveQueryData(so_queries.Get(),D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0,0,1,so_statistics.Get(),0);
      direct_depth_readback.emplace(gpu,direct_depth->texture.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    // Second draw uses the very same immutable shader inputs and original VS.
    Require(capture.Begin(gpu.list.Get(),0),"production capture begin");const auto range=capture.Reserve(expanded);
    Require(range.has_value(),"reserve expanded triangles");
    const D3D12_SO_DECLARATION_ENTRY declaration{0,"TEXCOORD",15,0,4,0};const UINT stride=16;
    pd.GS=PostVertexClipCapture::GeometryShader();pd.StreamOutput={&declaration,1,&stride,1,D3D12_SO_NO_RASTERIZED_STREAM};
    pd.PS={};pd.NumRenderTargets=0;pd.RTVFormats[0]=DXGI_FORMAT_UNKNOWN;
    pd.DepthStencilState.DepthEnable=FALSE;pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;pd.DSVFormat=DXGI_FORMAT_UNKNOWN;
    Check(gpu.device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&so_pso)),"production SO PSO with actual translated VS");
    gpu.list->OMSetRenderTargets(0,nullptr,FALSE,nullptr);gpu.list->SetPipelineState(so_pso.Get());
    gpu.list->SOSetTargets(0,1,&range->view);gpu.list->DrawInstanced(count,1,0,0);capture.Finish(gpu.list.Get());
    Require(capture.dropped()==0,"capture dropped draw");
    Readback original(gpu,depth.texture.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE);
    // These two controls record identical GPU copy/clear operations and differ
    // only in which depth resource TemporalMotion consumes. Keep the private
    // snapshot and both readbacks alive through Submit's fence below.
    ComPtr<ID3D12Resource> snapshot;
    std::optional<Readback> saved_depth,cleared_depth;
    if(test.depth_timing!=DepthTiming::Original) {
      D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_DEFAULT;
      const auto desc=depth.texture->GetDesc();
      Check(gpu.device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&snapshot)),"private matching depth snapshot in COPY_DEST");
      Transition(gpu.list.Get(),depth.texture.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE);
      gpu.list->CopyResource(snapshot.Get(),depth.texture.Get()); // Full depth+stencil resource, no CPU reconstruction.
      Transition(gpu.list.Get(),snapshot.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_DEPTH_READ);
      Transition(gpu.list.Get(),depth.texture.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
      gpu.list->ClearDepthStencilView(depth.dsv,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,1,0,0,nullptr);
      saved_depth.emplace(gpu,snapshot.Get(),D3D12_RESOURCE_STATE_DEPTH_READ);
      cleared_depth.emplace(gpu,depth.texture.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    if(test.depth_controls) {
      // Both planes differ from every original triangle depth. The farther one
      // specifically catches LESS_EQUAL; the nearer one catches GREATER_EQUAL.
      const D3D12_RECT far_rect{0,0,LONG(W/2),LONG(H/2)},near_rect{0,LONG(H/2),LONG(W/2),LONG(H)};
      gpu.list->ClearDepthStencilView(depth.dsv,D3D12_CLEAR_FLAG_DEPTH,.9999f,0,1,&far_rect);
      gpu.list->ClearDepthStencilView(depth.dsv,D3D12_CLEAR_FLAG_DEPTH,.1f,0,1,&near_rect);
    }
    Transition(gpu.list.Get(),depth.texture.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_DEPTH_READ);
    Check(motion.BeginFrame(0),"fenced production motion BeginFrame");MotionGeometry geometry=test.raster;
    geometry.currentClip=geometry.previousClip=capture.Buffer(0);geometry.current_offset=geometry.previous_offset=range->offset;
    geometry.vertex_count=expanded;geometry.correspondence_valid=true;
    TemporalMotionInputs mi{};
    mi.depth=test.depth_timing==DepthTiming::SnapshotBeforeClear ? snapshot.Get():depth.texture.Get();
    mi.width=W;mi.height=H;
    Check(motion.Render(gpu.list.Get(),mi,std::span<const MotionGeometry>(&geometry,1)),motion.LastError());
    const auto outputs=motion.Outputs();Require(outputs.validity && outputs.velocity,"production motion outputs missing");
    Readback validity(gpu,outputs.validity,D3D12_RESOURCE_STATE_RENDER_TARGET),velocity(gpu,outputs.velocity,D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto clip_readback=gpu.Buffer(UINT64(expanded)*16,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(gpu.list.Get(),capture.Buffer(0),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.list->CopyBufferRegion(clip_readback.Get(),0,capture.Buffer(0),range->offset,UINT64(expanded)*16);
    Transition(gpu.list.Get(),capture.Buffer(0),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ComPtr<ID3D12Resource> direct_clips_readback;
    if(direct_range) {
      direct_clips_readback=gpu.Buffer(UINT64(expanded)*16,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
      Transition(gpu.list.Get(),capture.Buffer(1),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
      gpu.list->CopyBufferRegion(direct_clips_readback.Get(),0,capture.Buffer(1),direct_range->offset,UINT64(expanded)*16);
      Transition(gpu.list.Get(),capture.Buffer(1),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    gpu.Submit(); // ALL uploads, descriptors, PSOs and modules outlive this fence.
    const auto db=original.Bytes(),mb=validity.Bytes(),vb_bytes=velocity.Bytes();
    const auto snapshot_bytes=saved_depth ? saved_depth->Bytes():std::vector<uint8_t>{};
    const auto cleared_bytes=cleared_depth ? cleared_depth->Bytes():std::vector<uint8_t>{};
    const auto direct_depth_bytes=direct_depth_readback ? direct_depth_readback->Bytes():std::vector<uint8_t>{};
    UINT covered=0,expected_valid=0,valid=0,missing=0,extra=0,bad_motion=0,far_rejected=0,near_rejected=0;
    UINT snapshot_mismatches=0,clear_mismatches=0,original_pixels_valid=0;
    UINT direct_depth_mismatches=0;
    uint32_t min_depth24=0xffffff,max_depth24=0;
    float max_motion=0;
    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
      uint32_t z=0;std::memcpy(&z,db.data()+original.layout.Offset+y*original.layout.Footprint.RowPitch+x*4,4);
      const bool written=(z&0xffffff)!=0xffffff;
      if(written) {min_depth24=std::min(min_depth24,z&0xffffff);max_depth24=std::max(max_depth24,z&0xffffff);}
      if(direct_depth_readback) {
        uint32_t direct=0;std::memcpy(&direct,direct_depth_bytes.data()+direct_depth_readback->layout.Offset+
          y*direct_depth_readback->layout.Footprint.RowPitch+x*4,4);
        direct_depth_mismatches+=(direct&0xffffff)!=(z&0xffffff);
      }
      if(saved_depth) {
        uint32_t copied=0,cleared=0;
        std::memcpy(&copied,snapshot_bytes.data()+saved_depth->layout.Offset+y*saved_depth->layout.Footprint.RowPitch+x*4,4);
        std::memcpy(&cleared,cleared_bytes.data()+cleared_depth->layout.Offset+y*cleared_depth->layout.Footprint.RowPitch+x*4,4);
        snapshot_mismatches+=(copied&0xffffff)!=(z&0xffffff);
        clear_mismatches+=(cleared&0xffffff)!=0xffffff;
      }
      const bool blocked=test.depth_controls&&x<W/2;
      const bool expected=written&&!blocked&&test.depth_timing!=DepthTiming::ClearedOriginal;
      if(written&&blocked) {if(y<H/2) ++far_rejected;else ++near_rejected;}
      const uint8_t mask=mb[size_t(validity.layout.Offset)+y*validity.layout.Footprint.RowPitch+x];
      original_pixels_valid+=written&&mask==1;
      uint16_t v[2]{};std::memcpy(v,vb_bytes.data()+velocity.layout.Offset+y*velocity.layout.Footprint.RowPitch+x*4,4);
      const float dx=Half(v[0]),dy=Half(v[1]);covered+=written;expected_valid+=expected;valid+=mask==1;
      missing+=expected&&mask!=1;extra+=!expected&&mask!=0;
      // Raster interpolation has subpixel precision. Bound the residual to
      // .002 render pixels, while coverage/depth EQUAL have NO tolerance.
      if(!std::isfinite(dx)||!std::isfinite(dy)||std::abs(dx)>.002f||std::abs(dy)>.002f||mask>1||
         (mask==0&&(dx!=0||dy!=0))) ++bad_motion;
      max_motion=std::max({max_motion,std::abs(dx),std::abs(dy)});
      if(expected&&mask!=1&&missing<=4) std::cerr<<test.name<<" missing ("<<x<<','<<y<<") depth24=0x"<<std::hex<<(z&0xffffff)<<std::dec<<'\n';
    }
    void* clips=nullptr;const D3D12_RANGE read{0,SIZE_T(expanded)*16};Check(clip_readback->Map(0,&read,&clips),"captured clips readback");
    const auto* actual=static_cast<const Clip*>(clips);bool clip_ok=true;
    float min_clip_ndc=std::numeric_limits<float>::infinity(),max_clip_ndc=-std::numeric_limits<float>::infinity();
    for(UINT i=0;i<expanded;++i) {
      min_clip_ndc=std::min(min_clip_ndc,actual[i].z/actual[i].w);
      max_clip_ndc=std::max(max_clip_ndc,actual[i].z/actual[i].w);
    }
    UINT stage_bit_mismatches=0;std::array<float,4> max_stage_delta{};
    if(direct_clips_readback) {
      void* direct_data=nullptr;Check(direct_clips_readback->Map(0,&read,&direct_data),"direct VS clip float readback");
      const auto* direct=static_cast<const Clip*>(direct_data);
      wchar_t executable[32768]{};Require(GetModuleFileNameW(nullptr,executable,32768)!=0,"stage artifact output path");
      const auto directory=std::filesystem::path(executable).parent_path().parent_path()/"stage_clips";
      std::filesystem::create_directories(directory);auto name=test.name;std::replace(name.begin(),name.end(),'/','_');
      const uint32_t header[4]{0x434c4950,1,expanded,sizeof(Clip)};
      for(UINT stage=0;stage<2;++stage) {
        const auto path=directory/(name+(stage ? "_direct_vs_so.bin":"_production_gs.bin"));
        std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(header),sizeof(header));
        output.write(reinterpret_cast<const char*>(stage ? direct:actual),std::streamsize(expanded)*sizeof(Clip));
        Require(bool(output),"save build-local captured stage float4s");
      }
      for(UINT t=0;t<expanded/3;++t) {
        UINT rotation=0;double best=std::numeric_limits<double>::infinity();
        for(UINT r=0;r<3;++r) {
          double distance=0;for(UINT v=0;v<3;++v) {
            const auto& a=actual[t*3+v];const auto& b=direct[t*3+(v+r)%3];
            distance+=std::abs(double(a.x)-b.x)+std::abs(double(a.y)-b.y)+std::abs(double(a.z)-b.z)+std::abs(double(a.w)-b.w);
          }
          if(distance<best) {best=distance;rotation=r;}
        }
        for(UINT v=0;v<3;++v) {
          const auto& a=actual[t*3+v];const auto& b=direct[t*3+(v+rotation)%3];
          const float av[4]{a.x,a.y,a.z,a.w},bv[4]{b.x,b.y,b.z,b.w};
          for(UINT axis=0;axis<4;++axis) {
            uint32_t a_bits=0,b_bits=0;std::memcpy(&a_bits,&av[axis],4);std::memcpy(&b_bits,&bv[axis],4);
            stage_bit_mismatches+=a_bits!=b_bits;max_stage_delta[axis]=std::max(max_stage_delta[axis],std::abs(av[axis]-bv[axis]));
            if(a_bits!=b_bits&&stage_bit_mismatches<=4) std::cerr<<test.name<<" stage clip t="<<t<<" v="<<v<<" axis="<<axis
              <<" GS=0x"<<std::hex<<a_bits<<" directVS=0x"<<b_bits<<std::dec<<'\n';
          }
        }
      }
      const D3D12_RANGE none{0,0};direct_clips_readback->Unmap(0,&none);
      const D3D12_RANGE stats_range{0,sizeof(D3D12_QUERY_DATA_SO_STATISTICS)};void* stats_data=nullptr;
      Check(so_statistics->Map(0,&stats_range,&stats_data),"SO statistics readback");D3D12_QUERY_DATA_SO_STATISTICS stats{};
      std::memcpy(&stats,stats_data,sizeof(stats));so_statistics->Unmap(0,&none);
      Require(stats.NumPrimitivesWritten==expanded/3&&stats.PrimitivesStorageNeeded==expanded/3,"direct VS SO expanded primitive count/overflow");
    }
    for(UINT triangle=0;triangle<expanded/3;++triangle) {
      std::array<UINT,3> order=test.strip ? std::array<UINT,3>{triangle,triangle+1,triangle+2}
                                        : std::array<UINT,3>{triangle*3,triangle*3+1,triangle*3+2};
      if(test.strip&&(triangle&1)) std::swap(order[0],order[1]);
      bool match=false;
      // GS strip assembly may cyclically rotate a triangle; never accept a winding reversal.
      for(UINT rotation=0;rotation<3;++rotation) {
        bool equal=true;
        for(UINT v=0;v<3;++v) {
          const auto& p=positions[remap[order[(v+rotation)%3]]];
          const Clip expected=test.captured ? ExpectedCapturedClip(p,constants,hx,hy):Clip{p.x+hx*p.z,p.y+hy*p.z,1.0001f*p.z-.1f,p.z};
          const auto& a=actual[triangle*3+v];
          const float epsilon=test.captured ? .01f:2e-6f; // Only a source-input sanity check. GPU depth EQUAL remains exact.
          equal=equal&&std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z)&&std::isfinite(a.w)&&
            std::abs(a.x-expected.x)<epsilon&&std::abs(a.y-expected.y)<epsilon&&std::abs(a.z-expected.z)<epsilon&&std::abs(a.w-expected.w)<epsilon;
        }
        match=match||equal;
      }
      clip_ok=clip_ok&&match;
      if(!match) for(UINT v=0;v<3;++v) {const auto& a=actual[triangle*3+v];std::cerr<<"captured triangle "<<triangle<<" vertex "<<v<<": "<<a.x<<','<<a.y<<','<<a.z<<','<<a.w<<'\n';}
    }
    const D3D12_RANGE none{0,0};clip_readback->Unmap(0,&none);
    const bool controls_ok=!test.depth_controls||(far_rejected>100&&near_rejected>100);
    const bool coverage_ok=test.depth_timing==DepthTiming::ClearedOriginal ? expected_valid==0&&valid==0:expected_valid>100;
    const bool pass=covered>100&&coverage_ok&&missing==0&&extra==0&&bad_motion==0&&clip_ok&&controls_ok&&
                    snapshot_mismatches==0&&clear_mismatches==0&&direct_depth_mismatches==0;
    std::cout<<(pass ? "PASS ":"FAIL ")<<test.name<<": depth="<<covered<<" expectedValid="<<expected_valid<<" valid="<<valid<<" missing="<<missing
             <<" extra="<<extra<<" badMV="<<bad_motion<<" maxMV="<<max_motion<<" clips="<<(clip_ok ? "OK":"BAD")<<'\n';
    if(test.depth_controls) std::cout<<"  EQUAL negative controls: farther="<<far_rejected<<" nearer="<<near_rejected<<" originally covered pixels rejected\n";
    if(saved_depth) std::cout<<"  GPU copy-before-clear: snapshotDepthMismatches="<<snapshot_mismatches
      <<" originalClearMismatches="<<clear_mismatches<<" originalGeometryPixelsValid="<<original_pixels_valid<<'/'<<covered<<'\n';
    if(direct_range) std::cout<<"  Stage diagnostic (direct VS SO instrumentation): depthDeltaPixels="<<direct_depth_mismatches
      <<" clipBitMismatches="<<stage_bit_mismatches<<" maxAbsXYZW="<<max_stage_delta[0]<<','<<max_stage_delta[1]<<','
      <<max_stage_delta[2]<<','<<max_stage_delta[3]<<'\n';
    if(test.captured) std::cout<<"  Depth domain: "<<(test.near_one_depth ? "SYNTHETIC near-one Z row":"UNCHANGED captured reversed-Z rows")
      <<" clipNdcZ=["<<min_clip_ndc<<','<<max_clip_ndc<<"] rasterDepth24=["<<min_depth24<<','<<max_depth24<<"]\n";
    return pass;
  }
};
int main(int argc,char** argv) {
  try {
    Require(argc==2,"Usage: clip_motion_replay_smoke <actual 2C45BFFE76D77398.vs.dxil>");GPU gpu;Fixture fixture(gpu,argv[1]);
    std::vector<Case> cases{{"list/perspective"},{"list/perspective/half-pixel",false,true},
                            {"strip5/odd-parity/perspective",true},{"strip5/odd-parity/half-pixel",true,true}};
    for(const auto cull:{D3D12_CULL_MODE_FRONT,D3D12_CULL_MODE_BACK}) for(const bool ccw:{false,true}) {
      Case c{cull==D3D12_CULL_MODE_FRONT ? (ccw ? "list/front/CCW":"list/front/CW") : (ccw ? "list/back/CCW":"list/back/CW")};
      c.raster.cull_mode=cull;c.raster.front_counter_clockwise=ccw;cases.push_back(c);
    }
    Case bias{"list/constant+slope-bias",false,true};bias.raster.depth_bias=17;bias.raster.slope_scaled_depth_bias=.125f;bias.raster.depth_bias_clamp=.01f;cases.push_back(bias);
    Case strip_bias{"strip5/constant+slope-bias",true,true};strip_bias.raster=bias.raster;cases.push_back(strip_bias);
    Case controls{"list/EQUAL-rejects-nearer-and-farther-depth"};controls.depth_controls=true;cases.push_back(controls);
    Case snapshot{"list/private-snapshot-survives-original-clear"};snapshot.depth_timing=DepthTiming::SnapshotBeforeClear;cases.push_back(snapshot);
    Case late{"list/late-cleared-original-loses-correspondence"};late.depth_timing=DepthTiming::ClearedOriginal;cases.push_back(late);
    const auto repo=std::filesystem::absolute(argv[1]).parent_path().parent_path().parent_path().parent_path();
    const auto captured=LoadCapturedConstants(repo);
    for(const auto& bank:captured) for(bool city:{false,true}) for(bool strip:{false,true}) for(UINT seed=0;seed<3;++seed) {
      Case real{"captured/"+bank.path.parent_path().filename().string()+"/"+(city ? "near-one-Z/":"original-Z/")+
        (strip ? "strip5/":"list/")+std::to_string(seed)};
      real.captured=&bank;real.near_one_depth=city;real.strip=strip;real.half_pixel=seed!=0;real.seed=seed;cases.push_back(real);
    }
    UINT failures=0;for(const auto& c:cases) if(!fixture.Run(c)) ++failures;
    std::cout<<"RESULT: "<<cases.size()-failures<<'/'<<cases.size()<<" cases passed; EQUAL unchanged\n";
    return failures ? 1:0;
  } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
