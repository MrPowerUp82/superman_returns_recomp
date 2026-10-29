#define NOMINMAX
#include <windows.h>
#include <dxgi1_6.h>
#include "d3d12_buffer_heap_pool.h"
#include <array>
#include <cstdio>
using Microsoft::WRL::ComPtr;
using superman_returns::native::D3D12BufferHeapPool;

// Intercept only the three allocation calls; all successful work uses WARP.
struct FaultDevice {
  ID3D12Device* device;
  bool fail_heap = false;
  unsigned fail_placed = 0, heap_calls = 0, placed_calls = 0, committed_calls = 0;
  D3D12_RESOURCE_ALLOCATION_INFO GetResourceAllocationInfo(UINT mask, UINT count,
      const D3D12_RESOURCE_DESC* desc) { return device->GetResourceAllocationInfo(mask, count, desc); }
  HRESULT CreateHeap(const D3D12_HEAP_DESC* desc, REFIID id, void** out) {
    ++heap_calls;
    return fail_heap ? E_OUTOFMEMORY : device->CreateHeap(desc, id, out);
  }
  HRESULT CreatePlacedResource(ID3D12Heap* heap, UINT64 offset, const D3D12_RESOURCE_DESC* desc,
      D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear, REFIID id, void** out) {
    if (++placed_calls == fail_placed) return E_OUTOFMEMORY;
    return device->CreatePlacedResource(heap, offset, desc, state, clear, id, out);
  }
  HRESULT CreateCommittedResource(const D3D12_HEAP_PROPERTIES* props, D3D12_HEAP_FLAGS flags,
      const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES state,
      const D3D12_CLEAR_VALUE* clear, REFIID id, void** out) {
    ++committed_calls;
    return device->CreateCommittedResource(props, flags, desc, state, clear, id, out);
  }
};

int main() {
  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter> warp;
  ComPtr<ID3D12Device> device;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
      FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
      FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return 1;
  // Pool must precede resources: resources are destroyed before their heaps.
  D3D12BufferHeapPool pool;
  std::array<ComPtr<ID3D12Resource>, 258> buffers;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = 256;
  desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  for (size_t i = 0; i < buffers.size(); ++i) {
    if (FAILED(pool.Create(device.Get(), desc, D3D12_RESOURCE_STATE_COPY_DEST, buffers[i]))) return 2;
    if (!buffers[i]->GetGPUVirtualAddress()) return 3;
    for (size_t j = 0; j < i; ++j)
      if (buffers[i]->GetGPUVirtualAddress() == buffers[j]->GetGPUVirtualAddress()) return 4;
  }
  if (pool.pages() != 2) return 5;
  ComPtr<ID3D12Resource> large;
  desc.Width = D3D12BufferHeapPool::kPageBytes + 1;
  if (SUCCEEDED(pool.Create(device.Get(), desc, D3D12_RESOURCE_STATE_COPY_DEST, large)) || large || pool.pages() != 2) return 6;
  desc.Width = 256;
  if (FAILED(pool.Create(device.Get(), desc, D3D12_RESOURCE_STATE_COPY_DEST, large))) return 17;
  struct FailureCase {
    D3D12BufferHeapPool pool;
    ComPtr<ID3D12Resource> existing, fallback, subsequent;
  } failures[3];
  for (unsigned i = 0; i < 3; ++i) {
    auto& f = failures[i];
    FaultDevice injected{device.Get()};
    injected.fail_heap = i == 0;
    injected.fail_placed = i;
    if (i == 2 && FAILED(f.pool.Create(&injected, desc, D3D12_RESOURCE_STATE_COPY_DEST, f.existing))) return 18;
    if (FAILED(f.pool.CreateOrCommitted(&injected, desc, D3D12_RESOURCE_STATE_COPY_DEST, f.fallback))) {
      std::printf("FAIL committed fallback after injected failure %u\n", i); return 19;
    }
    const auto heaps = injected.heap_calls, placed = injected.placed_calls;
    if (FAILED(f.pool.CreateOrCommitted(&injected, desc, D3D12_RESOURCE_STATE_COPY_DEST, f.subsequent)) ||
        injected.heap_calls != heaps || injected.placed_calls != placed || injected.committed_calls != 2 ||
        f.pool.pages() != (i == 2 ? 1u : 0u)) return 20;
  }

  D3D12_HEAP_PROPERTIES props{};
  props.Type = D3D12_HEAP_TYPE_UPLOAD;
  desc.Width = 512;
  ComPtr<ID3D12Resource> upload, readback;
  if (FAILED(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) return 7;
  props.Type = D3D12_HEAP_TYPE_READBACK;
  constexpr size_t kReadbackBytes = 7 * 256;
  desc.Width = kReadbackBytes;
  if (FAILED(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return 8;
  uint8_t* data = nullptr;
  D3D12_RANGE empty{};
  if (FAILED(upload->Map(0, &empty, reinterpret_cast<void**>(&data)))) return 9;
  for (size_t i = 0; i < 512; ++i) data[i] = uint8_t(i ^ (i < 256 ? 0x15 : 0xAB));
  upload->Unmap(0, nullptr);
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  D3D12_COMMAND_QUEUE_DESC q{};
  if (FAILED(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue))) ||
      FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
      FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)))) return 10;
  // Neighbors, rollover and buffers surviving all three allocation failures.
  ID3D12Resource* checked[]{buffers[0].Get(), buffers[1].Get(), buffers[256].Get(),
      failures[0].fallback.Get(), failures[1].fallback.Get(),
      failures[2].existing.Get(), failures[2].fallback.Get()};
  for (size_t i = 0; i < 7; ++i) {
    const uint64_t source = (i % 2) * 256;
    list->CopyBufferRegion(checked[i], 0, upload.Get(), source, 256);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {checked[i], D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &barrier);
    list->CopyBufferRegion(readback.Get(), i * 256, checked[i], 0, 256);
  }
  if (FAILED(list->Close())) return 11;
  ID3D12CommandList* lists[]{list.Get()};
  queue->ExecuteCommandLists(1, lists);
  ComPtr<ID3D12Fence> fence;
  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return 12;
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event || FAILED(queue->Signal(fence.Get(), 1)) || FAILED(fence->SetEventOnCompletion(1, event))) return 13;
  const auto waited = WaitForSingleObject(event, 10000);
  CloseHandle(event);
  if (waited != WAIT_OBJECT_0) return 14;
  D3D12_RANGE range{0, kReadbackBytes};
  if (FAILED(readback->Map(0, &range, reinterpret_cast<void**>(&data)))) return 15;
  bool valid = true;
  for (size_t i = 0; i < kReadbackBytes; ++i)
    valid &= data[i] == uint8_t((i % 256) ^ ((i / 256) % 2 ? 0xAB : 0x15));
  readback->Unmap(0, &empty);
  if (!valid) return 16;
  std::puts("D3D12 WARP pool: alignment, rollover, oversized fallback and independent contents OK");
  return 0;
}
