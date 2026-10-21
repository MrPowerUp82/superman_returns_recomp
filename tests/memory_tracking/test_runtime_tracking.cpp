#include "../native/test_main.h"
#include "../../tools/rexglue-write-tracking/include/rex_memory_tracking_abi.h"
#include "../../port/src/native_renderer/resource_write_tracking.h"
#include <rex/system/xmemory.h>
#include <rex/logging.h>
#include <rex/stream.h>
#include <windows.h>
#include <array>
#include <atomic>
#include <thread>
#include <cstring>

namespace {
struct Fixture {
  rex::memory::Memory memory;
  const RexMemoryTrackingApiV1* api = nullptr;
  RexTrackingSession session = 0;
  Fixture() {
    rex::InitLogging();
    SR_CHECK(memory.Initialize());
    const auto getter = reinterpret_cast<RexGetMemoryTrackingApiFn>(
        GetProcAddress(GetModuleHandleW(L"rexruntime.dll"), "RexGetMemoryTrackingApi"));
    if (getter) api = getter(1, sizeof(RexMemoryTrackingApiV1));
    SR_CHECK(api != nullptr);
    if (api) SR_CHECK(api->start(&memory, &session) == RexTrackingStatus::Ok);
  }
  ~Fixture() { if (session) api->stop(session); }
  bool Allocate(uint32_t page = 65536) {
    return memory.LookupHeapByType(true, page)->AllocFixed(page == 4096 ? 0xE3fff000 : 0xA4000000, page, page,
        rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
        rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite);
  }
  RexTrackingWatch Watch(uint32_t physical_address = 0x04000000) {
    const RexTrackingRange range{physical_address, 4096};
    RexTrackingWatch watch = 0;
    SR_CHECK(api->watch(session, &range, 1, &watch) == RexTrackingStatus::Ok);
    SR_CHECK(watch != 0);
    return watch;
  }
  RexTrackingStamp Sample(RexTrackingWatch watch) {
    RexTrackingStamp stamp{};
    SR_CHECK(api->sample(session, watch, &stamp, 1) == RexTrackingStatus::Ok);
    return stamp;
  }
};
}

SR_TEST(runtime_guest_aliases_and_raw_physical_store_invalidate) {
  for (const auto page : {4096u, 65536u}) for (const auto alias : {0xA4000000u, 0xC4000000u, 0xE3fff000u, 0u}) {
    Fixture f;
    if (!f.session) return;
    SR_CHECK(f.Allocate(page));
    auto* data = alias ? f.memory.TranslateVirtual<volatile uint8_t*>(alias)
                       : f.memory.TranslatePhysical<volatile uint8_t*>(0x04000000);
    *data = 1;
    const auto watch = f.Watch();
    const auto before = f.Sample(watch);
    if ((f.api->capabilities & 15) != 15) SR_CHECK(before.coverage != RexTrackingCoverage::Complete);
    SR_CHECK_EQ(*data, 1);
    *data = 23;
    const auto after = f.Sample(watch);
    SR_CHECK(after.invalidation_epoch > before.invalidation_epoch);
    SR_CHECK(after.coverage != RexTrackingCoverage::Complete);
    SR_CHECK_EQ(*data, 23);
    f.api->unwatch(f.session, watch);
  }
}

SR_TEST(runtime_optional_client_negotiates_and_releases_watches) {
  Fixture f;
  if (!f.session) return;
  f.api->stop(f.session); f.session = 0;
  superman_returns::native::ResourceWriteTracker tracker;
  SR_CHECK(tracker.Start(f.memory));
  SR_CHECK(f.Allocate());
  const RexTrackingRange range{0x04000000, 4096};
  const auto watch = tracker.Watch(std::span(&range, 1));
  SR_CHECK(watch != 0);
  RexTrackingStamp stamp{};
  SR_CHECK(tracker.Sample(watch, std::span(&stamp, 1)));
  SR_CHECK(stamp.coverage == RexTrackingCoverage::Complete);
  SR_CHECK_EQ(tracker.Stats().active_watches, 1u);
  tracker.Invalidate(watch);
  SR_CHECK(tracker.Sample(watch, std::span(&stamp, 1)));
  SR_CHECK(stamp.coverage != RexTrackingCoverage::Complete);
  tracker.Unwatch(watch);
  SR_CHECK_EQ(tracker.Stats().active_watches, 0u);
  tracker.Stop();
  SR_CHECK(!tracker.Sample(watch, std::span(&stamp, 1)));
}

