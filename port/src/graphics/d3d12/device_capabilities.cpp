#include "device_capabilities.h"

#include <rex/ui/d3d12/d3d12_api.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <rex/logging.h>
#include <string>

namespace superman_returns::graphics::d3d12 {
void LogDeviceCapabilities(ID3D12Device* device) {
  if (!device) {
    REXLOG_WARN("native D3D12 capabilities: device unavailable");
    return;
  }
  const LUID luid=device->GetAdapterLuid();
  Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  DXGI_ADAPTER_DESC1 desc{};
  HRESULT adapter_hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
  if (SUCCEEDED(adapter_hr)) adapter_hr=factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter));
  if (SUCCEEDED(adapter_hr)) adapter_hr=adapter->GetDesc1(&desc);
  std::string name="unknown";
  if (SUCCEEDED(adapter_hr)) {
    const int size=WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,nullptr,0,nullptr,nullptr);
    if (size>0) {
      name.resize(size);
      WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,name.data(),size,nullptr,nullptr);
      name.pop_back();
    }
  }
  REXLOG_INFO("native D3D12 adapter: name={} luid={:08X}{:08X} vendor={:04X} device={:04X} dedicated_mb={} query_hr={:08X}",
      name,uint32_t(luid.HighPart),luid.LowPart,desc.VendorId,desc.DeviceId,
      desc.DedicatedVideoMemory/1048576,uint32_t(adapter_hr));

  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
  const HRESULT options_hr=device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,&options,sizeof(options));
  Microsoft::WRL::ComPtr<ID3D12Device5> device5;
  const HRESULT device5_hr=device->QueryInterface(IID_PPV_ARGS(&device5));
  // Older runtimes reject an unknown requested model with E_INVALIDARG.
  // Report the ceiling as well: a successful query cannot prove a higher model.
  D3D12_FEATURE_DATA_SHADER_MODEL shader{};
  HRESULT shader_hr=E_INVALIDARG;
  for (uint32_t requested=0x69; requested>=0x60; --requested) {
    shader.HighestShaderModel=static_cast<D3D_SHADER_MODEL>(requested);
    shader_hr=device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&shader,sizeof(shader));
    if (shader_hr!=E_INVALIDARG) break;
  }
  const uint32_t model=SUCCEEDED(shader_hr)?uint32_t(shader.HighestShaderModel):0;
  REXLOG_INFO("native D3D12 capabilities: options5_hr={:08X} raytracing_tier={} device5_hr={:08X} shader_model={}.{} shader_hr={:08X} shader_query_ceiling=6.9",
      uint32_t(options_hr),SUCCEEDED(options_hr)?uint32_t(options.RaytracingTier):0,
      uint32_t(device5_hr),model>>4,model&15,uint32_t(shader_hr));
  REXLOG_INFO("native feature integration: RT/DLSS availability will be reported by the renderer after initialization");
}
}
