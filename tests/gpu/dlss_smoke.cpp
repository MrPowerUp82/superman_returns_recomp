// Standalone SDK smoke. Optional --evaluate uses a synthetic static scene, not gameplay.
#define NOMINMAX
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include "../../port/src/graphics/d3d12/dlss_super_resolution.h"

using Microsoft::WRL::ComPtr;
using namespace superman_returns::graphics::d3d12;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct GpuScope {
    DlssSuperResolution& dlss;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12DescriptorHeap> clearHeap;
    std::array<ComPtr<ID3D12Resource>, 4> textures;
    ComPtr<ID3D12Resource> readback;
    HANDLE event = nullptr;
    UINT64 value = 0;
    void Wait() {
        Require(SUCCEEDED(fence->SetEventOnCompletion(value, event)), "SetEventOnCompletion");
        Require(WaitForSingleObject(event, 30000) == WAIT_OBJECT_0, "GPU fence timeout");
        Require(fence->GetCompletedValue() != UINT64_MAX, "device removed");
    }
    ~GpuScope() {
        // Covers submitted work on success and error; unsubmitted lists are discarded.
        if (queue && fence && event) {
            if (FAILED(queue->Signal(fence.Get(), ++value))) std::abort();
            try { Wait(); } catch (...) { std::abort(); }
            if (!dlss.Shutdown(fence.Get(), value)) std::abort();
        }
        if (event) CloseHandle(event);
    }
};

static float HalfToFloat(std::uint16_t h) {
    const auto sign = (h & 0x8000) ? -1.0f : 1.0f;
    const auto exponent = (h >> 10) & 31;
    if (exponent == 31) return NAN;
    return sign * std::ldexp(exponent ? 1.0f + float(h & 1023) / 1024.0f : float(h & 1023) / 1024.0f,
                            exponent ? int(exponent) - 15 : -14);
}