SR_TEST(runtime_compare_missed_write_disqualifies_entire_allocation_generation) {
  using namespace superman_returns::native;
  Fixture f;
  if (!f.session) return;
  f.api->stop(f.session); f.session = 0;
  ResourceWriteTracker tracker;
  SR_CHECK(tracker.Start(f.memory));
  SR_CHECK(f.Allocate());
  auto* bytes = f.memory.TranslatePhysical<uint8_t*>(0x04000000);
  std::memset(bytes, 7, 4096);
  const RexTrackingRange range{0x04000000, 4096};
  TextureTrackingEntry entry;
  entry.owner = &tracker; entry.version = 1;
  entry.watch = tracker.Watch(std::span(&range, 1));
  const auto overlapping = tracker.Watch(std::span(&range, 1));
  const RexTrackingRange neighbor_range{0x04001000, 4096};
  const auto neighbor = tracker.Watch(std::span(&neighbor_range, 1));
  entry.before.resize(1); entry.current.resize(1);
  SR_CHECK(tracker.Sample(entry.watch, entry.before));
  SR_CHECK(entry.before[0].coverage == RexTrackingCoverage::Complete);
  const auto generation = entry.before[0].allocation_generation;
  const auto hash_bytes = [&] {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < 4096; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
  };
  const auto baseline_hash = hash_bytes();
  SR_CHECK(entry.proof.Publish(entry.version, baseline_hash, entry.before, entry.before, entry.before));
  entry.upload = {entry.watch, entry.before, entry.version};
  SR_CHECK(!AuditAndInvalidateTextureProof(entry, baseline_hash));

  // Test-only contract violation: bypass every runtime hook and guard fault.
  DWORD old_protection = 0;
  const bool writable = VirtualProtect(bytes, 4096, PAGE_READWRITE, &old_protection) != 0;
  SR_CHECK(writable);
  if (!writable) return;
  bytes[31] = 19;
  const auto changed_hash = hash_bytes();
  SR_CHECK(changed_hash != baseline_hash);
  SR_CHECK(tracker.Sample(entry.watch, entry.current));
  SR_CHECK(SameCoveredContent(entry.before, entry.current));
  SR_CHECK(AuditAndInvalidateTextureProof(entry, changed_hash));
  SR_CHECK_EQ(entry.proof.version, 0u);
  SR_CHECK_EQ(entry.upload.watch, 0u);
  RexTrackingStamp stamp{};
  for (const auto watch : {entry.watch, overlapping, neighbor}) {
    SR_CHECK(tracker.Sample(watch, std::span(&stamp, 1)));
    SR_CHECK(stamp.coverage == RexTrackingCoverage::Invalidated);
    SR_CHECK_EQ(stamp.allocation_generation, generation);
  }
  const auto fresh = tracker.Watch(std::span(&range, 1));
  SR_CHECK(tracker.Sample(fresh, std::span(&stamp, 1)));
  SR_CHECK(stamp.coverage == RexTrackingCoverage::Invalidated);
  // Retire old cache leases before freeing their backing allocation.
  for (const auto watch : {entry.watch, overlapping, neighbor, fresh}) tracker.Unwatch(watch);
  entry.watch = 0;
  auto* heap = f.memory.LookupHeapByType(true, 65536);
  SR_CHECK(heap->Release(0xA4000000));
  SR_CHECK(f.Allocate());
  const auto next_generation = tracker.Watch(std::span(&range, 1));
  SR_CHECK(tracker.Sample(next_generation, std::span(&stamp, 1)));
  SR_CHECK(stamp.coverage == RexTrackingCoverage::Complete);
  SR_CHECK(stamp.allocation_generation != generation);
  tracker.Unwatch(next_generation);
}

