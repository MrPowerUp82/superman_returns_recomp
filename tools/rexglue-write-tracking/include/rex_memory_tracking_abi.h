#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Binary C ABI, x64 natural alignment. No STL objects cross the DLL boundary.
enum class RexTrackingCoverage : uint32_t { Unknown, Complete, Invalidated };
enum class RexTrackingStatus : uint32_t { Ok, Unsupported, InvalidRange, Unavailable, LostCoverage };
enum class RexTrackingCapability : uint64_t {
  LifecycleAliases = 1, VirtualPhysicalGuards = 2, HostProducers = 4, AllUnprotectsObserved = 8
};
inline constexpr uint64_t kRexTrackingRequiredCapabilities = 15;
using RexTrackingSession = uint64_t;
using RexTrackingWatch = uint64_t;
struct RexTrackingRange { uint32_t physical_address, length; };
struct RexTrackingStamp {
  uint64_t allocation_generation, coverage_generation, invalidation_epoch;
  uint32_t active_writers;
  RexTrackingCoverage coverage;
};
struct RexTrackingStats {
  uint64_t guest_faults, physical_faults, io_begin, io_end, lifecycle_events;
  uint64_t protection_failures, coverage_losses, active_watches;
};
struct RexMemoryTrackingApiV1 {
  uint32_t version, size;
  uint64_t capabilities;
  RexTrackingStatus (*start)(void*, RexTrackingSession*);
  void (*stop)(RexTrackingSession);
  RexTrackingStatus (*watch)(RexTrackingSession, const RexTrackingRange*, uint32_t, RexTrackingWatch*);
  void (*unwatch)(RexTrackingSession, RexTrackingWatch);
  RexTrackingStatus (*sample)(RexTrackingSession, RexTrackingWatch, RexTrackingStamp*, uint32_t);
  void (*invalidate)(RexTrackingSession, RexTrackingWatch);
  void (*stats)(RexTrackingSession, RexTrackingStats*);
};
extern "C" const RexMemoryTrackingApiV1* RexGetMemoryTrackingApi(uint32_t version, uint32_t caller_size);
using RexGetMemoryTrackingApiFn = decltype(&RexGetMemoryTrackingApi);

static_assert(sizeof(void*) == 8);
static_assert(sizeof(RexTrackingRange) == 8);
static_assert(sizeof(RexTrackingStamp) == 32 && offsetof(RexTrackingStamp, coverage) == 28);
static_assert(sizeof(RexTrackingStats) == 64);
static_assert(sizeof(RexMemoryTrackingApiV1) == 72 && offsetof(RexMemoryTrackingApiV1, start) == 16);
static_assert(std::is_standard_layout_v<RexMemoryTrackingApiV1>);
