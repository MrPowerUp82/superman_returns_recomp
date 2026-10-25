#include "temporal_geometry_history.h"
#include "static_world_address_cs.h"
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
using superman_returns::graphics::CaptureStaticWorldDependenciesInVertexRange;
namespace {
constexpr uint64_t kShader = 0x2C45BFFE76D77398ull;
constexpr uint32_t kLast = 0xFFFFFFu, kAbsent = UINT32_MAX;
constexpr size_t kMaxSamples = 1024 * 1024;
using Words = std::array<uint32_t, 1024>;
using Capture = std::array<uint32_t, 636>;
struct Input { uint32_t id, stride; };
struct Output { uint32_t first, second, address, reciprocal; };
static_assert(sizeof(Input) == 8 && sizeof(Output) == 16);

void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}
void Check(HRESULT hr, const char* operation) {
  if (FAILED(hr)) {
    std::ostringstream text;
    text << operation << " HRESULT=0x" << std::hex << uint32_t(hr);
    throw std::runtime_error(text.str());
  }
}
Words Constants(float stride) {
  Words words{};
  for (uint32_t row = 0; row < 256; ++row) {
    for (uint32_t component = 0; component < 3; ++component)
      words[row * 4 + component] = std::bit_cast<uint32_t>(float(row * 4 + component + 1));
    words[row * 4 + 3] = std::bit_cast<uint32_t>(float(row + 1));
  }
  words[16] = std::bit_cast<uint32_t>(stride);
  words[17] = 0;
  return words;
}

void AuditDXIL(const char* path) {
  std::ifstream file(path);
  Require(bool(file), "DXIL assembly argument is required (run via CTest)");
  std::string line;
  size_t divisions = 0;
  while (std::getline(file, line)) {
    if (line.find(" = fdiv ") == std::string::npos) continue;
    // Reject fast/reassoc/arcp and any change to the numerator or scalar type.
    Require(line.find(" = fdiv float 1.000000e+00, ") != std::string::npos,
            "fixture lost precise scalar fdiv 1.0,stride: " + line);
    ++divisions;
  }
  Require(divisions == 1, "fixture must contain exactly one precise fdiv 1.0,stride");
  std::cout << "DXIL contract: precise scalar fdiv 1.0,stride verified; "
               "no general rcp-intrinsic accuracy claim\n";
}

size_t CPURejections() {
  size_t checked = 0;
  auto reject = [&](const Words& words, uint32_t first, uint32_t last,
                    const char* label, uint64_t shader = kShader) {
    Capture capture;
    capture.fill(0xdeadbeefu);
    const Capture untouched = capture;
    Require(!CaptureStaticWorldDependenciesInVertexRange(shader, words, first, last, capture),
            std::string("CPU accepted invalid input: ") + label);
    Require(capture == untouched, std::string("CPU modified rejected capture: ") + label);
    ++checked;
  };
  const float inf = std::numeric_limits<float>::infinity();
  for (float stride : {0.f, -0.f, -1.f, .5f, std::nextafter(1.f, 0.f), inf, -inf,
                       std::numeric_limits<float>::quiet_NaN(), std::nextafter(16777216.f, inf)})
    reject(Constants(stride), 0, 1, "invalid stride");
  for (float value : {inf, -inf, std::numeric_limits<float>::quiet_NaN()}) {
    auto words = Constants(1.f);
    words[17] = std::bit_cast<uint32_t>(value);
    reject(words, 0, 0, "nonfinite remap");
    for (uint32_t row : {100u, 101u}) {
      for (uint32_t component = 0; component < 3; ++component) {
        words = Constants(1.f);
        words[row * 4 + component] = std::bit_cast<uint32_t>(value);
        reject(words, 0, 0, "nonfinite reachable matrix component");
      }
      words = Constants(1.f);
      Capture baseline{}, changed{};
      Require(CaptureStaticWorldDependenciesInVertexRange(kShader, words, 0, 0, baseline),
              "baseline position capture rejected");
      words[row * 4 + 3] = std::bit_cast<uint32_t>(value);
      Require(CaptureStaticWorldDependenciesInVertexRange(kShader, words, 0, 0, changed) && changed == baseline,
              "unused lighting W component changed position signature");
    }
  }
  reject(Constants(1.f), 2, 1, "reversed interval");
  reject(Constants(1.f), 0, 0x1000000u, "24-bit wrap");
  reject(Constants(1.f), 0x1000000u, 0x1000000u, "masked high IDs");
  reject(Constants(1.f), 0, UINT32_MAX, "32-bit wrap");
  reject(Constants(1.f), 0, 0, "unaudited shader", 0);
  auto words = Constants(100.5f);
  Capture boundary{};
  Require(CaptureStaticWorldDependenciesInVertexRange(kShader, words, 100, 100, boundary),
          "fractional instance boundary rejected");
  // Hand-checked: numerator==stride. Conservative bounds must admit instances
  // zero AND one, even if this GPU chooses only one side of the fdiv boundary.
  Require(boundary[2] <= 100 && boundary[3] >= 103,
          "fractional boundary lost conservative adjacent-instance dependencies");
  // An unread nonfinite row must not poison a precise narrow capture.
  words = Constants(1.f);
  words[250 * 4] = std::bit_cast<uint32_t>(inf);
  Require(CaptureStaticWorldDependenciesInVertexRange(kShader, words, 0, 0, boundary),
          "unread nonfinite matrix row rejected");
  std::cout << "CPU: " << checked << " invalid-input rejections, "
               "fractional two-instance envelope and unread-row checks passed\n";
  return checked;
}

