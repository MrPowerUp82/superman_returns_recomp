#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <fstream>
#include <vector>
#include <iterator>
#include "../../port/src/graphics/d3d12/shaders/post_vertex_world1_gs.h"
#include "../../port/src/graphics/d3d12/shaders/post_vertex_world2_gs.h"
#include "../../port/src/graphics/d3d12/shaders/post_vertex_world0_gs.h"
#include "../../port/src/native_renderer/vertex_signature.h"
using Microsoft::WRL::ComPtr;
int main(int argc,char** argv) {
  if(argc!=2) return 2;
  ComPtr<IDXGIFactory4> factory;
  if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return 3;
  ComPtr<ID3D12Device> device;
  for(UINT i=0;;++i) {
    ComPtr<IDXGIAdapter1> adapter;
    if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND) break;
    DXGI_ADAPTER_DESC1 d{}; adapter->GetDesc1(&d);
    if(d.VendorId==0x10de && SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)))) break;
  }
  if(!device) return 4;
  D3D12_DESCRIPTOR_RANGE ranges[5]{};
  D3D12_ROOT_PARAMETER params[8]{};
  for(UINT i=0;i<3;++i) {
    params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV; params[i].Descriptor={i,4};
    ranges[i]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,32768,0,i,0};
    params[3+i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3+i].DescriptorTable={1,&ranges[i]};
  }
  ranges[3]={D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,2048,0,3,0};
  ranges[4]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,32768,0,5,0};
  params[6].ParameterType=params[7].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[6].DescriptorTable={1,&ranges[3]};params[7].DescriptorTable={1,&ranges[4]};
  params[7].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
  D3D12_ROOT_SIGNATURE_DESC root_desc{8,params,0,nullptr,
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT};
  ComPtr<ID3DBlob> blob,error;
  if(FAILED(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))) return 5;
  ComPtr<ID3D12RootSignature> root;
  if(FAILED(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)))) return 6;
  const D3D12_SO_DECLARATION_ENTRY so{0,"TEXCOORD",15,0,3,0};
  const UINT stride=12;
  const char* names[]{"2C45BFFE76D77398","4F17209D364D5B51","EC4D2CD998957026","57A00D4EB3BF4C31","C7C0E381A3C9B79E"};
  for(int i=0;i<5;++i) {
    std::ifstream file(std::string(argv[1])+"/"+names[i]+".vs.dxil",std::ios::binary);
    std::vector<char> vs{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    if(vs.empty()) return 7;
    const std::vector<uint8_t> bytecode(vs.begin(),vs.end());
    const auto inputs=superman_returns::native::VertexShaderInputs(bytecode);
    const auto floats=superman_returns::native::VertexShaderInputs(bytecode,true);
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    for(const auto& input:inputs) {
      D3D12_INPUT_ELEMENT_DESC e{};
      e.SemanticName=input.first.c_str();e.SemanticIndex=input.second;
      e.Format=std::find(floats.begin(),floats.end(),input)!=floats.end() ?
          DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32G32B32A32_UINT;
      e.AlignedByteOffset=UINT(elements.size())*16;
      elements.push_back(e);
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature=root.Get(); d.VS={vs.data(),vs.size()};
    d.InputLayout={elements.data(),UINT(elements.size())};
    d.GS=i ? D3D12_SHADER_BYTECODE{kPostVertexWorld1GS,sizeof(kPostVertexWorld1GS)} :
             D3D12_SHADER_BYTECODE{kPostVertexWorld2GS,sizeof(kPostVertexWorld2GS)};
    if(i>=3) d.GS={kPostVertexWorld0GS,sizeof(kPostVertexWorld0GS)};
    d.StreamOutput={&so,1,&stride,1,D3D12_SO_NO_RASTERIZED_STREAM};
    d.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;d.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.SampleMask=UINT_MAX;d.SampleDesc.Count=1;
    d.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    d.DepthStencilState.FrontFace=d.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    ComPtr<ID3D12PipelineState> pso;
    const HRESULT hr=device->CreateGraphicsPipelineState(&d,IID_PPV_ARGS(&pso));
    std::printf("post-VS pipeline %s: %08X\n",names[i],unsigned(hr));
    if(FAILED(hr)) return 8;
  }
  std::puts("PASS: real static and skinned shader pipelines link with production extraction GS");
  return 0;
}