SR_TEST(runtime_stop_drains_inflight_faults) {
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    Fixture f;
    if (!f.session) return;
    SR_CHECK(f.Allocate());
    f.Watch();
    auto* target = f.memory.TranslatePhysical<volatile uint8_t*>(0x04000000);
    std::atomic<bool> go = false, ready = false;
    std::thread writer([&] {
      ready.store(true, std::memory_order_release);
      while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
      for (unsigned i = 0; i < 128; ++i) *target = uint8_t(i);
    });
    while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
    go.store(true, std::memory_order_release);
    f.api->stop(f.session); f.session = 0;
    writer.join();
    SR_CHECK_EQ(*target, 127u);
  }
}

SR_TEST(runtime_unwatch_restores_only_owned_pages) {
  Fixture f;
  if (!f.session) return;
  SR_CHECK(f.Allocate());
  auto* data = f.memory.TranslatePhysical<volatile uint8_t*>(0x04000000);
  const auto watch = f.Watch();
  f.api->unwatch(f.session, watch);
  RexTrackingStats before{}, after{};
  f.api->stats(f.session, &before);
  *data = 42;
  f.api->stats(f.session, &after);
  SR_CHECK_EQ(after.physical_faults, before.physical_faults);
  SR_CHECK_EQ(after.active_watches, 0u);
  RexTrackingStamp stamp{};
  SR_CHECK(f.api->sample(f.session, watch, &stamp, 1) != RexTrackingStatus::Ok);
}

SR_TEST(runtime_gpu_writeback_alias_is_also_observed) {
  Fixture f;
  if (!f.session) return;
  auto* alias = f.memory.TranslateVirtual<volatile uint8_t*>(0x7F010000);
  *alias = 4;
  const auto watch = f.Watch(0x00010000);
  const auto before = f.Sample(watch);
  *alias = 9;
  SR_CHECK(f.Sample(watch).invalidation_epoch > before.invalidation_epoch);
  SR_CHECK_EQ(*f.memory.TranslatePhysical<volatile uint8_t*>(0x00010000), 9);
  f.api->unwatch(f.session, watch);
}

SR_TEST(runtime_range_allocation_establishes_new_identity) {
  Fixture f;
  if (!f.session) return;
  auto* heap = f.memory.LookupHeapByType(true, 65536);
  uint32_t guest = 0;
  SR_CHECK(heap->Alloc(65536, 65536,
      rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
      rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite, true, &guest));
  const auto physical = f.memory.GetPhysicalAddress(guest);
  const auto watch = f.Watch(physical);
  const auto before = f.Sample(watch);
  SR_CHECK(before.allocation_generation != 0);
  *f.memory.TranslatePhysical<volatile uint8_t*>(physical) = 33;
  SR_CHECK(f.Sample(watch).invalidation_epoch > before.invalidation_epoch);
  f.api->unwatch(f.session, watch);
  SR_CHECK(heap->Release(guest));
}

SR_TEST(runtime_direct_heap_reset_discards_allocation_identity) {
  Fixture f;
  if (!f.session) return;
  SR_CHECK(f.Allocate());
  const auto watch = f.Watch();
  SR_CHECK(f.Sample(watch).allocation_generation != 0);
  f.memory.GetPhysicalHeap()->Reset();
  SR_CHECK_EQ(f.Sample(watch).allocation_generation, 0u);
  f.api->unwatch(f.session, watch);
}

SR_TEST(runtime_alias_reset_invalidates_previous_identity) {
  Fixture f;
  if (!f.session) return;
  SR_CHECK(f.Allocate());
  const auto watch = f.Watch();
  const auto before = f.Sample(watch);
  f.memory.LookupHeapByType(true, 65536)->Reset();
  const auto after = f.Sample(watch);
  SR_CHECK(after.allocation_generation != before.allocation_generation ||
           after.invalidation_epoch > before.invalidation_epoch);
  f.api->unwatch(f.session, watch);
}

