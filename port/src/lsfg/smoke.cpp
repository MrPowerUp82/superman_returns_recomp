// SPDX-License-Identifier: GPL-3.0-or-later
// Real DLL shaders, real GPU dispatch, D3D12 -> Vulkan -> D3D12 round trip.
// No game/application launch, capture API or Lossless DLL code execution.
#include "win32_interop.h"
#include "lsfg-vk-backend/lsfgvk.hpp"
#include "lsfg-vk-common/vulkan/command_buffer.hpp"
#include "lsfg-vk-common/vulkan/fence.hpp"
#include <dxgi1_6.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using Microsoft::WRL::ComPtr;
using sr::lsfg::Check;
constexpr uint32_t kWidth = 256, kHeight = 256;

class D3D {
 public:
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> commands;
  ComPtr<ID3D12Fence> completion;
  uint64_t serial = 0;
  D3D() {
    ComPtr<IDXGIFactory6> factory;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
    ComPtr<IDXGIAdapter1> adapter;
    Check(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
        IID_PPV_ARGS(&adapter)), "DXGI adapter");
    Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&device)), "D3D12 device");
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "D3D12 queue");
    Check(device->CreateCommandAllocator(desc.Type, IID_PPV_ARGS(&allocator)), "command allocator");
    Check(device->CreateCommandList(0, desc.Type, allocator.Get(), nullptr,
        IID_PPV_ARGS(&commands)), "command list");
    Check(commands->Close(), "initial close");
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&completion)), "completion fence");
  }
  void Begin() {
    Check(allocator->Reset(), "allocator reset");
    Check(commands->Reset(allocator.Get(), nullptr), "command list reset");
  }
  void Submit() {
    Check(commands->Close(), "close command list");
    ID3D12CommandList* lists[]{commands.Get()};
    queue->ExecuteCommandLists(1, lists);
  }
  void Wait() {
    Check(queue->Signal(completion.Get(), ++serial), "signal completion");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) throw std::runtime_error("CreateEvent failed");
    HRESULT result = completion->SetEventOnCompletion(serial, event);
    if (FAILED(result)) { CloseHandle(event); Check(result, "completion event"); }
    DWORD waited = WaitForSingleObject(event, 30000);
    CloseHandle(event);
    if (waited != WAIT_OBJECT_0) throw std::runtime_error("D3D12 completion timeout");
    Check(device->GetDeviceRemovedReason(), "D3D12 device status");
  }
  ComPtr<ID3D12Resource> Buffer(size_t bytes, bool readback) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = readback ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> buffer;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        readback ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&buffer)), "staging buffer");
    return buffer;
  }
  void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                  D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    commands->ResourceBarrier(1, &barrier);
  }
};

