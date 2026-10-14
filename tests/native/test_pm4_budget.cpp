#include "../../port/src/native_renderer/pm4_mirror.h"
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <vector>
using superman_returns::native::Pm4Mirror;
volatile unsigned checksum=0;
#ifdef _MSC_VER
__declspec(noinline)
#else
__attribute__((noinline))
#endif
unsigned Reference(const uint32_t* source,uint32_t* destination,bool* written,unsigned n) {
 unsigned changes=0;
 for(unsigned i=0;i<n;++i) {unsigned value=__builtin_bswap32(source[i]);changes+=destination[i]!=value;destination[i]=value;}
 std::fill_n(written,n,true);return changes;
}
int main() {
 constexpr unsigned count=1024,iterations=20000;
 std::vector<uint32_t> stream(count+1);
 stream[0]=__builtin_bswap32(((count-1)<<16)|0x4000);
 for(unsigned i=0;i<count;++i) stream[i+1]=__builtin_bswap32(0x3f800000+i);
 Pm4Mirror mirror;uint32_t destination[count]{};bool written[count]{};
 std::vector<double> pm4,reference;
 for(unsigned round=0;round<7;++round) {
  auto start=std::chrono::steady_clock::now();
  for(unsigned n=0;n<iterations;++n) {stream[1]^=0x01000000;mirror.ScanCopy(nullptr,reinterpret_cast<const uint8_t*>(stream.data()),stream.size()*4);checksum=mirror.reg(0x4000);}
  pm4.push_back(std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/iterations/count);
  start=std::chrono::steady_clock::now();
  for(unsigned n=0;n<iterations;++n) {stream[1]^=0x01000000;checksum=Reference(stream.data()+1,destination,written,count);}
  reference.push_back(std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/iterations/count);
 }
 std::sort(pm4.begin(),pm4.end());std::sort(reference.begin(),reference.end());
 double ratio=pm4[3]/reference[3];
 printf("PM4=%.3f ns/word reference=%.3f ratio=%.2f budget<=2.0 result=%s\n",pm4[3],reference[3],ratio,ratio<=2?"PASS":"FAIL");
 return ratio<=2?0:1;
}