static bool EvaluateSynthetic(ID3D12Device* device, DlssSuperResolution& dlss,
                              const DlssOptimalSettings& settings) {
    GpuScope gpu{dlss};
    try {
        D3D12_COMMAND_QUEUE_DESC queue{};
        queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Require(SUCCEEDED(device->CreateCommandQueue(&queue, IID_PPV_ARGS(&gpu.queue))), "CreateCommandQueue");
        Require(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu.fence))), "CreateFence");
        gpu.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Require(gpu.event != nullptr, "CreateEvent");
        Require(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gpu.allocator))), "CreateCommandAllocator");
        Require(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpu.allocator.Get(), nullptr,
                                                   IID_PPV_ARGS(&gpu.commands))), "CreateCommandList");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 4; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Require(SUCCEEDED(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&gpu.heap))), "CreateDescriptorHeap");
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        Require(SUCCEEDED(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&gpu.clearHeap))), "Create CPU clear heap");
        auto* boundHeap = gpu.heap.Get();
        gpu.commands->SetDescriptorHeaps(1, &boundHeap);
        const auto stride = device->GetDescriptorHandleIncrementSize(heap.Type);
        const std::array<DXGI_FORMAT, 4> formats{DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                               DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16_FLOAT};
        const float clears[4][4]{{0.25f, 0.5f, 0.75f, 1}, {1, 0, 1, 1}, {0.5f, 0, 0, 0}, {0, 0, 0, 0}};
        D3D12_HEAP_PROPERTIES memory{};
        memory.Type = D3D12_HEAP_TYPE_DEFAULT; memory.CreationNodeMask = memory.VisibleNodeMask = 1;
        for (UINT i = 0; i < 4; ++i) {
            D3D12_RESOURCE_DESC texture{};
            texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            texture.Width = i == 1 ? settings.outputWidth : settings.renderWidth;
            texture.Height = i == 1 ? settings.outputHeight : settings.renderHeight;
            texture.DepthOrArraySize = texture.MipLevels = 1;
            texture.Format = formats[i]; texture.SampleDesc.Count = 1;
            texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            Require(SUCCEEDED(device->CreateCommittedResource(&memory, D3D12_HEAP_FLAG_NONE, &texture,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&gpu.textures[i]))), "Create input/output texture");
            auto cpu = gpu.heap->GetCPUDescriptorHandleForHeapStart(); cpu.ptr += SIZE_T(i) * stride;
            auto clearCpu = gpu.clearHeap->GetCPUDescriptorHandleForHeapStart(); clearCpu.ptr += SIZE_T(i) * stride;
            auto handle = gpu.heap->GetGPUDescriptorHandleForHeapStart(); handle.ptr += UINT64(i) * stride;
            D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
            view.Format = formats[i]; view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(gpu.textures[i].Get(), nullptr, &view, cpu);
            device->CreateUnorderedAccessView(gpu.textures[i].Get(), nullptr, &view, clearCpu);
            gpu.commands->ClearUnorderedAccessViewFloat(handle, clearCpu, gpu.textures[i].Get(), clears[i], 0, nullptr);
            D3D12_RESOURCE_BARRIER barrier{};
            if (i == 1) {
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.UAV.pResource = gpu.textures[i].Get();
            } else {
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = gpu.textures[i].Get();
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            }
            gpu.commands->ResourceBarrier(1, &barrier);
        }
        DlssEvaluation evaluation{};
        evaluation.commands = gpu.commands.Get();
        evaluation.color = gpu.textures[0].Get(); evaluation.output = gpu.textures[1].Get();
        evaluation.depth = gpu.textures[2].Get(); evaluation.motion = gpu.textures[3].Get();
        evaluation.colorState = evaluation.depthState = evaluation.motionState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        evaluation.outputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        auto& frame = evaluation.frame;
        frame.renderWidth = settings.renderWidth; frame.renderHeight = settings.renderHeight;
        frame.outputWidth = settings.outputWidth; frame.outputHeight = settings.outputHeight;
        // This scene is a uniform stationary plane: no HUD, camera/object/deformation motion,
        // depth=0.5, zero actual projection jitter. These facts are constructed, not assumed.
        frame.conventionsVerified = frame.colorBeforeHud = frame.rasterJitterApplied = true;
        frame.motionIncludesCameraObjectsAndDeformation = true;
        frame.depth = DlssDepth::Forward; frame.motion = DlssMotion::CurrentToPreviousPixels;
        frame.mvScaleX = frame.mvScaleY = 1; frame.hdr = true; frame.reset = true;
        evaluation.colorState |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        Require(dlss.Evaluate(evaluation).error == DlssError::InvalidResourceStates, "Reject combined input states");
        evaluation.colorState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        const auto evaluated = dlss.Evaluate(evaluation);
        std::printf("Synthetic Evaluate error=%u ngx=%08X ticket=%llu applied=%u\n", unsigned(evaluated.error),
            evaluated.ngxResult, static_cast<unsigned long long>(evaluated.ticket), unsigned(dlss.GetStatus().applied));
        Require(bool(evaluated), "NGX Evaluate");
        Require(dlss.GetStatus().applied == DlssApplied::PendingGpu, "Recording must not apply Quality");
        Require(dlss.Evaluate(evaluation).error == DlssError::SubmissionRequired, "Unsubmitted creation must block reuse");
        Require(dlss.ConfirmExecution(evaluated.ticket, gpu.fence.Get(), 1).error == DlssError::InvalidTicket,
                "Unsubmitted ticket cannot apply Quality");
        Require(dlss.Shutdown(gpu.fence.Get(), 1).error == DlssError::GpuPending, "Cannot release in-flight resources");
        auto outDesc = gpu.textures[1]->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 bytes = 0;
        device->GetCopyableFootprints(&outDesc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = bytes;
        buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        memory.Type = D3D12_HEAP_TYPE_READBACK;
        Require(SUCCEEDED(device->CreateCommittedResource(&memory, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gpu.readback))), "Create readback");
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = gpu.textures[1].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        gpu.commands->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION destination{}, source{};
        destination.pResource = gpu.readback.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        source.pResource = gpu.textures[1].Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        gpu.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        Require(SUCCEEDED(gpu.commands->Close()), "Close command list");
        ID3D12CommandList* lists[] = {gpu.commands.Get()};
        gpu.queue->ExecuteCommandLists(1, lists);
        Require(SUCCEEDED(gpu.queue->Signal(gpu.fence.Get(), ++gpu.value)), "Signal renderer fence");
        Require(bool(dlss.NotifySubmitted(evaluated.ticket, gpu.fence.Get(), gpu.value)), "NotifySubmitted");
        Require(dlss.GetStatus().applied == DlssApplied::PendingGpu, "Submission must not apply Quality");
        gpu.Wait();
        Require(bool(dlss.ConfirmExecution(evaluated.ticket, gpu.fence.Get(), gpu.value)), "ConfirmExecution");
        Require(dlss.GetStatus().applied == DlssApplied::Quality, "Completed evaluation must apply Quality");
        void* mapped = nullptr;
        D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
        Require(SUCCEEDED(gpu.readback->Map(0, &range, &mapped)), "Map output");
        const auto offset = footprint.Offset + UINT64(settings.outputHeight / 2) * footprint.Footprint.RowPitch +
                            UINT64(settings.outputWidth / 2) * 8;
        std::uint16_t pixel[4];
        std::memcpy(pixel, static_cast<const unsigned char*>(mapped) + offset, sizeof(pixel));
        D3D12_RANGE written{0, 0}; gpu.readback->Unmap(0, &written);
        const float red = HalfToFloat(pixel[0]), green = HalfToFloat(pixel[1]), blue = HalfToFloat(pixel[2]);
        std::printf("Synthetic completed applied=%u center_rgb=%.4f,%.4f,%.4f (expected 0.25,0.50,0.75)\n",
                    unsigned(dlss.GetStatus().applied), red, green, blue);
        Require(std::isfinite(red) && std::isfinite(green) && std::isfinite(blue) &&
                std::abs(red - 0.25f) < 0.1f && std::abs(green - 0.5f) < 0.1f && std::abs(blue - 0.75f) < 0.1f,
                "DLSS did not reconstruct the synthetic color (output initially magenta)");
        dlss.SetRequested(DlssRequested::Off);
        Require(dlss.GetStatus().applied == DlssApplied::Off, "Off clears applied");
        return true;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Synthetic smoke failed: %s\n", error.what());
        return false;
    }
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) { std::fprintf(stderr, "Usage: dlss_smoke <absolute runtime/data directory> [--evaluate]\n"); return 1; }
    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { std::printf("CreateDXGIFactory1 hr=%08X\n", unsigned(hr)); return 1; }
    ComPtr<ID3D12Device> device;
    for (UINT i = 0; ; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(i, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) return 1;
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) return 1;
        if (desc.VendorId != 0x10de || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
        if (FAILED(hr)) continue;
        std::printf("adapter=%ls vendor=%04X device=%04X luid=%08X:%08X\n",
            desc.Description, desc.VendorId, desc.DeviceId,
            unsigned(desc.AdapterLuid.HighPart), desc.AdapterLuid.LowPart);
        break;
    }
    if (!device) { std::puts("No hardware NVIDIA D3D12 device; smoke not passed"); return 2; }
    DlssSuperResolution dlss;
    dlss.SetRequested(DlssRequested::Quality);
    const auto initialized = dlss.Initialize(device.Get(), argv[1]);
    std::printf("Initialize error=%u ngx=%08X support=%u applied=%u\n",
        unsigned(initialized.error), initialized.ngxResult,
        unsigned(dlss.GetStatus().support), unsigned(dlss.GetStatus().applied));
    if (!initialized) return 3;
    wchar_t loadedPath[32768]{};
    const auto dll = GetModuleHandleW(L"nvngx_dlss.dll");
    const auto pathLength = dll ? GetModuleFileNameW(dll, loadedPath, 32768) : 0;
    std::error_code pathError;
    const bool officialRuntime = pathLength && pathLength < 32768 &&
        std::filesystem::equivalent(loadedPath, std::filesystem::path(argv[1]) / L"nvngx_dlss.dll", pathError) && !pathError;
    std::printf("loaded_dlss=%ls expected_official_runtime=%u\n", loadedPath, unsigned(officialRuntime));
    if (!officialRuntime) { (void)dlss.Shutdown(); return 5; }
    DlssOptimalSettings settings{};
    const auto queried = dlss.QueryOptimalSettings(1920, 1080, settings);
    std::printf("QueryOptimalSettings Quality error=%u ngx=%08X output=%ux%u render=%ux%u min=%ux%u max=%ux%u sharpness=%f\n",
        unsigned(queried.error), queried.ngxResult, settings.outputWidth, settings.outputHeight,
        settings.renderWidth, settings.renderHeight, settings.minWidth, settings.minHeight,
        settings.maxWidth, settings.maxHeight, settings.recommendedSharpness);
    const bool honest = dlss.GetStatus().applied == DlssApplied::Off;
    const bool evaluate = argc == 3 && std::wcscmp(argv[2], L"--evaluate") == 0;
    const bool evaluated = !evaluate || (queried && EvaluateSynthetic(device.Get(), dlss, settings));
    const auto shutdown = dlss.Shutdown(); // Synthetic scope already performed fenced shutdown.
    std::printf("Shutdown error=%u ngx=%08X; Evaluate=%s\n",
        unsigned(shutdown.error), shutdown.ngxResult, evaluate ? "synthetic only" : "NOT exercised");
    return queried && shutdown && honest && evaluated ? 0 : 4;
}
