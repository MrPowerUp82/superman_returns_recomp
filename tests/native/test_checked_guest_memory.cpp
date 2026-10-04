#ifdef _WIN32
#include "../../port/src/native_renderer/checked_guest_memory.h"
#include "test_main.h"
using superman_returns::native::CheckedGuestReads;
SR_TEST(checked_guest_reads_keep_nested_sources_owned) {
  CheckedGuestReads reads;
  uint8_t first[]{1,2,3},second[]{4,5};
  auto a=reads.Read(first,3);auto b=reads.Read(second,2);
  first[0]=99;second[0]=88;
  SR_CHECK_EQ(a.size(),3u);SR_CHECK_EQ(a[0],1u);
  SR_CHECK_EQ(b.size(),2u);SR_CHECK_EQ(b[0],4u);
  reads.Reset();SR_CHECK(reads.Read(nullptr,1).empty());
  SR_CHECK(reads.Read(first,UINT32_MAX).empty());
}
SR_TEST(checked_guest_reads_reject_inaccessible_and_partial_ranges) {
  auto* pages=static_cast<uint8_t*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
  SR_CHECK(pages!=nullptr);if(!pages) return;
  pages[0]=17;pages[4095]=18;
  DWORD previous=0;SR_CHECK(VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&previous));
  CheckedGuestReads reads;
  auto valid=reads.Read(pages,4096);SR_CHECK_EQ(valid.size(),4096u);
  SR_CHECK_EQ(valid[0],17u);SR_CHECK_EQ(valid[4095],18u);
  SR_CHECK(reads.Read(pages+4096,1).empty());
  SR_CHECK(reads.Read(pages,4097).empty());
  SR_CHECK(VirtualFree(pages,0,MEM_RELEASE));
  SR_CHECK_EQ(valid[0],17u); // Owned bytes survive release of the source.
}
#endif
