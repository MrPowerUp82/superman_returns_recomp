// SPDX-License-Identifier: GPL-3.0-or-later
#include "presentation.h"
#include "win32_interop.h"
#include "lsfg-vk-backend/lsfgvk.hpp"
#include "lsfg-vk-common/vulkan/command_buffer.hpp"
#include "lsfg-vk-common/vulkan/fence.hpp"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace sr::lsfg {
namespace {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

bool Enabled() {
  // The presenter can paint a startup window before CLI/config parsing.
  // Do not cache that initial default value for the lifetime of the process.
  return rex::cvar::Query<bool>("sr_lsfg");
}
VkImageMemoryBarrier Barrier(VkImage image, VkImageLayout before, VkImageLayout after,
    VkAccessFlags from, VkAccessFlags to, uint32_t source = VK_QUEUE_FAMILY_IGNORED,
    uint32_t destination = VK_QUEUE_FAMILY_IGNORED) {
  return {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .srcAccessMask = from, .dstAccessMask = to, .oldLayout = before, .newLayout = after,
          .srcQueueFamilyIndex = source, .dstQueueFamilyIndex = destination, .image = image,
          .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
}
ComPtr<ID3DBlob> Compile(const char* source, const char* entry, const char* target) {
  ComPtr<ID3DBlob> blob, errors;
  HRESULT result = D3DCompile(source, std::strlen(source), "lsfg_conversion", nullptr,
      nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
  if (FAILED(result)) throw std::runtime_error(errors ?
      std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) :
      "LSFG conversion shader compilation failed");
  return blob;
}
constexpr const char* kConversion = R"(
Texture2D<float4> source : register(t0);
RWTexture2D<float4> destination : register(u0);
[numthreads(8,8,1)] void capture(uint3 id : SV_DispatchThreadID) {
  uint w,h; destination.GetDimensions(w,h);
  if(id.x<w && id.y<h) destination[id.xy] = source.Load(int3(id.xy,0));
}
float4 vertex(uint id : SV_VertexID) : SV_Position {
  float2 p = float2((id << 1) & 2, id & 2);
  return float4(p * float2(2,-2) + float2(-1,1), 0, 1);
}
float4 pixel(float4 position : SV_Position) : SV_Target {
  return source.Load(int3(position.xy,0));
}
)";

class Session {
 public:
  Session(ID3D12Device* device, ID3D12CommandQueue* queue, const D3D12_RESOURCE_DESC& desc)
      : device_(device), queue_(queue), width_(uint32_t(desc.Width)), height_(desc.Height) {
    auto path = rex::cvar::Query<std::string>("sr_lsfg_dll");
    if (path.empty()) throw std::runtime_error("sr_lsfg_dll is empty");
    auto dll = std::filesystem::path(std::u8string(path.begin(), path.end()));
    if (!std::filesystem::is_regular_file(dll)) throw std::runtime_error("Lossless.dll was not found");
    auto luid = device_->GetAdapterLuid();
    std::array<uint8_t, 8> bytes{};
    std::memcpy(bytes.data(), &luid, sizeof(luid));
    backend_ = std::make_unique<lsfgvk::backend::Instance>(bytes, dll, false);
    const auto& vk = backend_->vulkan();
    bool performance = rex::cvar::Query<std::string>("sr_lsfg_mode") == "performance";
    context_ = &backend_->openLocalContext(width_, height_, false, 4.0F, performance, 1);
    input_ = std::make_unique<SharedImage>(vk, device_.Get(), width_, height_);
    output_ = std::make_unique<SharedImage>(vk, device_.Get(), width_, height_);
    sync_ = std::make_unique<SharedFence>(vk, device_.Get());
    vk_command_ = std::make_unique<vk::CommandBuffer>(vk);
    vk_done_ = std::make_unique<vk::Fence>(vk);
    Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&allocator_)), "LSFG command allocator");
    Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(),
        nullptr, IID_PPV_ARGS(&commands_)), "LSFG command list");
    Check(commands_->Close(), "initial command-list close");
    Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&completion_)), "LSFG completion fence");
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_) throw std::runtime_error("LSFG completion event creation failed");
    try { CreateConversion(desc); }
    catch (...) { CloseHandle(event_); event_ = nullptr; throw; }
    REXLOG_INFO("LSFG initialized: {}x{}, FP32 {}, direct DLL, 2x presentation",
        width_, height_, performance ? "performance" : "quality");
  }
  ~Session() {
    // The synchronous path finishes every submission before returning. Also
    // drain on failures before releasing resources referenced by either API.
    try { Wait(); } catch (...) {}
    if (backend_) backend_->vulkan().df().DeviceWaitIdle(backend_->vulkan().dev());
    if (event_) CloseHandle(event_);
  }
  bool Matches(ID3D12Device* device, ID3D12CommandQueue* queue,
               IDXGISwapChain3* swapchain, const D3D12_RESOURCE_DESC& desc) const {
    return device_.Get() == device && queue_.Get() == queue && swapchain_ == swapchain &&
        desc.Width == width_ && desc.Height == height_ && desc.Format == format_;
  }
  void Attach(IDXGISwapChain3* swapchain) { swapchain_ = swapchain; }
  bool Present(IDXGISwapChain3* swapchain, ID3D12Resource* backbuffer,
               uint64_t serial, double interval, UINT flags, HRESULT& result) {
    if (serial == last_serial_) return false;
    last_serial_ = serial;
    Capture(backbuffer);
    Generate(!initialized_);
    ++real_frames_;
    if (!initialized_) {
      initialized_ = true;
      REXLOG_INFO("LSFG temporal history initialized; next real frame can be interpolated");
      return false;
    }
    // From here the backbuffer is overwritten. Errors must be returned as GPU
    // loss rather than falling back to Present on a modified/rotated buffer.
    try {
      DrawGenerated(backbuffer);
      UINT pair_flags = flags & ~DXGI_PRESENT_RESTART;
      auto generated_time = Clock::now();
      result = swapchain->Present(0, pair_flags);
      if (FAILED(result) || result == DXGI_STATUS_OCCLUDED) return true;
      ++generated_frames_;
      ComPtr<ID3D12Resource> restored;
      Check(swapchain->GetBuffer(swapchain->GetCurrentBackBufferIndex(),
          IID_PPV_ARGS(&restored)), "LSFG next backbuffer");
      RestoreOriginal(restored.Get());
      // Keep the midpoint on screen before presenting the original. Limit the
      // wait so a stalled/paused game cannot hold the UI thread indefinitely.
      auto half = std::chrono::duration<double>(std::clamp(interval * 0.5, 0.001, 0.05));
      std::this_thread::sleep_until(generated_time +
          std::chrono::duration_cast<Clock::duration>(half));
      result = swapchain->Present(0, pair_flags);
      if (real_frames_ % 60 == 0)
        REXLOG_INFO("LSFG counters: real={}, generated={}, pairs={}, source interval={} ms",
            real_frames_, generated_frames_, generated_frames_, interval * 1000.0);
      return true;
    } catch (const std::exception& error) {
      REXLOG_ERROR("LSFG presentation failed after replacing output: {}", error.what());
      result = DXGI_ERROR_DEVICE_RESET;
      return true;
    }
  }
 private:
  void CreateConversion(D3D12_RESOURCE_DESC desc) {
    format_ = desc.Format;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&original_)), "LSFG original-frame backup");
    D3D12_DESCRIPTOR_HEAP_DESC views{};
    views.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    views.NumDescriptors = 4; views.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(device_->CreateDescriptorHeap(&views, IID_PPV_ARGS(&views_)), "LSFG view heap");
    stride_ = device_->GetDescriptorHandleIncrementSize(views.Type);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = format_; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(original_.Get(), &srv, Cpu(0));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM; uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(input_->d3d(), nullptr, &uav, Cpu(1));
    srv.Format = uav.Format;
    device_->CreateShaderResourceView(output_->d3d(), &srv, Cpu(2));
    device_->CreateUnorderedAccessView(input_->d3d(), nullptr, &uav, Cpu(3));
    D3D12_DESCRIPTOR_HEAP_DESC rtv{};
    rtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rtv.NumDescriptors = 1;
    Check(device_->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&rtv_)), "LSFG RTV heap");
    std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges{};
    ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1};
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable = {UINT(ranges.size()), ranges.data()};
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC root{};
    root.NumParameters = 1; root.pParameters = &parameter;
    ComPtr<ID3DBlob> serialized, errors;
    Check(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized, &errors), "LSFG root signature serialization");
    Check(device_->CreateRootSignature(0, serialized->GetBufferPointer(),
        serialized->GetBufferSize(), IID_PPV_ARGS(&root_)), "LSFG root signature");
    auto compute = Compile(kConversion, "capture", "cs_5_1");
    D3D12_COMPUTE_PIPELINE_STATE_DESC cp{};
    cp.pRootSignature = root_.Get(); cp.CS = {compute->GetBufferPointer(), compute->GetBufferSize()};
    Check(device_->CreateComputePipelineState(&cp, IID_PPV_ARGS(&capture_)), "LSFG capture pipeline");
    auto vertex = Compile(kConversion, "vertex", "vs_5_1");
    auto pixel = Compile(kConversion, "pixel", "ps_5_1");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gp{};
    gp.pRootSignature = root_.Get();
    gp.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    gp.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    gp.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    gp.SampleMask = UINT_MAX; gp.SampleDesc.Count = 1;
    gp.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    gp.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    gp.RasterizerState.DepthClipEnable = TRUE;
    gp.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    gp.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gp.NumRenderTargets = 1; gp.RTVFormats[0] = format_;
    Check(device_->CreateGraphicsPipelineState(&gp, IID_PPV_ARGS(&draw_)), "LSFG generated-frame pipeline");
  }
  D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT index) const {
    auto handle = views_->GetCPUDescriptorHandleForHeapStart(); handle.ptr += SIZE_T(index) * stride_; return handle;
  }
  D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT index) const {
    auto handle = views_->GetGPUDescriptorHandleForHeapStart(); handle.ptr += UINT64(index) * stride_; return handle;
  }
  void Begin() {
    Check(allocator_->Reset(), "LSFG allocator reset");
    Check(commands_->Reset(allocator_.Get(), nullptr), "LSFG command-list reset");
  }
  void Submit() {
    Check(commands_->Close(), "LSFG close command list");
    ID3D12CommandList* list = commands_.Get(); queue_->ExecuteCommandLists(1, &list);
  }
  void Wait() {
    if (!completion_ || !event_) return;
    Check(queue_->Signal(completion_.Get(), ++completion_value_), "LSFG completion signal");
    Check(completion_->SetEventOnCompletion(completion_value_, event_), "LSFG completion event");
    if (WaitForSingleObject(event_, 30000) != WAIT_OBJECT_0) throw std::runtime_error("LSFG D3D12 timeout");
    Check(device_->GetDeviceRemovedReason(), "LSFG D3D12 device status");
  }
  void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
    commands_->ResourceBarrier(1, &barrier);
  }
  void Capture(ID3D12Resource* buffer) {
    Begin();
    Transition(buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(original_.Get(), original_state_, D3D12_RESOURCE_STATE_COPY_DEST);
    commands_->CopyResource(original_.Get(), buffer);
    Transition(buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    Transition(original_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(input_->d3d(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap* heap = views_.Get(); commands_->SetDescriptorHeaps(1, &heap);
    commands_->SetComputeRootSignature(root_.Get()); commands_->SetPipelineState(capture_.Get());
    commands_->SetComputeRootDescriptorTable(0, Gpu(0));
    commands_->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
    Transition(input_->d3d(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    Transition(original_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    original_state_ = D3D12_RESOURCE_STATE_COPY_SOURCE;
    Submit(); Check(queue_->Signal(sync_->d3d(), ++sync_value_), "LSFG capture fence");
  }
  void Generate(bool warmup) {
    const auto& vk = backend_->vulkan();
    vk_done_->reset(vk); vk_command_->begin(vk);
    vk_command_->insertBarriers(vk, {Barrier(input_->image(), VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, VK_ACCESS_TRANSFER_READ_BIT,
        VK_QUEUE_FAMILY_EXTERNAL, vk.queuefamily())});
    for (size_t i = 0; i < (warmup ? 2 : 1); ++i) {
      VkImage source = backend_->sourceImage(*context_, warmup ? i : phase_ % 2);
      vk_command_->copyImage(vk, {Barrier(source,
          warmup ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          warmup ? 0 : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT)},
          {input_->image(), source}, {width_, height_}, {Barrier(source,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)});
    }
    vk_command_->insertBarriers(vk, {Barrier(input_->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT, 0,
        vk.queuefamily(), VK_QUEUE_FAMILY_EXTERNAL)});
    for (size_t i = 0; i < (warmup ? 4 : 1); ++i) backend_->recordFrame(*context_, *vk_command_);
    if (!warmup) {
      ++phase_;
      VkImage generated = backend_->destinationImage(*context_, 0);
      vk_command_->copyImage(vk, {
          Barrier(generated, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT),
          Barrier(output_->image(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_QUEUE_FAMILY_EXTERNAL, vk.queuefamily())},
          {generated, output_->image()}, {width_, height_}, {
          Barrier(generated, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
              VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT),
          Barrier(output_->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
              VK_ACCESS_TRANSFER_WRITE_BIT, 0, vk.queuefamily(), VK_QUEUE_FAMILY_EXTERNAL)});
    }
    vk_command_->end(vk);
    uint64_t wait_value = sync_value_++;
    vk_command_->submit(vk, {}, sync_->semaphore(), wait_value, {},
        sync_->semaphore(), sync_value_, vk_done_->handle());
    if (!vk_done_->wait(vk, 30'000'000'000ULL)) throw std::runtime_error("LSFG Vulkan timeout");
    Check(queue_->Wait(sync_->d3d(), sync_value_), "LSFG return fence"); Wait();
  }
  void DrawGenerated(ID3D12Resource* buffer) {
    Begin();
    Transition(output_->d3d(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rtv = rtv_->GetCPUDescriptorHandleForHeapStart();
    device_->CreateRenderTargetView(buffer, nullptr, rtv);
    commands_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT viewport{0, 0, float(width_), float(height_), 0, 1};
    D3D12_RECT scissor{0, 0, LONG(width_), LONG(height_)};
    commands_->RSSetViewports(1, &viewport); commands_->RSSetScissorRects(1, &scissor);
    ID3D12DescriptorHeap* heap = views_.Get(); commands_->SetDescriptorHeaps(1, &heap);
    commands_->SetGraphicsRootSignature(root_.Get()); commands_->SetPipelineState(draw_.Get());
    commands_->SetGraphicsRootDescriptorTable(0, Gpu(2));
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands_->DrawInstanced(3, 1, 0, 0);
    Transition(buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    Transition(output_->d3d(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    Submit(); Wait();
  }
  void RestoreOriginal(ID3D12Resource* buffer) {
    Begin(); Transition(buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    commands_->CopyResource(buffer, original_.Get());
    Transition(buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    Submit(); Wait();
  }
  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  uint32_t width_, height_;
  DXGI_FORMAT format_{};
  IDXGISwapChain3* swapchain_ = nullptr;
  // Declared first so the backend device is destroyed last.
  std::unique_ptr<lsfgvk::backend::Instance> backend_;
  lsfgvk::backend::Context* context_ = nullptr;
  std::unique_ptr<SharedImage> input_, output_;
  std::unique_ptr<SharedFence> sync_;
  std::unique_ptr<vk::CommandBuffer> vk_command_;
  std::unique_ptr<vk::Fence> vk_done_;
  ComPtr<ID3D12CommandAllocator> allocator_;
  ComPtr<ID3D12GraphicsCommandList> commands_;
  ComPtr<ID3D12Fence> completion_;
  HANDLE event_ = nullptr;
  uint64_t completion_value_ = 0, sync_value_ = 0, phase_ = 0;
  ComPtr<ID3D12Resource> original_;
  D3D12_RESOURCE_STATES original_state_ = D3D12_RESOURCE_STATE_COMMON;
  ComPtr<ID3D12DescriptorHeap> views_, rtv_;
  UINT stride_ = 0;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12PipelineState> capture_, draw_;
  uint64_t last_serial_ = UINT64_MAX, real_frames_ = 0, generated_frames_ = 0;
  bool initialized_ = false;
};

struct State {
  std::unique_ptr<Session> session; // Accessed only by serialized paint calls.
  bool disabled = false;
  uint64_t serial = UINT64_MAX;
  Clock::time_point arrival{};
  double interval = 1.0 / 30.0;
};
std::mutex states_mutex;
std::unordered_map<void*, std::shared_ptr<State>> states;
std::shared_ptr<State> GetState(void* key) {
  std::lock_guard lock(states_mutex);
  auto& state = states[key];
  if (!state) state = std::make_shared<State>();
  return state;
}
}
void NotifySourceFrame(void* presenter, uint64_t serial) {
  if (!Enabled()) return;
  auto state = GetState(presenter);
  auto now = Clock::now();
  std::lock_guard lock(states_mutex);
  if (state->serial != UINT64_MAX && state->serial != serial) {
    double dt = std::chrono::duration<double>(now - state->arrival).count();
    if (dt > 0.002 && dt < 0.2) state->interval = state->interval * 0.8 + dt * 0.2;
  }
  state->arrival = now; state->serial = serial;
}
void ForgetPresenter(void* presenter) {
  std::shared_ptr<State> removed;
  { std::lock_guard lock(states_mutex);
    auto found = states.find(presenter);
    if (found != states.end()) { removed = std::move(found->second); states.erase(found); }
  }
}
bool TryPresent(void* presenter, ID3D12Device* device, ID3D12CommandQueue* queue,
                IDXGISwapChain3* swapchain, ID3D12Resource* backbuffer,
                uint64_t serial, bool active, UINT flags, HRESULT& result) {
  if (!Enabled()) return false;
  auto state = GetState(presenter);
  if (state->disabled) return false;
  auto desc = backbuffer->GetDesc();
  if (!active || desc.Width < 256 || desc.Height < 256) {
    state->session.reset(); return false;
  }
  try {
    if (!state->session || !state->session->Matches(device, queue, swapchain, desc)) {
      state->session.reset();
      state->session = std::make_unique<Session>(device, queue, desc);
      state->session->Attach(swapchain);
    }
    double interval;
    { std::lock_guard lock(states_mutex); interval = state->interval; }
    return state->session->Present(swapchain, backbuffer, serial, interval, flags, result);
  } catch (const std::exception& error) {
    REXLOG_WARN("LSFG disabled; normal presentation restored: {}", error.what());
    state->session.reset(); state->disabled = true;
    return false;
  }
}
}
