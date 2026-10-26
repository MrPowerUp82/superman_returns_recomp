#include "temporal_motion.h"
#include "owner_VS.h"
#include "owner_PS.h"
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
constexpr UINT W=64,H=64;
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
struct Clip {float x,y,z,w;};
using Triangle=std::array<Clip,3>;
static Triangle Fullscreen() {return {{{-1,-1,.5f,1},{-1,3,.5f,1},{3,-1,.5f,1}}};}
static Triangle Previous(float dx,float dy) {
  auto t=Fullscreen();for(auto& v:t) {v.x+=2*dx/W;v.y-=2*dy/H;}return t;
}
static float Half(uint16_t h) {
  const float m=(h&0x7c00) ? std::ldexp(float(1024+(h&1023)),int((h>>10)&31)-25):std::ldexp(float(h&1023),-24);
  return (h&0x8000) ? -m:m;
}
static ComPtr<ID3D12Resource> Texture(GPU& gpu,D3D12_RESOURCE_DESC d,
                                    D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> result;
  Check(gpu.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&result)),"owner texture");return result;
}
struct Scene {
  Depth depth;
  ComPtr<ID3D12Resource> mask,clip,bg_previous,fg_previous;
  ComPtr<ID3D12DescriptorHeap> rtv;
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12PipelineState> pso,stencil_mark;
  Scene(GPU& gpu,uint32_t foreground_id=2,bool unowned=false,bool foreground_unmasked=false,bool stencil_fixture=false):depth(gpu) {
    clip=gpu.Upload(Fullscreen());bg_previous=gpu.Upload(Previous(2,-1));fg_previous=gpu.Upload(Previous(-3,2));
    auto desc=depth.texture->GetDesc();desc.Format=DXGI_FORMAT_R32_UINT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    mask=Texture(gpu,desc,D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    Check(gpu.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv)),"owner RTV");const auto target=rtv->GetCPUDescriptorHandleForHeapStart();
    gpu.device->CreateRenderTargetView(mask.Get(),nullptr,target);const float zero[4]{};gpu.list->ClearRenderTargetView(target,zero,0,nullptr);
    D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].Constants.Num32BitValues=2;
    parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rs{2,parameters,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> blob,error;
    Check(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"owner root serialize");
    Check(gpu.device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"owner root");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={kOwnerVS,sizeof(kOwnerVS)};pd.PS={kOwnerPS,sizeof(kOwnerPS)};
    pd.SampleMask=UINT_MAX;pd.SampleDesc.Count=1;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
    pd.DepthStencilState.DepthEnable=TRUE;pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;pd.DSVFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;
    if(stencil_fixture) {
      pd.DepthStencilState.StencilEnable=TRUE;pd.DepthStencilState.StencilReadMask=0xFF;
      pd.DepthStencilState.StencilWriteMask=0;
      pd.DepthStencilState.FrontFace=pd.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,
          D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_EQUAL};
      // Equal depth on BOTH halves: depth replay alone cannot recover stencil coverage.
      gpu.list->ClearDepthStencilView(depth.dsv,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,.5f,0,0,nullptr);
    }
    pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R32_UINT;
    auto& blend=pd.BlendState.RenderTarget[0];blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;
    blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
    blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    Check(gpu.device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pso)),"actual discard/owner PSO");
    gpu.list->SetGraphicsRootSignature(root.Get());gpu.list->SetPipelineState(pso.Get());
    gpu.list->SetGraphicsRootShaderResourceView(0,clip->GetGPUVirtualAddress());
    const D3D12_VIEWPORT vp{0,0,float(W),float(H),0,1};const D3D12_RECT sc{0,0,LONG(W),LONG(H)};
    gpu.list->RSSetViewports(1,&vp);gpu.list->RSSetScissorRects(1,&sc);
    gpu.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);gpu.list->OMSetRenderTargets(1,&target,FALSE,&depth.dsv);
    const uint32_t background[2]{1,0},foreground[2]{foreground_id,1};
    if(stencil_fixture) {
      auto mark=pd;mark.DepthStencilState.DepthEnable=FALSE;mark.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
      mark.DepthStencilState.StencilWriteMask=0xFF;
      mark.DepthStencilState.FrontFace=mark.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,
          D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_REPLACE,D3D12_COMPARISON_FUNC_ALWAYS};
      mark.BlendState.RenderTarget[0].RenderTargetWriteMask=0;
      Check(gpu.device->CreateGraphicsPipelineState(&mark,IID_PPV_ARGS(&stencil_mark)),"stencil mark PSO");
      const D3D12_RECT left{0,0,LONG(W/2),LONG(H)};gpu.list->OMSetStencilRef(1);
      gpu.list->SetPipelineState(stencil_mark.Get());gpu.list->RSSetScissorRects(1,&left);
      gpu.list->SetGraphicsRoot32BitConstants(1,2,background,0);gpu.list->DrawInstanced(3,1,0,0);
      gpu.list->SetPipelineState(pso.Get());gpu.list->RSSetScissorRects(1,&sc);
    }
    gpu.list->SetGraphicsRoot32BitConstants(1,2,background,0);gpu.list->DrawInstanced(3,1,0,0);
    // Unsupported writer explicitly unbinds the coverage MRT. Last audited
    // background ID remains; it is NOT a global nearest-writer identifier.
    if(foreground_unmasked) gpu.list->OMSetRenderTargets(0,nullptr,FALSE,&depth.dsv);
    gpu.list->SetGraphicsRoot32BitConstants(1,2,foreground,0);gpu.list->DrawInstanced(3,1,0,0);
    if(unowned) {const D3D12_RECT empty{0,0,8,LONG(H)};gpu.list->ClearRenderTargetView(target,zero,1,&empty);}
    if(stencil_fixture)
      gpu.list->ClearDepthStencilView(depth.dsv,D3D12_CLEAR_FLAG_STENCIL,1,2,0,nullptr);
    gpu.list->OMSetRenderTargets(0,nullptr,FALSE,nullptr);
    Transition(gpu.list.Get(),mask.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(gpu.list.Get(),depth.texture.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_DEPTH_READ);
  }
  TemporalMotionInputs Inputs() const {
    TemporalMotionInputs in{};in.depth=depth.texture.Get();in.width=W;in.height=H;in.coverage=mask.Get();return in;
  }
  std::array<MotionGeometry,2> Geometry(bool bg_unknown=false,bool fg_unknown=false,uint32_t foreground_id=2) const {
    std::array<MotionGeometry,2> g{};
    for(auto& draw:g) {draw.currentClip=clip.Get();draw.vertex_count=3;draw.correspondence_valid=true;}
    g[0].previousClip=bg_previous.Get();g[0].coverage_id=1;g[1].previousClip=fg_previous.Get();g[1].coverage_id=foreground_id;
    if(bg_unknown) {g[0].correspondence_valid=false;g[0].previousClip=nullptr;}
    if(fg_unknown) {g[1].correspondence_valid=false;g[1].previousClip=nullptr;}
    return g;
  }
};
static bool CoverageCase(GPU& gpu,TemporalMotion& motion,const char* name,bool bg_unknown,bool fg_unknown,
                         bool reverse=false,bool unowned=false,uint32_t foreground_id=2,bool no_mask=false,bool foreground_unmasked=false,
                         bool depth_control=false,bool stencil_fixture=false) {
  Check(motion.BeginFrame(0),"fenced coverage BeginFrame");Scene scene(gpu,foreground_id,unowned,foreground_unmasked,stencil_fixture);
  if(depth_control) {
    Transition(gpu.list.Get(),scene.depth.texture.Get(),D3D12_RESOURCE_STATE_DEPTH_READ,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    gpu.list->ClearDepthStencilView(scene.depth.dsv,D3D12_CLEAR_FLAG_DEPTH,.75f,0,0,nullptr);
    Transition(gpu.list.Get(),scene.depth.texture.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_DEPTH_READ);
  }
  auto in=scene.Inputs();auto geometry=scene.Geometry(bg_unknown,fg_unknown,foreground_id);
  if(foreground_unmasked) geometry[1].coverage_id=0;
  if(reverse) std::swap(geometry[0],geometry[1]);
  if(no_mask) {in.coverage=nullptr;for(auto& g:geometry) g.coverage_id=0;}
  Readback owner(gpu,scene.mask.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  Readback original_depth(gpu,scene.depth.texture.Get(),D3D12_RESOURCE_STATE_DEPTH_READ);
  Check(motion.Render(gpu.list.Get(),in,geometry),motion.LastError());const auto outputs=motion.Outputs();
  Readback velocity(gpu,outputs.velocity,D3D12_RESOURCE_STATE_RENDER_TARGET),validity(gpu,outputs.validity,D3D12_RESOURCE_STATE_RENDER_TARGET);
  Readback final_depth(gpu,scene.depth.texture.Get(),D3D12_RESOURCE_STATE_DEPTH_READ);
  // Descriptor alone is insufficient: the module MUST retain this actual resource until the fence.
  if(!no_mask) scene.mask.Reset();
  gpu.Submit();const auto ids=owner.Bytes(),v=velocity.Bytes(),m=validity.Bytes(),before=original_depth.Bytes(),after=final_depth.Bytes();
  UINT errors=0,background_pixels=0,foreground_pixels=0,invalid_pixels=0;
  for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
    uint32_t id=0,z=0,z_after=0;std::memcpy(&id,ids.data()+owner.layout.Offset+y*owner.layout.Footprint.RowPitch+x*4,4);
    std::memcpy(&z,before.data()+original_depth.layout.Offset+y*original_depth.layout.Footprint.RowPitch+x*4,4);
    std::memcpy(&z_after,after.data()+final_depth.layout.Offset+y*final_depth.layout.Footprint.RowPitch+x*4,4);
    const uint32_t expected_owner=stencil_fixture && x>=W/2 ? 0:foreground_unmasked ? 1:(unowned&&x<8 ? 0:(((x/4+y/4)&1) ? 1:foreground_id));
    const bool fg=no_mask||foreground_unmasked ? !reverse:id==foreground_id;
    const bool invalid=depth_control||(no_mask ? (fg ? fg_unknown:bg_unknown):(id==0||(fg ? fg_unknown:bg_unknown)));
    const float expected_x=invalid ? 0:(fg ? -3.f:2.f),expected_y=invalid ? 0:(fg ? 2.f:-1.f);
    uint16_t packed[2]{};std::memcpy(packed,v.data()+velocity.layout.Offset+y*velocity.layout.Footprint.RowPitch+x*4,4);
    const float dx=Half(packed[0]),dy=Half(packed[1]);const uint8_t valid=m[size_t(validity.layout.Offset)+y*validity.layout.Footprint.RowPitch+x];
    const bool expected_depth=depth_control ? std::abs(double(z&0xffffff)/16777215-.75)<1e-7:(z&0xffffff)==0x800000;
    const bool good=id==expected_owner&&expected_depth&&(z_after&0xffffff)==(z&0xffffff)&&
      valid==(invalid ? 0:1)&&(packed[0]&0x7c00)!=0x7c00&&(packed[1]&0x7c00)!=0x7c00&&
      std::abs(dx-expected_x)<.002f&&std::abs(dy-expected_y)<.002f&&(!invalid||(dx==0&&dy==0));
    if(!good&&errors++<3) std::cerr<<name<<" pixel "<<x<<','<<y<<" owner="<<id<<" got="<<dx<<','<<dy<<" valid="<<UINT(valid)<<'\n';
    background_pixels+=id==1;foreground_pixels+=id==foreground_id;invalid_pixels+=invalid;
  }
  std::cout<<(errors ? "FAIL ":"PASS ")<<name<<" background="<<background_pixels<<" foreground="<<foreground_pixels<<" invalid="<<invalid_pixels<<" errors="<<errors<<'\n';
  return errors==0;
}
static bool ConcurrentSlots(GPU& gpu,TemporalMotion& motion) {
  Check(motion.BeginFrame(0),"fenced slot0");Scene a(gpu,2);auto in_a=a.Inputs();auto ga=a.Geometry();
  Check(motion.Render(gpu.list.Get(),in_a,ga),"slot0 owner render");const auto oa=motion.Outputs();
  Readback va(gpu,oa.velocity,D3D12_RESOURCE_STATE_RENDER_TARGET),ma(gpu,oa.validity,D3D12_RESOURCE_STATE_RENDER_TARGET);
  a.mask.Reset();
  Check(motion.BeginFrame(1),"fenced slot1");Scene b(gpu,7);auto in_b=b.Inputs();auto gb=b.Geometry(false,false,7);
  Check(motion.Render(gpu.list.Get(),in_b,gb),"slot1 distinct owner render");const auto ob=motion.Outputs();
  Readback vb(gpu,ob.velocity,D3D12_RESOURCE_STATE_RENDER_TARGET),mb(gpu,ob.validity,D3D12_RESOURCE_STATE_RENDER_TARGET);
  b.mask.Reset();gpu.Submit();
  const auto av=va.Bytes(),am=ma.Bytes(),bv=vb.Bytes(),bm=mb.Bytes();UINT errors=0;
  for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
    const bool fg=((x/4+y/4)&1)==0;const float dx=fg ? -3.f:2.f,dy=fg ? 2.f:-1.f;
    uint16_t ah[2]{},bh[2]{};std::memcpy(ah,av.data()+va.layout.Offset+y*va.layout.Footprint.RowPitch+x*4,4);
    std::memcpy(bh,bv.data()+vb.layout.Offset+y*vb.layout.Footprint.RowPitch+x*4,4);
    const bool good=am[size_t(ma.layout.Offset)+y*ma.layout.Footprint.RowPitch+x]==1&&
      bm[size_t(mb.layout.Offset)+y*mb.layout.Footprint.RowPitch+x]==1&&
      std::abs(Half(ah[0])-dx)<.002f&&std::abs(Half(ah[1])-dy)<.002f&&
      std::abs(Half(bh[0])-dx)<.002f&&std::abs(Half(bh[1])-dy)<.002f;
    errors+=!good;
  }
  std::cout<<(errors ? "FAIL ":"PASS ")<<"two slots queued before fence, independent retained coverage descriptors: errors="<<errors<<'\n';
  return errors==0;
}
static bool ValidationCases(GPU& gpu,TemporalMotion& motion) {
  Scene scene(gpu);gpu.Submit();UINT errors=0,checked=0;
  auto run=[&](const char* name,TemporalMotionInputs in,std::array<MotionGeometry,2> geometry) {
    Check(motion.BeginFrame(1),"fenced validation BeginFrame");const HRESULT hr=motion.Render(gpu.list.Get(),in,geometry);
    const auto o=motion.Outputs();const bool rejected=FAILED(hr)&&!o.validity&&!o.velocity&&!o.width&&!o.height;
    if(!rejected) {std::cerr<<"FAIL rejection: "<<name<<'\n';++errors;}
    else {auto good=scene.Inputs();auto g=scene.Geometry();Check(motion.Render(gpu.list.Get(),good,g),"retry valid batch after rejected coverage input");}
    gpu.Submit();++checked;
  };
  auto in=scene.Inputs();auto geometry=scene.Geometry();in.coverage=nullptr;run("mask absent/nonzero IDs",in,geometry);
  geometry[0].coverage_id=0;run("mask absent/second positive ID validates full batch",in,geometry);
  const auto desc=scene.mask->GetDesc();
  for(UINT variant=0;variant<8;++variant) {
    auto bad=desc;bad.Flags=D3D12_RESOURCE_FLAG_NONE;
    if(variant==0) --bad.Width;if(variant==1) --bad.Height;if(variant==2) bad.Format=DXGI_FORMAT_R32_FLOAT;
    if(variant==3) bad.Format=DXGI_FORMAT_R32_TYPELESS;if(variant==4) bad.DepthOrArraySize=2;if(variant==5) bad.MipLevels=2;
    if(variant==6) {bad.SampleDesc.Count=4;bad.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;}
    if(variant==7) {bad.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE1D;bad.Height=1;}
    auto wrong=Texture(gpu,bad);in=scene.Inputs();in.coverage=wrong.Get();run("wrong coverage descriptor",in,scene.Geometry());
  }
  auto buffer=gpu.Buffer(1024,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  in=scene.Inputs();in.coverage=buffer.Get();run("coverage buffer",in,scene.Geometry());
  ComPtr<ID3D12Device> other;Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&other)),"second hardware device");
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> foreign;
  Check(other->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&foreign)),"foreign coverage texture");
  in=scene.Inputs();in.coverage=foreign.Get();run("foreign coverage device",in,scene.Geometry());
  std::cout<<(errors ? "FAIL ":"PASS ")<<"validation/retry: "<<checked<<" cases, "<<errors<<" errors\n";return errors==0;
}
int main() {
  try {
    GPU gpu;TemporalMotion motion;Check(motion.Initialize(gpu.device.Get()),"coverage motion initialize");UINT failures=0,cases=0;
    auto run=[&](const char* name,bool bg,bool fg,bool reverse=false,bool empty=false,uint32_t id=2,bool absent=false) {
      ++cases;if(!CoverageCase(gpu,motion,name,bg,fg,reverse,empty,id,absent)) ++failures;
    };
    run("checkerboard/two known motions",false,false);
    run("checkerboard/reversed replay",false,false,true);
    run("unknown foreground preserves background holes",false,true);
    run("unknown foreground/reversed replay",false,true,true);
    run("unknown background cannot erase foreground",true,false,true);
    run("unowned pixels remain invalid",false,false,false,true);
    run("full uint32 owner IDs",false,false,false,false,UINT32_MAX);
    ++cases;if(!CoverageCase(gpu,motion,"unmasked unknown invalidates despite retained audited ID",false,true,false,false,2,false,true)) ++failures;
    ++cases;if(!CoverageCase(gpu,motion,"unmasked known draw ignores retained audited ID",false,false,false,false,2,false,true)) ++failures;
    ++cases;if(!CoverageCase(gpu,motion,"matching owner cannot bypass depth EQUAL",false,false,false,false,2,false,false,true)) ++failures;
    ++cases;if(!CoverageCase(gpu,motion,"original stencil coverage with later stencil replacement",false,false,false,false,2,false,false,false,true)) ++failures;
    ++cases;if(!CoverageCase(gpu,motion,"unknown stencil writer invalidates only original passing fragments",false,true,false,false,2,false,false,false,true)) ++failures;
    run("null coverage legacy output",false,false,false,false,2,true);
    run("null coverage legacy unknown invalidation",false,true,false,false,2,true);
    ++cases;if(!ValidationCases(gpu,motion)) ++failures;
    ++cases;if(!ConcurrentSlots(gpu,motion)) ++failures;
    std::cout<<"RESULT: "<<cases-failures<<'/'<<cases<<" test groups passed\n";return failures ? 1:0;
  } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
