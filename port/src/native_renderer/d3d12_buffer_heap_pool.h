#pragma once
#include "buffer_heap_layout.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <utility>

namespace superman_returns::native {
// Worker-owned, append-only heaps. Each placed resource has its own state,
// GPU VA and descriptors; ranges are never aliased or recycled. Keep this pool
// alive longer than every live or fence-retired resource created from it.
class D3D12BufferHeapPool {
 public:
  static constexpr uint64_t kPageBytes = 16u << 20;
  template <typename Device>
  HRESULT Create(Device* device, const D3D12_RESOURCE_DESC& desc,
                 D3D12_RESOURCE_STATES state,
                 Microsoft::WRL::ComPtr<ID3D12Resource>& out) {
    if (failed_ || desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) return E_NOTIMPL;
    const auto info = device->GetResourceAllocationInfo(0, 1, &desc);
    auto range = PlanBufferHeapAllocation(0, kPageBytes, info.SizeInBytes, info.Alignment);
    if (!range) return E_NOTIMPL;  // oversized/invalid: use a dedicated resource
    if (!pages_.empty()) {
      auto& page = pages_.back();
      if (auto allocation = PlanBufferHeapAllocation(page.cursor, kPageBytes,
              info.SizeInBytes, info.Alignment)) {
        const HRESULT hr = device->CreatePlacedResource(page.heap.Get(), allocation->offset,
            &desc, state, nullptr, IID_PPV_ARGS(out.ReleaseAndGetAddressOf()));
        if (SUCCEEDED(hr)) { page.cursor = allocation->end; ++placed_; }
        else { failed_ = true; failure_ = hr; }
        return hr;
      }
    }
    Page page;
    D3D12_HEAP_DESC heap{};
    heap.SizeInBytes = kPageBytes;
    heap.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.Properties.CreationNodeMask = heap.Properties.VisibleNodeMask = 1;
    heap.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    HRESULT hr = device->CreateHeap(&heap, IID_PPV_ARGS(&page.heap));
    if (SUCCEEDED(hr)) {
      hr = device->CreatePlacedResource(page.heap.Get(), range->offset, &desc, state,
          nullptr, IID_PPV_ARGS(out.ReleaseAndGetAddressOf()));
    }
    if (FAILED(hr)) { failed_ = true; failure_ = hr; return hr; }
    page.cursor = range->end;
    pages_.push_back(std::move(page));
    ++placed_;
    return hr;
  }
  size_t pages() const { return pages_.size(); }
  uint64_t placed() const { return placed_; }
  uint64_t oversized_fallbacks() const { return oversized_fallbacks_; }
  uint64_t failure_fallbacks() const { return failure_fallbacks_; }
  HRESULT failure() const { return failure_; }
  template <typename Device>
  HRESULT CreateOrCommitted(Device* device, const D3D12_RESOURCE_DESC& desc,
      D3D12_RESOURCE_STATES state, Microsoft::WRL::ComPtr<ID3D12Resource>& out) {
    const HRESULT hr = Create(device, desc, state, out);
    if (SUCCEEDED(hr)) return hr;
    if (failed_) ++failure_fallbacks_;
    else ++oversized_fallbacks_;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    return device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        state, nullptr, IID_PPV_ARGS(out.ReleaseAndGetAddressOf()));
  }

 private:
  struct Page {
    Microsoft::WRL::ComPtr<ID3D12Heap> heap;
    uint64_t cursor = 0;
  };
  std::vector<Page> pages_;
  bool failed_ = false;
  HRESULT failure_ = S_OK;
  uint64_t placed_ = 0, oversized_fallbacks_ = 0, failure_fallbacks_ = 0;
};
}  // namespace superman_returns::native
