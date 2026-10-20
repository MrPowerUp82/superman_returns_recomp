#include "../native/test_main.h"
#include "../../tools/rexglue-write-tracking/include/rex_memory_tracking_abi.h"
#include "../../tools/rexglue-write-tracking/src/runtime_memory_tracker.h"
#include <WinSock2.h>
#include <rex/runtime.h>
#include <rex/system/xfile.h>
#include <rex/system/xsocket.h>
#include <rex/system/util/object_table.h>
#include <windows.h>
#include <array>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>
#include <atomic>

using rex::X_STATUS;

SR_TEST(runtime_preexisting_host_write_blocks_session_installation) {
  rex::memory::Memory memory;
  SR_CHECK(memory.Initialize());
  auto getter = reinterpret_cast<RexGetMemoryTrackingApiFn>(
      GetProcAddress(GetModuleHandleW(L"rexruntime.dll"), "RexGetMemoryTrackingApi"));
  SR_CHECK(getter != nullptr); if (!getter) return;
  const auto* api = getter(1, sizeof(RexMemoryTrackingApiV1));
  RexTrackingSession session = 0;
  {
    sr::tracking::HostWriteScope pending(memory, 0x04000000u, 4096u);
    SR_CHECK(api->start(&memory, &session) == RexTrackingStatus::Unavailable);
    SR_CHECK_EQ(session, 0u);
  }
  SR_CHECK(api->start(&memory, &session) == RexTrackingStatus::Ok);
  api->stop(session);
}

SR_TEST(runtime_active_host_scope_retains_metadata_after_stop) {
  rex::memory::Memory memory;
  SR_CHECK(memory.Initialize());
  auto getter = reinterpret_cast<RexGetMemoryTrackingApiFn>(
      GetProcAddress(GetModuleHandleW(L"rexruntime.dll"), "RexGetMemoryTrackingApi"));
  const auto* api = getter(1, sizeof(RexMemoryTrackingApiV1));
  RexTrackingSession session = 0;
  SR_CHECK(api->start(&memory, &session) == RexTrackingStatus::Ok);
  SR_CHECK(memory.LookupHeapByType(true, 65536)->AllocFixed(0xA4000000, 65536, 65536,
      rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
      rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite));
  const RexTrackingRange range{0x04000000, 4096};
  RexTrackingWatch watch = 0;
  SR_CHECK(api->watch(session, &range, 1, &watch) == RexTrackingStatus::Ok);
  {
    sr::tracking::HostWriteScope pending(memory, 0x04000000u, 4096u);
    api->stop(session);
    RexTrackingSession next = 0;
    SR_CHECK(api->start(&memory, &next) == RexTrackingStatus::Unavailable);
    *memory.TranslatePhysical<volatile uint8_t*>(0x04000000) = 49;
  }
  SR_CHECK(api->start(&memory, &session) == RexTrackingStatus::Ok);
  api->stop(session);
}

int RunTrackingFailureCase(int failure) {
  SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
  const bool lost_contract = failure == 1;
  rex::InitLogging();
  rex::memory::Memory memory;
  auto getter = reinterpret_cast<RexGetMemoryTrackingApiFn>(
      GetProcAddress(GetModuleHandleW(L"rexruntime.dll"), "RexGetMemoryTrackingApi"));
  const auto* api = getter(1, sizeof(RexMemoryTrackingApiV1));
  if (failure == 2) {
    RexTrackingSession uninitialized = 123;
    SR_CHECK(api->start(&memory, &uninitialized) == RexTrackingStatus::Unavailable);
    SR_CHECK_EQ(uninitialized, 0u);
    if (uninitialized) api->stop(uninitialized);
    return sr_test::Failures() ? 1 : 0;
  }
  SR_CHECK(memory.Initialize());
  RexTrackingSession session = 0;
  SR_CHECK(api->start(&memory, &session) == RexTrackingStatus::Ok);
  SR_CHECK(memory.LookupHeapByType(true, 65536)->AllocFixed(0xA4000000, 65536, 65536,
      rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
      rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite));
  if (!lost_contract) SR_CHECK(UnmapViewOfFile(memory.virtual_membase() + 0xA0000000ull) != 0);
  const RexTrackingRange range{0x04000000, 4096};
  RexTrackingWatch watch = 0;
  SR_CHECK(api->watch(session, &range, 1, &watch) == RexTrackingStatus::Ok);
  RexTrackingStamp stamp{}; api->sample(session, watch, &stamp, 1);
  if (lost_contract) {
    SR_CHECK(stamp.coverage == RexTrackingCoverage::Complete);
    {
      // An unrepresentable producer range cannot be registered safely. This
      // must poison coverage without relying on a hash comparison.
      sr::tracking::HostWriteScope unknown(memory, memory.physical_membase(), SIZE_MAX);
      api->sample(session, watch, &stamp, 1);
      SR_CHECK(stamp.coverage != RexTrackingCoverage::Complete);
    }
  } else SR_CHECK(stamp.coverage != RexTrackingCoverage::Complete);
  api->unwatch(session, watch); api->stop(session);
  if (lost_contract) {
    RexTrackingSession retry = 0;
    SR_CHECK(api->start(&memory, &retry) == RexTrackingStatus::Unavailable);
    if (retry) api->stop(retry);
  }
  return sr_test::Failures() ? 1 : 0;
}

