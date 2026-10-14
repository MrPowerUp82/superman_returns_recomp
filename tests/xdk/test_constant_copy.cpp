#include "../../port/src/native_renderer/xdk_constant_copy.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <cstdlib>
REX_FUNC(reference_vs);
REX_FUNC(reference_ps);
int main() {
  using superman_returns::native::TryCopyConstantPrefix;
  constexpr size_t size=65536;
  auto release=[](uint8_t* p) {_aligned_free(p);};
  std::unique_ptr<uint8_t,decltype(release)> expected(static_cast<uint8_t*>(_aligned_malloc(size,32)),release);
  std::unique_ptr<uint8_t,decltype(release)> actual(static_cast<uint8_t*>(_aligned_malloc(size,32)),release);
  if(!expected || !actual) return 2;
  std::array<uint8_t,size> initial;
  std::mt19937 random(12345);for(auto& byte:initial) byte=uint8_t(random());
  size_t cases=0,prefixes=0;
  for(bool pixel:{false,true}) for(uint32_t alignment=0;alignment<16;++alignment)
    for(uint32_t count=0;count<=256;++count) {
      PPCContext original{};
      auto* bytes=reinterpret_cast<uint8_t*>(&original);
      // Include arbitrary volatile/nonvolatile bits and condition flags. Both
      // contexts start byte-identical, including padding; compare all of them.
      for(size_t i=0;i<sizeof(original);++i) bytes[i]=uint8_t(random());
      original.r1.u64=0xf000;original.r3.u64=0x1000;
      original.r4.u64=(count<=224?31:0);original.r5.u64=0x8000+alignment;
      original.r6.u64=count;auto optimized=original;
      std::memcpy(expected.get(),initial.data(),size);std::memcpy(actual.get(),initial.data(),size);
      auto* reference=pixel?reference_ps:reference_vs;
      reference(original,expected.get());
      const bool prefix=TryCopyConstantPrefix(optimized,actual.get(),pixel);
      if(prefix) ++prefixes;
      if(prefix!=(count>=8)) {std::fprintf(stderr,"Missing/unexpected prefix: pixel=%d alignment=%u count=%u\n",pixel,alignment,count);return 1;}
      reference(optimized,actual.get());
      if(std::memcmp(&original,&optimized,sizeof(original)) || std::memcmp(expected.get(),actual.get(),size)) {
        std::fprintf(stderr,"XDK mismatch: pixel=%d alignment=%u count=%u\n",pixel,alignment,count);return 1;
      }
      ++cases;
    }
  // Invalid/overlapping plans must not modify guest state before fallback.
  for(uint32_t kind=0;kind<7;++kind) {
    PPCContext input{};input.r1.u64=0xf000;input.r3.u64=0x1000;input.r4.u64=0;input.r5.u64=0x8000;input.r6.u64=16;
    if(kind==0) input.r5.u64=0x1700; // overlaps destination
    if(kind==1) input.r3.u64+=1;     // unaligned vector destination
    if(kind==2) input.r6.u64=257;    // outside bank
    if(kind==3) input.r5.u64=0xf030; // overlaps the original's saved dirty mask
    if(kind==4) input.r5.u64=UINT32_MAX-16;
    if(kind==5) input.r5.u64=UINT64_MAX-16;
    if(kind==6) input.r3.u64=UINT64_MAX-15;
    auto before=input;std::memcpy(actual.get(),initial.data(),size);
    if(TryCopyConstantPrefix(input,actual.get(),false) || std::memcmp(&before,&input,sizeof(input)) ||
       std::memcmp(actual.get(),initial.data(),size)) {std::fprintf(stderr,"Fallback mutated state: %u\n",kind);return 1;}
  }
  std::printf("%zu XDK calls: complete guest memory and PPC context matched; %zu native prefixes\n",cases,prefixes);
}