struct Interval {
  float stride;
  uint32_t first, last;
  size_t offset, count;
  Capture capture;
};
struct Matrix {
  std::vector<Interval> intervals;
  std::vector<Input> samples;
  std::mt19937 random{0x2c45bffeu};
  size_t exhaustive = 0, sampled = 0;

  void Add(float stride, uint32_t first, uint32_t last) {
    Require(first <= last && last <= kLast, "invalid test interval");
    Interval interval{stride, first, last, samples.size(), 0, {}};
    const auto words = Constants(stride);
    Require(CaptureStaticWorldDependenciesInVertexRange(kShader, words, first, last, interval.capture),
            "helper rejected finite valid interval");
    std::vector<uint32_t> ids{first, last}; // Always test BOTH exact endpoints.
    if (last - first <= 256) {
      for (uint32_t id = first; id <= last; ++id) ids.push_back(id);
      ++exhaustive;
    } else {
      ids.push_back(first + 1);
      ids.push_back(last - 1);
      ids.push_back(first + (last - first) / 2);
      std::uniform_int_distribution<uint32_t> middle(first + 1, last - 1);
      for (size_t i = 0; i < 64; ++i) ids.push_back(middle(random));
      ++sampled;
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    Require(samples.size() + ids.size() <= kMaxSamples, "bounded sample budget exceeded");
    for (uint32_t id : ids) samples.push_back({id, std::bit_cast<uint32_t>(stride)});
    interval.count = ids.size();
    intervals.push_back(interval);
  }

  Matrix() {
    std::vector<float> strides;
    for (uint32_t stride = 1; stride <= 256; ++stride) strides.push_back(float(stride));
    for (uint32_t power = 0; power <= 24; ++power) {
      const float stride = std::ldexp(1.f, int(power));
      strides.push_back(stride);
      if (power) strides.push_back(std::nextafter(stride, 0.f));
      if (power < 24) strides.push_back(std::nextafter(stride, 16777216.f));
    }
    for (float stride : {1.5f, 2.5f, 3.25f, 77.5f, 100.5f, 255.5f, 65535.5f, 8388607.5f})
      strides.push_back(stride);
    // Reproducible representatives across the complete supported exponent range.
    for (size_t i = 0; i < 96; ++i) {
      const uint32_t power = random() % 24;
      const float scale = 1.f + float(random() % 65536) / 65536.f;
      strides.push_back(std::ldexp(scale, int(power)));
    }
    std::sort(strides.begin(), strides.end());
    strides.erase(std::unique(strides.begin(), strides.end()), strides.end());
    for (float stride : strides) {
      Add(stride, 0, 256);
      Add(stride, 0, kLast);
      Add(stride, kLast - 256, kLast); // float(id)+.5 tie/rounding near 24 bits
      Add(stride, 0x7fffffu - 4, 0x800000u + 4);
      for (uint32_t instance : {1u, 2u, 3u, 76u, 77u, 78u, 79u, 127u, 128u}) {
        const double boundary = double(stride) * instance - .5;
        if (boundary < 0 || boundary > kLast) continue;
        const uint32_t center = uint32_t(std::floor(boundary));
        const uint32_t first = center > 4 ? center - 4 : 0;
        const uint32_t last = std::min(kLast, center + 5);
        Add(stride, first, last);
        // Singleton endpoints prevent a broad interval from hiding an off-by-one.
        for (uint32_t id = first; id <= last; ++id) Add(stride, id, id);
      }
    }
    // Literal address witnesses. No CPU address arithmetic supplies expected rows.
    for (uint32_t id : {0u, 1u, 77u, 78u, 127u, 128u, kLast}) Add(1.f, id, id);
    Add(100.5f, 100, 100);
    std::cout << "Matrix: " << strides.size() << " strides [1,2^24], "
              << exhaustive << " exhaustive small intervals, " << sampled
              << " sampled large intervals, " << samples.size() << " GPU vertices (cap "
              << kMaxSamples << ")\n";
  }
};

struct GPU {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence;
  ComPtr<ID3D12InfoQueue> info;