SR_TEST(runtime_restore_of_free_pages_discards_previous_identity) {
  for (const bool direct_heap : {false, true}) {
  Fixture f;
  if (!f.session) return;
  std::vector<uint8_t> bytes(64 * 1024 * 1024);
  rex::stream::ByteStream stream(bytes.data(), bytes.size());
  SR_CHECK(direct_heap ? f.memory.GetPhysicalHeap()->Save(&stream) : f.memory.Save(&stream));
  SR_CHECK(f.Allocate());
  const auto watch = f.Watch();
  SR_CHECK(f.Sample(watch).allocation_generation != 0);
  stream.set_offset(0);
  SR_CHECK(direct_heap ? f.memory.GetPhysicalHeap()->Restore(&stream) : f.memory.Restore(&stream));
  SR_CHECK_EQ(f.Sample(watch).allocation_generation, 0u);
  f.api->unwatch(f.session, watch);
  }
}

SR_TEST(runtime_capability_query_rejects_wrong_version_and_size) {
  Fixture f;
  if (!f.session) return;
  const auto getter = reinterpret_cast<RexGetMemoryTrackingApiFn>(
      GetProcAddress(GetModuleHandleW(L"rexruntime.dll"), "RexGetMemoryTrackingApi"));
  SR_CHECK(getter(2, sizeof(RexMemoryTrackingApiV1)) == nullptr);
  SR_CHECK(getter(1, sizeof(RexMemoryTrackingApiV1) - 1) == nullptr);
}

SR_TEST(runtime_unwatch_keeps_sdk_owned_guards) {
  Fixture f;
  if (!f.session) return;
  SR_CHECK(f.Allocate());
  unsigned callbacks = 0;
  const auto callback = [](void* context, uint32_t address, uint32_t length, bool) {
    ++*static_cast<unsigned*>(context);
    return std::pair<uint32_t, uint32_t>{address, length};
  };
  auto* handle = f.memory.RegisterPhysicalMemoryInvalidationCallback(callback, &callbacks);
  f.memory.EnablePhysicalMemoryAccessCallbacks(0x04000000, 4096, true, false);
  const auto watch = f.Watch();
  f.api->unwatch(f.session, watch);
  *f.memory.TranslateVirtual<volatile uint8_t*>(0xA4000000) = 17;
  SR_CHECK(callbacks > 0);
  f.memory.UnregisterPhysicalMemoryInvalidationCallback(handle);
}

SR_TEST(runtime_decommit_and_reset_reject_previous_watch) {
  for (const bool reset : {false, true}) {
    Fixture f;
    if (!f.session) return;
    SR_CHECK(f.Allocate());
    const auto watch = f.Watch();
    const auto before = f.Sample(watch);
    if (reset) f.memory.Reset();
    else SR_CHECK(f.memory.LookupHeapByType(true, 65536)->Decommit(0xA4000000, 65536));
    const auto after = f.Sample(watch);
    SR_CHECK(after.coverage != RexTrackingCoverage::Complete);
    SR_CHECK(after.allocation_generation != before.allocation_generation || after.invalidation_epoch > before.invalidation_epoch);
    f.api->unwatch(f.session, watch);
  }
}

SR_TEST(runtime_shared_page_unprotect_invalidates_every_watch) {
  Fixture f;
  if (!f.session) return;
  SR_CHECK(f.Allocate());
  const auto first = f.Watch(), second = f.Watch();
  const auto a = f.Sample(first), b = f.Sample(second);
  auto* heap = f.memory.LookupHeapByType(true, 65536);
  SR_CHECK(heap->Protect(0xA4000000, 4096,
      rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite));
  SR_CHECK(f.Sample(first).invalidation_epoch > a.invalidation_epoch);
  SR_CHECK(f.Sample(second).invalidation_epoch > b.invalidation_epoch);
  f.api->unwatch(f.session, first); f.api->unwatch(f.session, second);
}

SR_TEST(runtime_lifecycle_changes_generation) {
  Fixture f;
  if (!f.session) return;
  SR_CHECK(f.Allocate());
  const auto old = f.Watch();
  const auto before = f.Sample(old);
  auto* heap = f.memory.LookupHeapByType(true, 65536);
  SR_CHECK(heap->Release(0xA4000000));
  SR_CHECK(f.Allocate());
  const auto next = f.Watch();
  SR_CHECK(f.Sample(next).allocation_generation != before.allocation_generation);
  SR_CHECK(f.Sample(old).coverage != RexTrackingCoverage::Complete);
  f.api->unwatch(f.session, old); f.api->unwatch(f.session, next);
}
