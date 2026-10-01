// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdint>

namespace sr::lsfg {
void NotifySourceFrame(void* presenter, uint64_t serial);
void ForgetPresenter(void* presenter);
bool TryPresent(void* presenter, ID3D12Device* device, ID3D12CommandQueue* queue,
                IDXGISwapChain3* swapchain, ID3D12Resource* backbuffer,
                uint64_t serial, bool active, UINT flags, HRESULT& result);
}
