#pragma once
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <span>

namespace sr::tracking {
// Caller holds the SDK memory lock. Query results are used only within this
// call and clipped to the watch and alias; no mapping cache survives it.
template<class Query,class Protect>
bool ArmWindowsRegions(uint8_t* view,std::span<uint8_t> owned,uint32_t first,uint32_t last,
                       Query&& query,Protect&& protect) {
  constexpr size_t page_size=4096;
  const size_t stop=(std::min)(size_t(last)+1,owned.size());
  for(size_t page=first;page<stop;) {
    const auto view_address=reinterpret_cast<uintptr_t>(view);
    if(page>(UINTPTR_MAX-view_address)/page_size) return false;
    const uintptr_t address=view_address+page*page_size;
    auto* host=reinterpret_cast<uint8_t*>(address);
    MEMORY_BASIC_INFORMATION mapping{};
    if(!query(host,mapping) || mapping.State!=MEM_COMMIT || mapping.Type!=MEM_MAPPED ||
       mapping.AllocationBase!=view) return false;
    const auto region=reinterpret_cast<uintptr_t>(mapping.BaseAddress);
    if(address%page_size || region%page_size || mapping.RegionSize%page_size ||
       region>address || !mapping.RegionSize || mapping.RegionSize>UINTPTR_MAX-region) return false;
    const auto end=region+mapping.RegionSize;
    if(end<=address || (end-address)<page_size) return false;
    const size_t pages=(std::min)(stop-page,size_t((end-address)/page_size));
    if(mapping.Protect==PAGE_READWRITE) {
      if(!protect(host,pages*page_size)) return false;
      std::fill(owned.begin()+page,owned.begin()+page+pages,uint8_t(1));
    } else if(mapping.Protect!=PAGE_READONLY && mapping.Protect!=PAGE_NOACCESS) {
      // Unknown, executable, copy-on-write and guard-modified permissions never
      // establish a reusable proof. Do not widen or remove someone else's guard.
      return false;
    }
    page+=pages;
  }
  return true;
}
}  // namespace sr::tracking
#endif
