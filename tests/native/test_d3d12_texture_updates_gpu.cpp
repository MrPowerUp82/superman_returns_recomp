// Standalone D3D12 readback test. Link main.cpp, d3d12.lib and dxgi.lib.
// Uses the production exact-byte/fence helper; does not instantiate Renderer.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "test_main.h"
#include "../../port/src/native_renderer/d3d12_texture_updates.h"
#include <array>
#include <algorithm>

using Microsoft::WRL::ComPtr;
using namespace superman_returns::native;
#define CHECK_HR(expr) do { const HRESULT hr=(expr); if(FAILED(hr)) { \
  std::printf("HRESULT %08lx: %s\n",hr,#expr); SR_CHECK(false); return; } } while(false)

SR_TEST(d3d12_texture_updates_readback_pending_cow_and_completed_reuse) {
  ComPtr<ID3D12Debug> debug;
  if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
  ComPtr<ID3D12Device> device;
  CHECK_HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
  ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC qd{};
  CHECK_HR(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
  ComPtr<ID3D12CommandAllocator> allocator;
  CHECK_HR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
  ComPtr<ID3D12GraphicsCommandList> list;
  CHECK_HR(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
  ComPtr<ID3D12Fence> fence,gate;
  CHECK_HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
  CHECK_HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
  auto wait=[&](uint64_t value) {
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!event) return false;
    const bool armed=SUCCEEDED(fence->SetEventOnCompletion(value,event));
    const bool done=armed && WaitForSingleObject(event,10000)==WAIT_OBJECT_0;
    CloseHandle(event);return done;
  };
  auto barrier=[&](ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};
    list->ResourceBarrier(1,&b);
  };
  uint64_t serial=0;
  for(DXGI_FORMAT format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_BC1_UNORM}) {
    D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width=td.Height=8;td.DepthOrArraySize=6;td.MipLevels=2;td.Format=format;td.SampleDesc.Count=1;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> old_texture,new_texture;
    CHECK_HR(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&old_texture)));
    CHECK_HR(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&new_texture)));
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,12> fp{};
    std::array<UINT,12> rows{};std::array<UINT64,12> rowbytes{};UINT64 total=0;
    device->GetCopyableFootprints(&td,0,12,0,fp.data(),rows.data(),rowbytes.data(),&total);
    struct Level { uint32_t width,height,row_pitch,rows;size_t offset; };
    std::array<Level,12> levels{};std::vector<uint8_t> bytes;
    // Same mip-major/slice-minor decoded order as production, unlike D3D.
    for(unsigned mip=0;mip<2;++mip) for(unsigned slice=0;slice<6;++slice) {
      const unsigned sub=mip+slice*2;
      levels[mip*6+slice]={8u>>mip,8u>>mip,uint32_t(rowbytes[sub]),rows[sub],bytes.size()};
      bytes.insert(bytes.end(),size_t(rowbytes[sub])*rows[sub],uint8_t(sub+1));
    }
    auto budget=std::make_shared<D3DTextureSnapshotBudget>();
    auto baseline=D3DTextureSnapshot::Capture(budget,std::span(bytes),std::span(levels));SR_CHECK(baseline);
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=total;
    bd.Height=bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> upload,read_old,read_new;
    heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    CHECK_HR(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
    heap.Type=D3D12_HEAP_TYPE_READBACK;
    CHECK_HR(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read_old)));
    CHECK_HR(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read_new)));
    uint8_t* mapped=nullptr;CHECK_HR(upload->Map(0,nullptr,reinterpret_cast<void**>(&mapped)));
    for(unsigned sub=0;sub<12;++sub) for(unsigned row=0;row<rows[sub];++row)
      std::memset(mapped+fp[sub].Offset+size_t(row)*fp[sub].Footprint.RowPitch,sub+1,size_t(rowbytes[sub]));
    upload->Unmap(0,nullptr);
    auto upload_sub=[&](ID3D12Resource* target,unsigned sub) {
      D3D12_TEXTURE_COPY_LOCATION src{},dst{};
      src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp[sub];
      dst.pResource=target;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.SubresourceIndex=sub;
      list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    };
    auto read=[&](ID3D12Resource* source,ID3D12Resource* target) {
      for(unsigned sub=0;sub<12;++sub) {
        D3D12_TEXTURE_COPY_LOCATION src{},dst{};
        src.pResource=source;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=sub;
        dst.pResource=target;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp[sub];
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
      }
    };
    for(unsigned sub=0;sub<12;++sub) upload_sub(old_texture.Get(),sub);
    barrier(old_texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
    CHECK_HR(list->Close());ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);
    CHECK_HR(queue->Signal(fence.Get(),++serial));SR_CHECK(wait(serial));
    CHECK_HR(allocator->Reset());CHECK_HR(list->Reset(allocator.Get(),nullptr));
    // A real queue gate makes the pending-version decision deterministic.
    CHECK_HR(queue->Wait(gate.Get(),serial));
    const unsigned changed_sub=7;const size_t changed_level=(changed_sub%2)*6+changed_sub/2;
    bytes[levels[changed_level].offset]=99;
    CHECK_HR(upload->Map(0,nullptr,reinterpret_cast<void**>(&mapped)));
    mapped[fp[changed_sub].Offset]=99;upload->Unmap(0,nullptr);
    unsigned skipped=0;
    for(unsigned sub=0;sub<12;++sub) {
      if(baseline->Matches(std::span(bytes),std::span(levels),(sub%2)*6+sub/2)) {
        ++skipped;D3D12_TEXTURE_COPY_LOCATION src{},dst{};
        src.pResource=old_texture.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=sub;
        dst.pResource=new_texture.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.SubresourceIndex=sub;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
      } else upload_sub(new_texture.Get(),sub);
    }
    SR_CHECK_EQ(skipped,11u);
    barrier(new_texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
    read(old_texture.Get(),read_old.Get());read(new_texture.Get(),read_new.Get());
    CHECK_HR(list->Close());queue->ExecuteCommandLists(1,lists);
    const uint64_t gate_value=serial;CHECK_HR(queue->Signal(fence.Get(),++serial));
    SR_CHECK(!D3DTextureCanOverwrite(serial,fence->GetCompletedValue(),serial+1));
    // Always release the queue before any exit, including a failed assertion.
    CHECK_HR(gate->Signal(gate_value));SR_CHECK(wait(serial));
    for(unsigned version=0;version<2;++version) {
      auto* rb=version?read_new.Get():read_old.Get();CHECK_HR(rb->Map(0,nullptr,reinterpret_cast<void**>(&mapped)));
      bool exact=true;
      for(unsigned sub=0;sub<12;++sub) for(unsigned row=0;row<rows[sub];++row)
        for(size_t x=0;x<rowbytes[sub];++x)
          exact&=mapped[fp[sub].Offset+size_t(row)*fp[sub].Footprint.RowPitch+x]==
              ((version && sub==changed_sub && !row && !x)?99:sub+1);
      rb->Unmap(0,nullptr);SR_CHECK(exact);
    }
    SR_CHECK(D3DTextureCanOverwrite(serial,fence->GetCompletedValue(),serial+1));
    CHECK_HR(allocator->Reset());CHECK_HR(list->Reset(allocator.Get(),nullptr));
    auto* same_allocation=new_texture.Get();
    CHECK_HR(upload->Map(0,nullptr,reinterpret_cast<void**>(&mapped)));
    mapped[fp[changed_sub].Offset]=88;upload->Unmap(0,nullptr);
    barrier(new_texture.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    upload_sub(new_texture.Get(),changed_sub);
    barrier(new_texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
    read(new_texture.Get(),read_new.Get());
    CHECK_HR(list->Close());queue->ExecuteCommandLists(1,lists);
    CHECK_HR(queue->Signal(fence.Get(),++serial));SR_CHECK(wait(serial));
    SR_CHECK(new_texture.Get()==same_allocation);
    CHECK_HR(read_new->Map(0,nullptr,reinterpret_cast<void**>(&mapped)));
    bool reused_exact=true;
    for(unsigned sub=0;sub<12;++sub) for(unsigned row=0;row<rows[sub];++row)
      for(size_t x=0;x<rowbytes[sub];++x)
        reused_exact&=mapped[fp[sub].Offset+size_t(row)*fp[sub].Footprint.RowPitch+x]==
            ((sub==changed_sub && !row && !x)?88:sub+1);
    SR_CHECK(reused_exact);read_new->Unmap(0,nullptr);
    CHECK_HR(allocator->Reset());CHECK_HR(list->Reset(allocator.Get(),nullptr));
  }
  CHECK_HR(list->Close());
  ComPtr<ID3D12InfoQueue> info;
  if(SUCCEEDED(device.As(&info))) {
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
      SIZE_T length=0;info->GetMessage(i,nullptr,&length);std::vector<uint8_t> storage(length);
      auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
      if(SUCCEEDED(info->GetMessage(i,message,&length))) {
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) std::printf("D3D12: %s\n",message->pDescription);
        SR_CHECK(message->Severity>D3D12_MESSAGE_SEVERITY_ERROR);
      }
    }
  }
}
