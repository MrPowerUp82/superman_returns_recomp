#include "../../port/src/coop_guest_string.h"
#include "test_main.h"
#include <cstring>
using superman_returns::coop::ReadGuestSymbol;
SR_TEST(coop_guest_symbol_batches_reads_without_caching_guest_memory) {
  std::array<char,128> bytes{};
  std::memcpy(bytes.data(),"StaminaMeter",12);
  unsigned calls=0;
  auto read=[&](uint64_t at,void* out,size_t n) {
    ++calls;if(at<1 || at-1+n>bytes.size())return false;
    std::memcpy(out,bytes.data()+at-1,n);return true;
  };
  SR_CHECK(ReadGuestSymbol(1,read)=="StaminaMeter");
  SR_CHECK(calls==1);
  bytes[0]='X';
  SR_CHECK(ReadGuestSymbol(1,read)=="XtaminaMeter");
  SR_CHECK(ReadGuestSymbol(0,read).empty());
}
SR_TEST(coop_guest_symbol_preserves_terminator_before_inaccessible_region) {
  const char bytes[]="Main_Hud";
  auto read=[&](uint64_t at,void* out,size_t n) {
    if(at<1 || at-1+n>sizeof(bytes))return false;
    std::memcpy(out,bytes+at-1,n);return true;
  };
  SR_CHECK(ReadGuestSymbol(1,read)=="Main_Hud");
  SR_CHECK(ReadGuestSymbol(sizeof(bytes)+1,read).empty());
}
SR_TEST(coop_guest_symbol_rejects_non_ascii_unterminated_and_overflow) {
  std::array<uint8_t,128> bytes;bytes.fill('A');
  auto read=[&](uint64_t at,void* out,size_t n) {
    if(at<1 || at-1+n>bytes.size())return false;
    std::memcpy(out,bytes.data()+at-1,n);return true;
  };
  SR_CHECK(ReadGuestSymbol(1,read).empty());
  bytes[65]=0;
  SR_CHECK(ReadGuestSymbol(1,read).size()==65);
  bytes[31]=0x80;
  SR_CHECK(ReadGuestSymbol(1,read).empty());
  SR_CHECK(ReadGuestSymbol(0xFFFFFFF0u,read).empty());
}
