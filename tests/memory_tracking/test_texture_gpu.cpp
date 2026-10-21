#include "../native/test_main.h"
#include "../../port/src/native_renderer/resource_write_tracking.h"
#include "../../port/src/native_renderer/texture_decode.h"
#include "../../port/src/graphics/guest/texture_capture.h"
#include <rex/system/xmemory.h>
#include <rex/graphics/xenos.h>
#include <wrl/client.h>
#include <dxgi1_4.h>
#include <array>
#include <cstring>
#include <cstdio>
using Microsoft::WRL::ComPtr;
using namespace superman_returns;
#define GPU_CHECK(call) do { const HRESULT result=(call); if(FAILED(result)) { std::fprintf(stderr,"GPU failure: %s %08lx\n",#call,result); SR_CHECK(false); return; } } while(false)

SR_TEST(active_gpu_upload_contains_certified_snapshot) {
  ComPtr<ID3D12Debug> debug;
  const bool debug_available=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
  if(debug_available) debug->EnableDebugLayer();
  std::fprintf(stderr,"texture GPU fixture: debug layer %s\n",debug_available?"enabled":"unavailable");
  ComPtr<IDXGIFactory4> factory; GPU_CHECK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
  ComPtr<IDXGIAdapter> adapter; GPU_CHECK(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
  ComPtr<ID3D12Device> device; GPU_CHECK(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
  for(unsigned phase=0;phase<4;++phase) {
    rex::memory::Memory memory; SR_CHECK(memory.Initialize());
    auto* heap=memory.LookupHeapByType(true,65536);
    SR_CHECK(heap->AllocFixed(0xA4000000,65536,65536,
      rex::memory::kMemoryAllocationReserve|rex::memory::kMemoryAllocationCommit,
      rex::memory::kMemoryProtectRead|rex::memory::kMemoryProtectWrite));
    auto* live=memory.TranslatePhysical<uint8_t*>(0x04000000);
    std::memset(live,7,4096);
    native::ResourceWriteTracker tracker; SR_CHECK(tracker.Start(memory));
    const std::array ranges{RexTrackingRange{0x04000000,4096}};
    const auto watch=tracker.Watch(ranges); SR_CHECK(watch!=0);
    std::array<RexTrackingStamp,1> before{},after{};
    SR_CHECK(tracker.Sample(watch,before));
    rex::graphics::xenos::xe_gpu_texture_fetch_t f{};
    f.type=rex::graphics::xenos::FetchConstantType::kTexture;
    f.dimension=rex::graphics::xenos::DataDimension::k2DOrStacked;
    f.format=rex::graphics::xenos::TextureFormat::k_8_8_8_8;
    f.size_2d.width=f.size_2d.height=31;f.pitch=1;f.tiled=1;f.base_address=0x4000;
    f.swizzle=0|(1<<3)|(2<<6)|(3<<9);
    std::array<uint32_t,6> fetch{};std::memcpy(fetch.data(),&f,sizeof(f));
    std::shared_ptr<const graphics::guest::TextureCapture> snapshot;std::string error;
    SR_CHECK(graphics::guest::CaptureTexture(fetch,phase+1,[&](uint32_t,uint32_t n){return std::span<const uint8_t>(live,n);},snapshot,error));
    native::DecodedTexture decoded;SR_CHECK(native::DecodeTexture(*snapshot,decoded));
    if(phase==1) live[0]=9; // after owned decode, before upload
    D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=desc.Height=32;desc.DepthOrArraySize=desc.MipLevels=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    ComPtr<ID3D12Resource> texture;GPU_CHECK(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};UINT rows=0;UINT64 row_bytes=0,total=0;
    device->GetCopyableFootprints(&desc,0,1,0,&layout,&rows,&row_bytes,&total);
    desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=total;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    properties.Type=D3D12_HEAP_TYPE_UPLOAD;ComPtr<ID3D12Resource> upload;
    GPU_CHECK(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
    uint8_t* mapped=nullptr;GPU_CHECK(upload->Map(0,nullptr,reinterpret_cast<void**>(&mapped)));
    for(UINT row=0;row<rows;++row) std::memcpy(mapped+row*layout.Footprint.RowPitch,decoded.data.data()+row*128,128);
    upload->Unmap(0,nullptr);
    properties.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;
    GPU_CHECK(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    D3D12_COMMAND_QUEUE_DESC queue_desc{};ComPtr<ID3D12CommandQueue> queue;
    GPU_CHECK(device->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;GPU_CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;GPU_CHECK(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=layout;
    dst.pResource=texture.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={texture.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);
    src=dst;dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=layout;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);GPU_CHECK(list->Close());
    ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;GPU_CHECK(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));GPU_CHECK(queue->Signal(fence.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);SR_CHECK(event!=nullptr);
    GPU_CHECK(fence->SetEventOnCompletion(1,event));const auto waited=WaitForSingleObject(event,10000);CloseHandle(event);SR_CHECK_EQ(waited,DWORD(WAIT_OBJECT_0));if(waited!=WAIT_OBJECT_0)return;
    if(phase==2) live[0]=9; // after upload, before publication
    SR_CHECK(tracker.Sample(watch,after));
    native::TextureUploadProof proof;
    if(native::SameCoveredContent(before,after)) proof={watch,{before.begin(),before.end()},phase+1};
    SR_CHECK(native::CanReuseTexture(proof,after,phase+1)==(phase==0||phase==3));
    GPU_CHECK(readback->Map(0,nullptr,reinterpret_cast<void**>(&mapped)));
    bool pixels_match=true;for(UINT row=0;row<rows;++row) for(UINT x=0;x<128;++x) pixels_match&=mapped[row*layout.Footprint.RowPitch+x]==7;
    readback->Unmap(0,nullptr);SR_CHECK(pixels_match);
    if(phase==3) {live[0]=9;SR_CHECK(tracker.Sample(watch,after));SR_CHECK(!native::CanReuseTexture(proof,after,phase+1));}
    tracker.Unwatch(watch);
  }
}
