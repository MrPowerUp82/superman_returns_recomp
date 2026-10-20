#include "runtime_memory_tracker.h"
#include <rex/system/xmemory.h>
#include <rex/thread.h>
#include <algorithm>
#include <array>
#include <memory>
#include <unordered_map>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace sr::tracking {
namespace {
constexpr uint32_t kPage = WriteTrackingState::kPageSize;
struct View {
  uint8_t* host;
  uint32_t physical_start, length;
  std::vector<uint8_t> owned;
};
struct Watch {
  std::vector<RexTrackingRange> ranges;
  std::vector<uint64_t> generations;
};
struct Monitor {
  rex::memory::Memory* memory;
  RexTrackingSession session;
  WriteTrackingState state;
  std::array<View, 5> views;
  std::unordered_map<RexTrackingWatch, Watch> watches;
  std::vector<uint32_t> references;
  std::vector<uint32_t> host_writers;
  RexTrackingStats stats{};
  bool closing = false;
  Monitor(rex::memory::Memory* mem, uint64_t id) : memory(mem), session(id), references(0x20000000 / kPage),
      host_writers(0x20000000 / kPage) {
    auto* base = mem->virtual_membase();
    views = {View{mem->physical_membase(), 0, 0x20000000, {}},
             View{base + 0xA0000000ull, 0, 0x20000000, {}},
             View{base + 0xC0000000ull, 0, 0x20000000, {}},
             View{base + 0xE0000000ull, 0, 0x20000000, {}},
             View{base + 0x7F000000ull, 0, 0x01000000, {}}};
    // E maps file offset rounded down to 64 KiB on Windows; its logical
    // +4 KiB physical offset is supplied by TranslateVirtual, not the map.
    for (auto& view : views) view.owned.resize(view.length / kPage);
    auto* heap = mem->GetPhysicalHeap();
    for (uint32_t p = 0; p < 0x20000000;) {
      rex::memory::HeapAllocationInfo info{};
      if (!heap->QueryRegionInfo(p, &info) || !info.region_size) break;
      if (info.state && info.allocation_base == p) state.Allocate(p, info.allocation_size);
      p += info.region_size;
    }
  }
  void Invalidate(uint32_t address, uint32_t length) {
    state.Invalidate(address, length);
    ++stats.coverage_losses;
  }
  void Protection(void* host, size_t length) {
    const auto begin = reinterpret_cast<uintptr_t>(host);
    if (closing || !length || length > UINTPTR_MAX - begin) return;
    for (auto& view : views) {
      const auto base = reinterpret_cast<uintptr_t>(view.host);
      const auto first = std::max(begin, base), last = std::min(begin + length, base + view.length);
      if (first >= last) continue;
      const uint32_t a = uint32_t(first - base) / kPage, b = uint32_t(last - 1 - base) / kPage;
      bool watched = false;
      for (uint32_t p = a; p <= b; ++p) { watched |= references[p] != 0; view.owned[p] = false; }
      if (watched) Invalidate(a * kPage, (b - a + 1) * kPage);
    }
  }
  bool Arm(RexTrackingRange range) {
    const uint32_t first = range.physical_address / kPage;
    const uint32_t last = (range.physical_address + range.length - 1) / kPage;
    for (auto& view : views) {
      for (uint32_t p = first; p <= last && p < view.owned.size(); ++p) {
        size_t length = kPage;
        rex::memory::PageAccess access;
        void* host = view.host + size_t(p) * kPage;
#if defined(_WIN32)
        MEMORY_BASIC_INFORMATION mapping{};
        if (!VirtualQuery(host, &mapping, sizeof(mapping)) || mapping.State != MEM_COMMIT ||
            mapping.Type != MEM_MAPPED || mapping.AllocationBase != view.host) return false;
#endif
        if (!rex::memory::QueryProtect(host, length, access) || length < kPage) return false;
        if (access == rex::memory::PageAccess::kReadWrite) {
          if (!rex::memory::Protect(host, kPage, rex::memory::PageAccess::kReadOnly)) return false;
          view.owned[p] = true;
        } else if (access != rex::memory::PageAccess::kReadOnly && access != rex::memory::PageAccess::kNoAccess) {
          return false;
        }
      }
    }
    return true;
  }
  void Restore() {
    closing = true;
    for (auto& view : views) for (uint32_t p = 0; p < view.owned.size(); ++p) {
      if (view.owned[p]) {
        // Only undo protections still owned by this monitor. Other SDK
        // changes relinquish ownership before modifying the mapping.
        rex::memory::Protect(view.host + size_t(p) * kPage, kPage, rex::memory::PageAccess::kReadWrite);
        view.owned[p] = false;
      }
    }
  }
};
std::shared_ptr<Monitor> monitor;
uint64_t next_handle = 0;
// Registered even without guards, so installing a session cannot miss an I/O
// operation which began before it. No page monitor or per-frame audit in Off.
uint64_t pending_host_writes = 0;
bool host_registration_lost = false;
uint64_t NewHandle() { return next_handle == UINT64_MAX ? 0 : ++next_handle; }
bool Valid(RexTrackingRange r) { return r.length && uint64_t(r.physical_address) + r.length <= 0x20000000; }
Monitor* Find(RexTrackingSession session) { return monitor && monitor->session == session ? monitor.get() : nullptr; }

RexTrackingStatus Start(void* memory, RexTrackingSession* out) {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  if (out) *out = 0;
  auto* guest_memory = static_cast<rex::memory::Memory*>(memory);
  if (!out || !guest_memory || !guest_memory->virtual_membase() || !guest_memory->physical_membase() ||
      monitor || pending_host_writes || host_registration_lost ||
      rex::memory::page_size() != kPage) return RexTrackingStatus::Unavailable;
  const auto id = NewHandle();
  if (!id) return RexTrackingStatus::Unavailable;
  monitor = std::make_shared<Monitor>(guest_memory, id);
  *out = id;
  return RexTrackingStatus::Ok;
}
void Stop(RexTrackingSession session) {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  if (auto* m = Find(session)) { m->Restore(); monitor.reset(); }
}
RexTrackingStatus WatchRanges(RexTrackingSession session, const RexTrackingRange* ranges,
                             uint32_t count, RexTrackingWatch* out) {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  auto* m = Find(session);
  if (!m || !out || !ranges || !count || count > 4096) return RexTrackingStatus::InvalidRange;
  *out = 0;
  Watch watch;
  bool may_cover = true;
  for (uint32_t i = 0; i < count; ++i) {
    if (!Valid(ranges[i])) return RexTrackingStatus::InvalidRange;
    const auto stamp = m->state.Sample(ranges[i]);
    if (!stamp.allocation_generation) return RexTrackingStatus::Unavailable;
    for (uint32_t p = ranges[i].physical_address / kPage;
         p <= (ranges[i].physical_address + ranges[i].length - 1) / kPage; ++p) {
      if (m->host_writers[p]) return RexTrackingStatus::Unavailable;
      if (m->references[p] > UINT32_MAX - count) {
        m->Invalidate(ranges[i].physical_address, ranges[i].length);
        return RexTrackingStatus::LostCoverage;
      }
    }
    may_cover &= stamp.coverage != RexTrackingCoverage::Invalidated;
    watch.generations.push_back(stamp.allocation_generation);
    watch.ranges.push_back(ranges[i]);
  }
  const auto id = NewHandle();
  if (!id) return RexTrackingStatus::Unavailable;
  m->watches.emplace(id, std::move(watch));
  for (const auto r : m->watches.at(id).ranges) {
    for (uint32_t p = r.physical_address / kPage; p <= (r.physical_address + r.length - 1) / kPage; ++p) {
      ++m->references[p];
    }
    if (may_cover && !m->Arm(r)) {
      ++m->stats.protection_failures; m->Invalidate(r.physical_address, r.length); may_cover = false;
    }
  }
  if (may_cover) {
#if defined(_WIN32)
    // Drain stores issued before the last alias became read-only. Later
    // stores must fault before proceeding; host operations are excluded above.
    FlushProcessWriteBuffers();
    for (const auto r : m->watches.at(id).ranges) {
      if (!m->state.MarkCovered(r)) { m->Invalidate(r.physical_address, r.length); may_cover = false; }
    }
#endif
  }
  m->stats.active_watches = m->watches.size();
  *out = id;
  return RexTrackingStatus::Ok;
}
void Unwatch(RexTrackingSession session, RexTrackingWatch id) {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  auto* m = Find(session); if (!m) return;
  auto it = m->watches.find(id); if (it == m->watches.end()) return;
  for (const auto r : it->second.ranges) {
    m->Invalidate(r.physical_address, r.length);
    for (uint32_t p = r.physical_address / kPage; p <= (r.physical_address + r.length - 1) / kPage; ++p) {
      if (m->references[p]) --m->references[p];
      if (!m->references[p]) for (auto& view : m->views) {
        if (p < view.owned.size() && view.owned[p]) {
          if (!rex::memory::Protect(view.host + size_t(p) * kPage, kPage, rex::memory::PageAccess::kReadWrite)) {
            ++m->stats.protection_failures;
          } else view.owned[p] = false;
        }
      }
    }
  }
  m->watches.erase(it);
  m->stats.active_watches = m->watches.size();
}
RexTrackingStatus Sample(RexTrackingSession session, RexTrackingWatch id, RexTrackingStamp* out, uint32_t count) {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  auto* m = Find(session); if (!m || !out) return RexTrackingStatus::Unavailable;
  auto it = m->watches.find(id);
  if (it == m->watches.end() || count != it->second.ranges.size()) return RexTrackingStatus::InvalidRange;
  for (uint32_t i = 0; i < count; ++i) {
    out[i] = m->state.Sample(it->second.ranges[i]);
    const auto r = it->second.ranges[i];
    uint64_t writers = out[i].active_writers;
    for (uint32_t p = r.physical_address / kPage; p <= (r.physical_address + r.length - 1) / kPage; ++p)
      writers += m->host_writers[p];
    out[i].active_writers = uint32_t(std::min(writers, uint64_t(UINT32_MAX)));
    if (writers && out[i].coverage == RexTrackingCoverage::Complete) out[i].coverage = RexTrackingCoverage::Unknown;
    if (out[i].allocation_generation != it->second.generations[i]) out[i].coverage = RexTrackingCoverage::Invalidated;
  }
  return RexTrackingStatus::Ok;
}
void Invalidate(RexTrackingSession session, RexTrackingWatch id) {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  auto* m = Find(session); if (!m) return;
  auto it = m->watches.find(id);
  if (it != m->watches.end()) for (const auto r : it->second.ranges) m->Invalidate(r.physical_address, r.length);
}
void Stats(RexTrackingSession session, RexTrackingStats* out) {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  if (auto* m = Find(session); m && out) *out = m->stats;
}
}  // namespace