VkImageMemoryBarrier Barrier(VkImage image, VkImageLayout before, VkImageLayout after,
    VkAccessFlags from, VkAccessFlags to, uint32_t source = VK_QUEUE_FAMILY_IGNORED,
    uint32_t destination = VK_QUEUE_FAMILY_IGNORED) {
  return {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .srcAccessMask = from, .dstAccessMask = to,
          .oldLayout = before, .newLayout = after,
          .srcQueueFamilyIndex = source, .dstQueueFamilyIndex = destination,
          .image = image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
}

std::vector<uint8_t> Pattern(uint32_t left) {
  std::vector<uint8_t> bytes(kWidth * kHeight * 4);
  for (uint32_t y = 0; y < kHeight; ++y) for (uint32_t x = 0; x < kWidth; ++x) {
    auto* pixel = &bytes[(y * kWidth + x) * 4];
    bool box = x >= left && x < left + 48 && y >= 80 && y < 176;
    pixel[0] = box ? 240 : 24;
    pixel[1] = box ? 210 : 32;
    pixel[2] = box ? 40 : 48;
    pixel[3] = 255;
  }
  return bytes;
}

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc < 2 || argc > 4) {
      std::cerr << "Usage: sr_lsfg_smoke.exe <Lossless.dll> [generated.ppm] [quality|performance]\n";
      return 2;
    }
    bool performance = true;
    if (argc == 4) {
      std::wstring mode(argv[3]);
      if (mode != L"quality" && mode != L"performance")
        throw std::runtime_error("Mode must be quality or performance");
      performance = mode == L"performance";
    }
    // Verify the path before allocating a GPU device. The backend reads bytes.
    std::filesystem::path dll(argv[1]);
    if (!std::filesystem::is_regular_file(dll)) throw std::runtime_error("DLL file not found");
    if (argc >= 3 && std::filesystem::exists(std::filesystem::path(argv[2])) &&
        std::filesystem::equivalent(dll, std::filesystem::path(argv[2])))
      throw std::runtime_error("Output must not overwrite the DLL");
    D3D d3d;
    auto luid = d3d.device->GetAdapterLuid();
    std::array<uint8_t, 8> luidBytes{};
    std::memcpy(luidBytes.data(), &luid, sizeof(luid));
    lsfgvk::backend::Instance backend(luidBytes, dll, false);
    const auto& vk = backend.vulkan();
    VkPhysicalDeviceProperties2 properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vk.fi().GetPhysicalDeviceProperties2(vk.physdev(), &properties);
    std::cout << "Adapter: " << properties.properties.deviceName << " (LUID matched)\n";
    auto& context = backend.openLocalContext(kWidth, kHeight, false, 4.0F, performance, 1);
    std::cout << "Mode: FP32 " << (performance ? "performance" : "quality") << '\n';
    sr::lsfg::SharedImage previous(vk, d3d.device.Get(), kWidth, kHeight);
    sr::lsfg::SharedImage next(vk, d3d.device.Get(), kWidth, kHeight);
    sr::lsfg::SharedImage output(vk, d3d.device.Get(), kWidth, kHeight);
    sr::lsfg::SharedFence sync(vk, d3d.device.Get());
    auto desc = previous.d3d()->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 allocationSize{};
    d3d.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &allocationSize);
    auto previousBytes = Pattern(72), nextBytes = Pattern(104);
    std::array<ComPtr<ID3D12Resource>, 2> uploads;
    d3d.Begin();
    std::array<ID3D12Resource*, 2> textures{previous.d3d(), next.d3d()};
    for (size_t i = 0; i < 2; ++i) {
      uploads[i] = d3d.Buffer(allocationSize, false);
      void* mapped{};
      D3D12_RANGE empty{0, 0};
      Check(uploads[i]->Map(0, &empty, &mapped), "map upload");
      const auto& bytes = i ? nextBytes : previousBytes;
      for (size_t y = 0; y < kHeight; ++y)
        std::memcpy(static_cast<uint8_t*>(mapped) + footprint.Offset + y * footprint.Footprint.RowPitch,
            bytes.data() + y * kWidth * 4, kWidth * 4);
      uploads[i]->Unmap(0, nullptr);
      D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = uploads[i].Get();
      source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; source.PlacedFootprint = footprint;
      D3D12_TEXTURE_COPY_LOCATION target{}; target.pResource = textures[i];
      target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      d3d.Transition(textures[i], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
      d3d.commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
      d3d.Transition(textures[i], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    }
    d3d.Submit();
    Check(d3d.queue->Signal(sync.d3d(), 1), "D3D12 to Vulkan fence");
    vk::CommandBuffer command(vk);
    vk::Fence completed(vk);
    command.begin(vk);
    std::array<VkImage, 2> imported{previous.image(), next.image()};
    for (size_t i = 0; i < 2; ++i) {
      auto source = backend.sourceImage(context, 1 - i);
      command.copyImage(vk, {
          Barrier(imported[0], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              0, VK_ACCESS_TRANSFER_READ_BIT, VK_QUEUE_FAMILY_EXTERNAL, vk.queuefamily()),
          Barrier(source, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              0, VK_ACCESS_TRANSFER_WRITE_BIT)}, {imported[0], source}, {kWidth, kHeight}, {
          Barrier(imported[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
              VK_ACCESS_TRANSFER_READ_BIT, 0, vk.queuefamily(), VK_QUEUE_FAMILY_EXTERNAL),
          Barrier(source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)});
    }
    // LSFG keeps two/three temporal feature slots, not just two RGB frames.
    // Prime those slots with a static frame before evaluating motion.
    for (size_t i = 0; i < 4; ++i) backend.recordFrame(context, command);
    auto currentSource = backend.sourceImage(context, 0);
    command.copyImage(vk, {
        Barrier(imported[1], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            0, VK_ACCESS_TRANSFER_READ_BIT, VK_QUEUE_FAMILY_EXTERNAL, vk.queuefamily()),
        Barrier(currentSource, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT)},
        {imported[1], currentSource}, {kWidth, kHeight}, {
        Barrier(imported[1], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_TRANSFER_READ_BIT, 0, vk.queuefamily(), VK_QUEUE_FAMILY_EXTERNAL),
        Barrier(currentSource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)});
    backend.recordFrame(context, command);
    auto generated = backend.destinationImage(context, 0);
    command.copyImage(vk, {
        Barrier(generated, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT),
        Barrier(output.image(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_QUEUE_FAMILY_EXTERNAL, vk.queuefamily())},
        {generated, output.image()}, {kWidth, kHeight}, {
        Barrier(generated, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT),
        Barrier(output.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, 0, vk.queuefamily(), VK_QUEUE_FAMILY_EXTERNAL)});
    command.end(vk);
    auto start = std::chrono::steady_clock::now();
    command.submit(vk, {}, sync.semaphore(), 1, {}, sync.semaphore(), 2, completed.handle());
    if (!completed.wait(vk, 30'000'000'000ULL)) throw std::runtime_error("LSFG GPU timeout");
    auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    // GPU completion is checked before queueing a cross-API wait to avoid a
    // hung D3D12 queue when a shader/device fails during this diagnostic.
    Check(d3d.queue->Wait(sync.d3d(), 2), "Vulkan to D3D12 fence");
    d3d.Wait(); // The command allocator's uploads have now completed.
    auto readback = d3d.Buffer(allocationSize, true);
    d3d.Begin();
    D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = output.d3d();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION target{}; target.pResource = readback.Get();
    target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; target.PlacedFootprint = footprint;
    d3d.Transition(output.d3d(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    d3d.commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
    d3d.Transition(output.d3d(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    d3d.Submit(); d3d.Wait();
    void* mapped{};
    D3D12_RANGE range{0, static_cast<SIZE_T>(allocationSize)};
    Check(readback->Map(0, &range, &mapped), "map readback");
    std::vector<uint8_t> bytes(kWidth * kHeight * 4);
    for (size_t y = 0; y < kHeight; ++y)
      std::memcpy(bytes.data() + y * kWidth * 4,
          static_cast<uint8_t*>(mapped) + footprint.Offset + y * footprint.Footprint.RowPitch, kWidth * 4);
    readback->Unmap(0, nullptr);
    uint64_t count = 0, sum = 0;
    for (size_t i = 0; i < bytes.size(); i += 4)
      if (bytes[i] > 150 && bytes[i + 1] > 130) { ++count; sum += (i / 4) % kWidth; }
    double center = count ? static_cast<double>(sum) / count : 0;
    std::cout << "Foreground centroid=" << center << ", pixels=" << count
              << ", GPU batch ms=" << elapsed << " (4 warmups + 1 interpolation; not gameplay FPS)\n";
    if (argc >= 3) {
      std::ofstream file(std::filesystem::path(argv[2]), std::ios::binary);
      file << "P6\n" << kWidth << ' ' << kHeight << "\n255\n";
      for (size_t i = 0; i < bytes.size(); i += 4) file.write(reinterpret_cast<char*>(&bytes[i]), 3);
      if (!file) throw std::runtime_error("Unable to write generated PPM");
    }
    if (count < 1000 || count > 10000) throw std::runtime_error("Generated frame has invalid foreground coverage");
    if (center <= 96 || center >= 127) throw std::runtime_error("Generated frame is not between source positions");
    std::cout << "PASS: DLL compute shaders generated an intermediate frame; "
              << "D3D12/Vulkan shared texture and fence round trip passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
