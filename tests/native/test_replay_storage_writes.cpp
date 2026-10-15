#include "../../port/src/graphics/guest/constant_snapshot.h"
#include "test_main.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstddef>
#include <cstring>
#include <memory>
using superman_returns::graphics::guest::ConstantSnapshot;
SR_TEST(replay_snapshot_does_not_write_metadata_that_decode_replaces) {
  SYSTEM_INFO system{};GetSystemInfo(&system);
  const size_t page=system.dwPageSize,prefix=512;
  // Align the replaced suffix to a page boundary, separately from the prefix.
  // Write-watch measures stores without reading indeterminate object bytes.
  const size_t offset=(page-(offsetof(ConstantSnapshot,shared)+prefix)%page)%page;
  const size_t size=((offset+sizeof(ConstantSnapshot)+page-1)/page+1)*page;
  auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT|MEM_WRITE_WATCH,PAGE_READWRITE));
  SR_CHECK(memory!=nullptr);if(!memory) return;
  struct Free {void* memory;~Free(){VirtualFree(memory,0,MEM_RELEASE);}} free{memory};
  std::memset(memory,0xa5,size);
  SR_CHECK_EQ(ResetWriteWatch(memory,size),0u);
  auto* snapshot=std::construct_at(reinterpret_cast<ConstantSnapshot*>(memory+offset),ConstantSnapshot::ReplayStorageTag{});
  for(size_t i=0;i<prefix;++i) SR_CHECK_EQ(snapshot->shared[i],0u);
  std::array<void*,16> pages{};ULONG_PTR count=pages.size();DWORD granularity=0;
  SR_CHECK_EQ(GetWriteWatch(0,memory,size,pages.data(),&count,&granularity),0u);
  SR_CHECK_EQ(granularity,page);
  auto* suffix=memory+offset+offsetof(ConstantSnapshot,shared)+prefix;
  bool suffix_written=false,prefix_written=false;
  for(ULONG_PTR i=0;i<count;++i) {
    suffix_written|=pages[i]==suffix;
    prefix_written|=pages[i]==suffix-page;
  }
  SR_CHECK(prefix_written);SR_CHECK(!suffix_written);
  std::destroy_at(snapshot);
}
#endif