HostWriteScope::HostWriteScope(rex::memory::Memory& memory, uint32_t address, uint32_t length)
    : HostWriteScope(memory, memory.physical_membase() + size_t(address), size_t(length)) {}

HostWriteScope::HostWriteScope(rex::memory::Memory& memory, void* host, size_t length)
    : memory_(&memory), host_(host), length_(length) {
  if (!length) return;
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  if (pending_host_writes == UINT64_MAX) {
    host_registration_lost = true;
    if (monitor) monitor->state.Invalidate(0, 0);
    return;
  }
  ++pending_host_writes; registered_ = true;
  if (!monitor || monitor->memory != &memory) return;
  auto& m = *monitor;
  lease_ = monitor;  // Stop may detach; the operation still owns its metadata.
  const auto begin = reinterpret_cast<uintptr_t>(host);
  if (length > UINTPTR_MAX - begin) {
    host_registration_lost = true;
    m.state.Invalidate(0, 0);
    return;
  }
  for (auto& view : m.views) {
    const auto base = reinterpret_cast<uintptr_t>(view.host);
    const auto first = std::max(begin, base), end = std::min(begin + length, base + view.length);
    if (first >= end) continue;
    const RexTrackingRange range{uint32_t(first - base), uint32_t(end - first)};
    ranges_[count_++] = range;
    const uint32_t a = range.physical_address / kPage, b = (range.physical_address + range.length - 1) / kPage;
    bool watched = false;
    for (uint32_t p = a; p <= b; ++p) {
      watched |= m.references[p] != 0;
      if (m.host_writers[p] == UINT32_MAX) m.state.Invalidate(0, 0);
      else ++m.host_writers[p];
    }
    if (watched) m.Invalidate(range.physical_address, range.length);
    for (uint32_t p = a; p <= b; ++p) {
      auto* page = view.host + size_t(p) * kPage;
      bool allow_write = view.owned[p] != 0;
      if (!allow_write && &view != &m.views[0]) {
        // A virtual write watch owned by the old SDK also needs an explicit
        // handoff for kernel I/O. Never widen guest read-only/no-access rights.
        const auto guest = memory.HostToGuestVirtual(page);
        auto* heap = memory.LookupHeap(guest);
        allow_write = heap && heap->QueryRangeAccess(guest, guest + kPage - 1) == rex::memory::PageAccess::kReadWrite;
      }
      if (allow_write) {
        // The entire producer remains active while this view is writable.
        m.Protection(page, kPage);
        if (!rex::memory::Protect(page, kPage, rex::memory::PageAccess::kReadWrite)) {
          ++m.stats.protection_failures; m.Invalidate(range.physical_address, range.length);
        }
      }
    }
  }
  if (count_) ++m.stats.io_begin;
}

