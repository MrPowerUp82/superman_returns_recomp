#ifdef _WIN32
#include "../../port/src/native_renderer/checked_guest_memory.h"
#include "test_main.h"
#include <array>
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
SR_TEST(checked_guest_reads_reuse_slots_without_aliasing_successful_reads) {
  CheckedGuestReads reads;
  std::array<uint8_t,128> first{},second{};
  first.fill(17);second.fill(23);
  auto warm=reads.Read(first.data(),uint32_t(first.size()));
  const auto* slot=warm.data();reads.Reset();
  first.fill(31);
  auto a=reads.Read(first.data(),uint32_t(first.size()));
  SR_CHECK(a.data()==slot);SR_CHECK_EQ(a[0],31u);
  SR_CHECK(reads.Read(reinterpret_cast<void*>(1),128).empty());
  auto b=reads.Read(second.data(),uint32_t(second.size()));
  SR_CHECK(a.data()!=b.data());SR_CHECK_EQ(a[0],31u);SR_CHECK_EQ(b[0],23u);
  // Force outer storage growth while the first two spans remain in use.
  for(unsigned i=0;i<64;++i) SR_CHECK_EQ(reads.Read(second.data(),128).size(),128u);
  SR_CHECK_EQ(a[0],31u);SR_CHECK_EQ(b[0],23u);
  reads.Reset();SR_CHECK_EQ(reads.Read(second.data(),128)[0],23u);
}
SR_TEST(checked_guest_reads_scratch_does_not_disturb_nested_slots) {
  CheckedGuestReads reads;
  std::array<uint8_t, 64> indirect{}, alu1{}, alu2{};
  indirect.fill(0xAA);
  alu1.fill(0x11);
  alu2.fill(0x22);
  auto outer = reads.Read(indirect.data(), uint32_t(indirect.size()));
  const auto* outer_ptr = outer.data();
  auto s1 = reads.ReadScratch(alu1.data(), uint32_t(alu1.size()));
  SR_CHECK_EQ(s1.size(), alu1.size());
  SR_CHECK_EQ(s1[0], 0x11u);
  auto s2 = reads.ReadScratch(alu2.data(), uint32_t(alu2.size()));
  SR_CHECK_EQ(s2.size(), alu2.size());
  SR_CHECK_EQ(s2[0], 0x22u);
  SR_CHECK(outer.data() == outer_ptr);
  SR_CHECK_EQ(outer[0], 0xAAu);
  SR_CHECK(reads.ReadScratch(nullptr, 16).empty());
}
#endif