SR_TEST(runtime_network_outputs_invalidate_covered_memory) {
  WSADATA wsa{};
  SR_CHECK_EQ(WSAStartup(MAKEWORD(2, 2), &wsa), 0);
  for (int scenario = 0; scenario < 3; ++scenario) {
    const auto root = std::filesystem::current_path() / "tracking-network-fixture";
    std::filesystem::create_directories(root);
    rex::Runtime runtime(root, root / "user", {}, root / "cache", root / "metadata");
    rex::RuntimeConfig config; config.tool_mode = true;
    SR_CHECK(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS);
    auto& memory = *runtime.memory();
    auto getter = reinterpret_cast<RexGetMemoryTrackingApiFn>(
        GetProcAddress(GetModuleHandleW(L"rexruntime.dll"), "RexGetMemoryTrackingApi"));
    SR_CHECK(getter != nullptr); if (!getter) { WSACleanup(); return; }
    const auto* api = getter(1, sizeof(RexMemoryTrackingApiV1));
    RexTrackingSession session = 0;
    SR_CHECK(api->start(&memory, &session) == RexTrackingStatus::Ok);
    SR_CHECK(memory.LookupHeapByType(true, 65536)->AllocFixed(0xA4000000, 65536, 65536,
        rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
        rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite));
    unsigned legacy_notifications = 0;
    auto* legacy_handle = memory.RegisterPhysicalMemoryInvalidationCallback(
        [](void* context, uint32_t address, uint32_t length, bool) {
          ++*static_cast<unsigned*>(context);
          return std::pair<uint32_t, uint32_t>{address, length};
        }, &legacy_notifications);
    memory.EnablePhysicalMemoryAccessCallbacks(0x04000000, 8192, true, false);
    const RexTrackingRange range{0x04000000, 4096};
    RexTrackingWatch watch = 0;
    SR_CHECK(api->watch(session, &range, 1, &watch) == RexTrackingStatus::Ok);
    RexTrackingStamp before{}; api->sample(session, watch, &before, 1);
    SR_CHECK(before.coverage == RexTrackingCoverage::Complete);
    auto socket = rex::system::object_ref<rex::system::XSocket>(new rex::system::XSocket(runtime.kernel_state()));
    SR_CHECK(socket->Initialize(rex::system::XSocket::X_AF_INET, rex::system::XSocket::X_SOCK_DGRAM,
                               rex::system::XSocket::X_IPPROTO_UDP) == X_STATUS_SUCCESS);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const SOCKET receiver = SOCKET(socket->native_handle());
    SR_CHECK_EQ(bind(receiver, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    int address_size = sizeof(address);
    SR_CHECK_EQ(getsockname(receiver, reinterpret_cast<sockaddr*>(&address), &address_size), 0);
    const DWORD timeout = 2000;
    setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    const SOCKET sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const char payload[] = "tracking";
    auto send = [&] { SR_CHECK_EQ(sendto(sender, payload, 8, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 8); };
    auto* target = memory.TranslateVirtual<uint8_t*>(0xA4000000);
    if (scenario == 2) {
      send();
      SR_CHECK(socket->IOControl(FIONREAD, target) == X_STATUS_SUCCESS);
      SR_CHECK_EQ(*reinterpret_cast<const uint32_t*>(target), 8u);
    } else {
      std::atomic<bool> done = false;
      int received = -1;
      std::thread worker([&] {
        received = scenario == 0 ? socket->Recv(target, 8, 0) : socket->RecvFrom(target, 8, 0, nullptr, nullptr);
        done.store(true, std::memory_order_release);
      });
      bool saw_active = false;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      while (!done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        RexTrackingStamp during{}; api->sample(session, watch, &during, 1);
        if (during.active_writers) { saw_active = true; break; }
        std::this_thread::yield();
      }
      SR_CHECK(saw_active);
      SR_CHECK_EQ(legacy_notifications, 0u);
      send(); worker.join();
      SR_CHECK_EQ(received, 8);
      for (size_t i = 0; i < 8; ++i) SR_CHECK_EQ(target[i], uint8_t(payload[i]));
    }
    RexTrackingStamp after{}; api->sample(session, watch, &after, 1);
    SR_CHECK(after.invalidation_epoch > before.invalidation_epoch);
    SR_CHECK(after.coverage != RexTrackingCoverage::Complete);
    SR_CHECK_EQ(after.active_writers, 0u);
    SR_CHECK(legacy_notifications > 0);
    memory.UnregisterPhysicalMemoryInvalidationCallback(legacy_handle);
    api->unwatch(session, watch); api->stop(session);
    runtime.kernel_state()->object_table()->RemoveHandle(socket->handle()); socket.reset();
    closesocket(sender);
  }
  WSACleanup();
}

namespace {
struct ReadState {
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, resume = false;
  int failure = 0;
  HANDLE handle = INVALID_HANDLE_VALUE;
};
// Control the slow host operation, retaining the real XFile, guest mappings,
// allocation lifecycle and (for success) kernel ReadFile into guest memory.
class ControlledFile final : public rex::filesystem::File {
 public:
  explicit ControlledFile(std::shared_ptr<ReadState> state) : File(0, nullptr), state_(std::move(state)) {}
  void Destroy() override { delete this; }
  X_STATUS ReadSync(std::span<uint8_t> buffer, size_t offset, size_t* bytes) override {
    {
      std::unique_lock lock(state_->mutex);
      state_->entered = true; state_->cv.notify_all();
      state_->cv.wait(lock, [&] { return state_->resume; });
    }
    *bytes = 0;
    if (state_->failure) {
      if (state_->failure == 1) { buffer[0] = 0x5d; *bytes = 1; }
      return X_STATUS_UNSUCCESSFUL;
    }
    LARGE_INTEGER position{}; position.QuadPart = offset;
    SetFilePointerEx(state_->handle, position, nullptr, FILE_BEGIN);
    DWORD read = 0;
    const bool ok = ReadFile(state_->handle, buffer.data(), DWORD(buffer.size()), &read, nullptr) != 0;
    *bytes = read;
    return ok ? X_STATUS_SUCCESS : X_STATUS_UNSUCCESSFUL;
  }
  X_STATUS WriteSync(std::span<const uint8_t>, size_t, size_t*) override { return X_STATUS_UNSUCCESSFUL; }
 private:
  std::shared_ptr<ReadState> state_;
};
}

SR_TEST(runtime_host_io_partial_failure_is_active_until_return) {
  for (int scenario = 0; scenario < 4; ++scenario) {
    const auto root = std::filesystem::current_path() / "tracking-io-fixture";
    std::filesystem::create_directories(root);
    rex::Runtime runtime(root, root / "user", {}, root / "cache", root / "metadata");
    rex::RuntimeConfig config; config.tool_mode = true;
    const auto setup = runtime.Setup(std::move(config));
    SR_CHECK(setup == X_STATUS_SUCCESS);
    if (setup != X_STATUS_SUCCESS) return;
    auto& memory = *runtime.memory();
    auto getter = reinterpret_cast<RexGetMemoryTrackingApiFn>(
        GetProcAddress(GetModuleHandleW(L"rexruntime.dll"), "RexGetMemoryTrackingApi"));
    SR_CHECK(getter != nullptr); if (!getter) return;
    const auto* api = getter(1, sizeof(RexMemoryTrackingApiV1));
    SR_CHECK(api != nullptr); if (!api) return;
    RexTrackingSession session = 0;
    SR_CHECK(api->start(&memory, &session) == RexTrackingStatus::Ok);
    if (!session) return;
    SR_CHECK(memory.LookupHeapByType(true, 65536)->AllocFixed(0xA4000000, 65536, 65536,
        rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
        rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite));
    const RexTrackingRange range{0x04000000, 8192};
    unsigned legacy_notifications = 0;
    auto* legacy_handle = memory.RegisterPhysicalMemoryInvalidationCallback(
        [](void* context, uint32_t address, uint32_t length, bool) {
          ++*static_cast<unsigned*>(context);
          return std::pair<uint32_t, uint32_t>{address, length};
        }, &legacy_notifications);
    memory.EnablePhysicalMemoryAccessCallbacks(0x04000000, 8192, true, false);
    RexTrackingWatch watch = 0;
    SR_CHECK(api->watch(session, &range, 1, &watch) == RexTrackingStatus::Ok);
    RexTrackingStamp before{};
    SR_CHECK(api->sample(session, watch, &before, 1) == RexTrackingStatus::Ok);
    SR_CHECK(before.coverage == RexTrackingCoverage::Complete);

    auto state = std::make_shared<ReadState>();
    state->failure = scenario < 3 ? scenario : 0;
    const auto path = root / "input.bin";
    state->handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    SR_CHECK(state->handle != INVALID_HANDLE_VALUE);
    const std::array<uint8_t, 8> pattern{1, 7, 11, 19, 23, 31, 37, 41};
    DWORD written = 0;
    SR_CHECK(WriteFile(state->handle, pattern.data(), DWORD(pattern.size()), &written, nullptr) != 0);
    auto file = rex::system::object_ref<rex::system::XFile>(
        new rex::system::XFile(runtime.kernel_state(), new ControlledFile(state), true));
    const bool scatter = scenario == 3;
    const uint32_t segments_address = memory.SystemHeapAlloc(8);
    auto* segments = memory.TranslateVirtual<rex::be<uint32_t>*>(segments_address);
    segments[0] = 0xA4000000; segments[1] = 0xA4001000;
    uint32_t read = 0;
    X_STATUS status = X_STATUS_UNSUCCESSFUL;
    std::thread worker([&] {
      status = scatter ? file->ReadScatter(segments_address, 8192, 0, &read, 0)
          : file->Read(0xA4000000, 4096, 0, &read, 0, false);
    });
    {
      std::unique_lock lock(state->mutex);
      SR_CHECK(state->cv.wait_for(lock, std::chrono::seconds(5), [&] { return state->entered; }));
    }
    RexTrackingStamp during{};
    SR_CHECK(api->sample(session, watch, &during, 1) == RexTrackingStatus::Ok);
    SR_CHECK(during.active_writers > 0);
    SR_CHECK(during.coverage != RexTrackingCoverage::Complete);
    SR_CHECK(during.invalidation_epoch > before.invalidation_epoch);
    SR_CHECK_EQ(legacy_notifications, 0u);
    {
      std::lock_guard lock(state->mutex); state->resume = true;
    }
    state->cv.notify_all(); worker.join();
    RexTrackingStamp after{};
    SR_CHECK(api->sample(session, watch, &after, 1) == RexTrackingStatus::Ok);
    SR_CHECK_EQ(after.active_writers, 0u);
    SR_CHECK(after.coverage != RexTrackingCoverage::Complete);
    SR_CHECK(after.invalidation_epoch > before.invalidation_epoch);
    if (!state->failure) {
      SR_CHECK(status == X_STATUS_SUCCESS);
      SR_CHECK_EQ(read, 8u);
      const auto* actual = memory.TranslatePhysical<const uint8_t*>(0x04000000);
      for (size_t i = 0; i < pattern.size(); ++i) SR_CHECK_EQ(actual[i], pattern[i]);
      SR_CHECK(legacy_notifications > 0);
    } else {
      SR_CHECK(status == X_STATUS_UNSUCCESSFUL);
      SR_CHECK_EQ(read, state->failure == 1 ? 1u : 0u);
      SR_CHECK_EQ(legacy_notifications, 0u);
    }
    memory.UnregisterPhysicalMemoryInvalidationCallback(legacy_handle);
    api->unwatch(session, watch); api->stop(session);
    runtime.kernel_state()->object_table()->RemoveHandle(file->handle()); file.reset();
    memory.SystemHeapFree(segments_address);
    CloseHandle(state->handle);
  }
}