HostWriteScope::~HostWriteScope() {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  if (lease_) {
    auto& m = *static_cast<Monitor*>(lease_.get());
    for (uint32_t i = 0; i < count_; ++i) {
      const auto r = ranges_[i];
      for (uint32_t p = r.physical_address / kPage; p <= (r.physical_address + r.length - 1) / kPage; ++p) {
        if (!m.host_writers[p]) m.state.Invalidate(0, 0);
        else --m.host_writers[p];
      }
    }
    if (count_) ++m.stats.io_end;
  }
  if (registered_) {
    if (pending_host_writes) --pending_host_writes;
    else { host_registration_lost = true; if (monitor) monitor->state.Invalidate(0, 0); }
  }
}

void HostWriteScope::NotifyLegacyVirtualWrite() {
  if (!lease_ || !count_ || !memory_ || !length_ || length_ > UINT32_MAX) return;
  const auto begin = reinterpret_cast<uintptr_t>(host_);
  const auto base = reinterpret_cast<uintptr_t>(memory_->virtual_membase());
  if (begin < base || begin - base >= 0x100000000ull) return;
  const auto guest = memory_->HostToGuestVirtual(host_);
  // This is the old SDK notification AFTER the host has stopped writing.
  // It is intentionally separate from the new pre-write invalidation.
  memory_->TriggerPhysicalMemoryCallbacks(rex::thread::global_critical_region::AcquireDirect(),
      guest, uint32_t(length_), true, true);
}