  GPU() {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
      debug->EnableDebugLayer();
      std::cout << "D3D12 debug layer enabled\n";
    } else std::cout << "D3D12 debug layer unavailable; hardware assertions enabled\n";
    ComPtr<IDXGIFactory6> factory;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
    for (UINT index = 0;; ++index) {
      ComPtr<IDXGIAdapter1> adapter;
      const HRESULT hr = factory->EnumAdapterByGpuPreference(index,
          DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
      if (hr == DXGI_ERROR_NOT_FOUND) break;
      Check(hr, "enumerate adapters");
      DXGI_ADAPTER_DESC1 desc{};
      Check(adapter->GetDesc1(&desc), "adapter description");
      if (desc.VendorId != 0x10de || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ||
          std::wstring(desc.Description).find(L"RTX 2060") == std::wstring::npos) continue;
      Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                             IID_PPV_ARGS(&device)), "RTX 2060 device");
      std::wcout << L"Hardware: " << desc.Description << L'\n';
      break;
    }
    Require(bool(device), "RTX 2060 hardware required; no WARP or skip");
    (void)device.As(&info);
    D3D12_COMMAND_QUEUE_DESC desc{};
    Check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "queue");
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&allocator)), "allocator");
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                    nullptr, IID_PPV_ARGS(&list)), "command list");
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
  }

  ComPtr<ID3D12Resource> Buffer(uint64_t bytes, D3D12_HEAP_TYPE type,
                              D3D12_RESOURCE_STATES state,
                              D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    ComPtr<ID3D12Resource> buffer;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state,
                                          nullptr, IID_PPV_ARGS(&buffer)), "buffer");
    return buffer;
  }
  void Fill(ID3D12Resource* buffer, const void* data, size_t bytes) {
    void* mapped = nullptr;
    D3D12_RANGE none{0, 0};
    Check(buffer->Map(0, &none, &mapped), "upload map");
    std::memcpy(mapped, data, bytes);
    buffer->Unmap(0, nullptr);
  }
  void Submit() {
    Check(list->Close(), "close command list");
    ID3D12CommandList* commands[]{list.Get()};
    queue->ExecuteCommandLists(1, commands);
    Check(queue->Signal(fence.Get(), 1), "signal fence");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Require(event != nullptr, "create fence event");
    const HRESULT armed = fence->SetEventOnCompletion(1, event);
    if (FAILED(armed)) { CloseHandle(event); Check(armed, "arm fence event"); }
    const DWORD waited = WaitForSingleObject(event, 30000);
    CloseHandle(event);
    Require(waited == WAIT_OBJECT_0, "bounded 30-second GPU fence wait failed");
    Check(device->GetDeviceRemovedReason(), "device removed");
    if (info) {
      bool clean = true;
      for (UINT64 index = 0; index < info->GetNumStoredMessages(); ++index) {
        SIZE_T bytes = 0;
        Check(info->GetMessage(index, nullptr, &bytes), "debug message size");
        std::vector<char> storage(bytes);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        Check(info->GetMessage(index, message, &bytes), "debug message");
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
          std::cerr << message->pDescription << '\n';
          clean = false;
        }
      }
      Require(clean, "D3D12 debug validation reported errors");
    }
  }

  std::vector<Output> Run(const std::vector<Input>& samples) {
    auto constants = Buffer(sizeof(Words), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    const auto words = Constants(1.f); // stride comes from each input, row tags are invariant
    Fill(constants.Get(), words.data(), sizeof(words));
    auto inputs = Buffer(samples.size() * sizeof(Input), D3D12_HEAP_TYPE_UPLOAD,
                         D3D12_RESOURCE_STATE_GENERIC_READ);
    Fill(inputs.Get(), samples.data(), samples.size() * sizeof(Input));
    const size_t output_bytes = samples.size() * sizeof(Output);
    auto output = Buffer(output_bytes, D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto readback = Buffer(output_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    std::array<D3D12_ROOT_PARAMETER, 4> params{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[3].Constants.ShaderRegister = 1;
    params[3].Constants.Num32BitValues = 1;
    D3D12_ROOT_SIGNATURE_DESC signature{};
    signature.NumParameters = UINT(params.size());
    signature.pParameters = params.data();
    ComPtr<ID3DBlob> blob, errors;
    Check(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1,
                                      &blob, &errors), "serialize root signature");
    ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                       IID_PPV_ARGS(&root)), "root signature");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = root.Get();
    pipeline.CS = {kStaticWorldAddressCS, sizeof(kStaticWorldAddressCS)};
    ComPtr<ID3D12PipelineState> pso;
    Check(device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&pso)), "address compute PSO");
    list->SetComputeRootSignature(root.Get());
    list->SetPipelineState(pso.Get());
    list->SetComputeRootConstantBufferView(0, constants->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(1, inputs->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(2, output->GetGPUVirtualAddress());
    list->SetComputeRoot32BitConstant(3, UINT(samples.size()), 0);
    list->Dispatch(UINT((samples.size() + 63) / 64), 1, 1);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {output.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &barrier);
    list->CopyBufferRegion(readback.Get(), 0, output.Get(), 0, output_bytes);
    Submit();
    void* mapped = nullptr;
    D3D12_RANGE range{0, output_bytes};
    Check(readback->Map(0, &range, &mapped), "readback map");
    std::vector<Output> results(samples.size());
    std::memcpy(results.data(), mapped, output_bytes);
    D3D12_RANGE none{0, 0};
    readback->Unmap(0, &none);
    return results;
  }
};

