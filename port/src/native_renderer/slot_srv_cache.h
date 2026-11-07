#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace superman_returns::native {

struct SlotSrvCache {
  static constexpr uint32_t kSets = 32;
  static constexpr uint32_t kWays = 2;
  static constexpr uint32_t kTotalEntries = kSets * kWays;

  struct Entry {
    uint32_t fetch[6] = {};
    uint32_t srv = 0;
    uint64_t frame = ~0ull;
    uint64_t fetch_version = 0;
    bool smooth = false;
  };

  std::array<Entry, kTotalEntries> entries{};
  std::array<uint8_t, kSets> next_way{};
  uint32_t active_entry = 0;

  uint64_t sampler_key = 0;
  uint32_t sampler_index = 0;
  bool sampler_valid = false;

  static uint32_t SetIndex(const uint32_t fetch[6]) {
    uint32_t h = fetch[1] ^ (fetch[1] >> 12) ^ (fetch[2] * 0x9E3779B1u) ^ fetch[3] ^ fetch[4];
    return ((h * 0x85EBCA6Bu) >> 27) & (kSets - 1);
  }

  Entry* FindVersioned(uint64_t frame, uint64_t fetch_version) {
    Entry& active = entries[active_entry];
    if (active.frame == frame && active.fetch_version == fetch_version) {
      return &active;
    }
    return nullptr;
  }

  Entry* Find(uint64_t frame, const uint32_t fetch[6]) {
    const uint32_t set = SetIndex(fetch);
    const uint32_t base = set * kWays;
    for (uint32_t w = 0; w < kWays; ++w) {
      Entry& e = entries[base + w];
      if (e.frame == frame && std::memcmp(e.fetch, fetch, sizeof(e.fetch)) == 0) {
        active_entry = base + w;
        return &e;
      }
    }
    return nullptr;
  }

  Entry& Insert(uint64_t frame, const uint32_t fetch[6], uint32_t srv,
                uint64_t fetch_version, bool smooth) {
    const uint32_t set = SetIndex(fetch);
    const uint32_t base = set * kWays;
    uint32_t chosen_way = kWays;
    for (uint32_t w = 0; w < kWays; ++w) {
      if (entries[base + w].frame != frame) {
        chosen_way = w;
        break;
      }
    }
    if (chosen_way == kWays) {
      chosen_way = next_way[set]++ & (kWays - 1);
    }
    const uint32_t idx = base + chosen_way;
    Entry& e = entries[idx];
    std::memcpy(e.fetch, fetch, sizeof(e.fetch));
    e.srv = srv;
    e.frame = frame;
    e.fetch_version = fetch_version;
    e.smooth = smooth;
    active_entry = idx;
    return e;
  }

  template <typename GuestPhysicalFn>
  void Invalidate(uint32_t dest_base, GuestPhysicalFn&& guest_physical) {
    for (auto& e : entries) {
      if (dest_base == ~0u || guest_physical(e.fetch[1] & 0xFFFFF000u) == dest_base) {
        e.frame = ~0ull;
      }
    }
  }
};

}  // namespace superman_returns::native