void TrackingAllocation(rex::memory::Memory* memory, uint32_t address, uint32_t length) {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  if (monitor && monitor->memory == memory) { monitor->state.Allocate(address, length); ++monitor->stats.lifecycle_events; }
}
void TrackingRelease(rex::memory::Memory* memory, uint32_t address, uint32_t length) {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  if (monitor && monitor->memory == memory) { monitor->state.Release(address, length); ++monitor->stats.lifecycle_events; }
}
void TrackingReset(rex::memory::Memory* memory) {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  if (monitor && monitor->memory == memory) { monitor->state.Reset(); ++monitor->stats.lifecycle_events; }
}
void TrackingStopMemory(rex::memory::Memory* memory) {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  if (monitor && monitor->memory == memory) Stop(monitor->session);
}
void TrackingHostProtection(rex::memory::Memory* memory, void* address, size_t length) {
  // Caller holds global critical region through the actual protection change.
  if (monitor && monitor->memory == memory) monitor->Protection(address, length);
}
bool TrackingFault(rex::memory::Memory* memory, void* address, bool write) {
  // MMIO dispatch enters with global critical region held exactly once.
  if (!write || !monitor || monitor->memory != memory) return false;
  auto& m = *monitor;
  const auto fault = reinterpret_cast<uintptr_t>(address);
  for (size_t v = 0; v < m.views.size(); ++v) {
    auto& view = m.views[v]; const auto base = reinterpret_cast<uintptr_t>(view.host);
    if (fault < base || fault - base >= view.length) continue;
    const uint32_t page = uint32_t(fault - base) / kPage;
    if (!view.owned[page]) return false;
    m.Invalidate(page * kPage, kPage);  // Publish loss BEFORE the store can resume.
    if (v == 0) ++m.stats.physical_faults; else ++m.stats.guest_faults;
    view.owned[page] = false;
    if (!rex::memory::Protect(view.host + size_t(page) * kPage, kPage, rex::memory::PageAccess::kReadWrite)) {
      ++m.stats.protection_failures;
      return false;
    }
    return true;
  }
  return false;
}
}  // namespace sr::tracking

extern "C" const RexMemoryTrackingApiV1* RexGetMemoryTrackingApi(uint32_t version, uint32_t caller_size) {
  using namespace sr::tracking;
  static const RexMemoryTrackingApiV1 api{1, sizeof(RexMemoryTrackingApiV1), 15,
      Start, Stop, WatchRanges, Unwatch, Sample, Invalidate, Stats};
  return version == 1 && caller_size >= sizeof(api) ? &api : nullptr;
}