void Verify(const Matrix& matrix, const std::vector<Output>& results, bool negative_control) {
  size_t reads = 0, absent = 0, failures = 0, literal_witnesses = 0;
  for (const auto& interval : matrix.intervals) {
    const auto words = Constants(interval.stride);
    for (size_t index = interval.offset; index < interval.offset + interval.count; ++index) {
      const auto& actual = results[index];
      const auto& input = matrix.samples[index];
      Capture singleton{};
      Require(CaptureStaticWorldDependenciesInVertexRange(kShader, words, input.id, input.id, singleton),
              "helper rejected valid sampled singleton");
      for (uint32_t row : {actual.first, actual.second}) {
        if (row == kAbsent) { ++absent; continue; }
        ++reads;
        // Optional harness negative control: deliberately empty a returned
        // envelope locally. Never change the production helper to test the test.
        const uint32_t first = negative_control ? 256u : interval.capture[2];
        const uint32_t last = negative_control ? 255u : interval.capture[3];
        bool included = row < 256 && first <= row && row <= last &&
                        singleton[2] <= row && row <= singleton[3];
        if (included) {
          for (uint32_t component = 0; component < 3; ++component)
            included = included && interval.capture[4 + (row - 98) * 4 + component] == words[row * 4 + component]
                                && singleton[4 + (row - 98) * 4 + component] == words[row * 4 + component];
        }
        if (!included) {
          if (failures++ < 12)
            std::cerr << "COVERAGE FAILURE stride=" << interval.stride << " id=" << input.id
                      << " interval=[" << interval.first << ',' << interval.last << "] GPU row=" << row
                      << " a0=" << actual.address << " reciprocal_bits=0x" << std::hex << actual.reciprocal
                      << std::dec << " helper=[" << first << ',' << last << "] singleton=["
                      << singleton[2] << ',' << singleton[3] << "]\n";
        }
      }
      if (interval.stride == 1.f) {
        // Independent literals check row tags, truncation, matrix-table edge,
        // address-register saturation and zero selection for out-of-range reads.
        uint32_t first = kAbsent, second = kAbsent, address = 0;
        bool witness = true;
        switch (input.id) {
          case 0: first = 100; second = 101; address = 0; break;
          case 1: first = 102; second = 103; address = 2; break;
          case 77: first = 254; second = 255; address = 154; break;
          case 78: address = 156; break;
          case 127: address = 254; break;
          case 128: case kLast: address = 255; break;
          default: witness = false;
        }
        if (witness) {
          Require(actual.first == first && actual.second == second && actual.address == address,
                  "hand-checked GPU row/address witness failed at vertex " + std::to_string(input.id));
          ++literal_witnesses;
        }
      }
    }
  }
  Require(reads > 0 && absent > 0 && literal_witnesses >= 7, "vacuous GPU coverage matrix");
  Require(failures == 0, std::to_string(failures) + " GPU reads outside helper envelope/captured values");
  std::cout << "PASS: " << results.size() << " GPU vertices, " << reads << " included row reads, "
            << absent << " zero-selected out-of-range reads, " << literal_witnesses
            << " literal GPU witnesses; interval AND singleton captures cover every observed row\n";
}
} // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 2 || (argc == 3 && std::string(argv[2]) == "--negative-control"),
            "usage: static_world_address_smoke <generated-dxil.ll> [--negative-control]");
    AuditDXIL(argv[1]);
    (void)CPURejections();
    const Matrix matrix;
    GPU gpu;
    Verify(matrix, gpu.Run(matrix.samples), argc == 3);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
