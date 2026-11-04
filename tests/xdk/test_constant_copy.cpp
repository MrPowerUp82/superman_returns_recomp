#include "../../port/src/native_renderer/xdk_constant_copy.h"
#include "../../port/src/native_renderer/xdk_constant_bank_copy.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <cstdlib>
#include <chrono>
#include <atomic>
REX_FUNC(reference_vs);
REX_FUNC(reference_ps);
__declspec(noinline) void BenchOld(PPCContext& ctx,uint8_t* base) {
  superman_returns::native::TryCopyConstantPrefix(ctx,base,false);
  reference_vs(ctx,base);
}
__declspec(noinline) void BenchFull(PPCContext& ctx,uint8_t* base) {
  if(!superman_returns::native::TryCopyConstantBank(ctx,base,false)) BenchOld(ctx,base);
}
int main(int argc,char** argv) {
  using superman_returns::native::TryCopyConstantPrefix;
  constexpr size_t size=65536;
  auto release=[](uint8_t* p) {_aligned_free(p);};
  std::unique_ptr<uint8_t,decltype(release)> expected(static_cast<uint8_t*>(_aligned_malloc(size,32)),release);
  std::unique_ptr<uint8_t,decltype(release)> actual(static_cast<uint8_t*>(_aligned_malloc(size,32)),release);
  if(!expected || !actual) return 2;
  std::array<uint8_t,size> initial;
  std::mt19937 random(12345);for(auto& byte:initial) byte=uint8_t(random());
  size_t cases=0,prefixes=0,full_copies=0;
  for(bool pixel:{false,true}) for(uint32_t alignment=0;alignment<16;++alignment)
    for(uint32_t count=0;count<=256;++count) {
      PPCContext original{};
      auto* bytes=reinterpret_cast<uint8_t*>(&original);
      // Include arbitrary volatile/nonvolatile bits and condition flags. Both
      // contexts start byte-identical, including padding; compare all of them.
      for(size_t i=0;i<sizeof(original);++i) bytes[i]=uint8_t(random());
      original.r1.u64=0xf000;original.r3.u64=0x1000;
      original.r4.u64=(count<=224?31:0);original.r5.u64=0x8000+alignment;
      original.r6.u64=count;auto optimized=original;auto complete=original;
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
      std::memcpy(actual.get(),initial.data(),size);
      const bool full=superman_returns::native::TryCopyConstantBank(complete,actual.get(),pixel);
      full_copies+=full;
      if(full!=(alignment==0)) {
        std::fprintf(stderr,"Unexpected full-copy admission: pixel=%d alignment=%u count=%u\n",pixel,alignment,count);return 1;
      }
      if(!full) reference(complete,actual.get());
      if(std::memcmp(&original,&complete,sizeof(original)) || std::memcmp(expected.get(),actual.get(),size)) {
        std::fprintf(stderr,"Full XDK mismatch: pixel=%d alignment=%u count=%u\n",pixel,alignment,count);return 1;
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
    if(superman_returns::native::TryCopyConstantBank(input,actual.get(),false) ||
       std::memcmp(&before,&input,sizeof(input)) || std::memcmp(actual.get(),initial.data(),size)) {
      std::fprintf(stderr,"Full-copy fallback mutated state: %u\n",kind);return 1;
    }
  }
  std::printf("%zu XDK calls: complete guest memory and PPC context matched; %zu native prefixes, %zu native full copies\n",cases,prefixes,full_copies);
  if(argc>1 && !std::strcmp(argv[1],"--bench")) {
    constexpr unsigned repeats=500000;
    for(unsigned count:{0u,1u,2u,3u,4u,7u,8u,16u,32u,64u,256u}) {
      double ns[2]{};
      for(unsigned mode=0;mode<2;++mode) {
        PPCContext context{};context.r1.u64=0xf000;
        auto begin=std::chrono::steady_clock::now();
        for(unsigned i=0;i<repeats;++i) {
          context.r3.u64=0x1000;context.r4.u64=0;context.r5.u64=0x8000;
          context.r6.u64=count;context.r7.u64=1;
          (mode?BenchFull:BenchOld)(context,actual.get());
          std::atomic_signal_fence(std::memory_order_seq_cst);
        }
        ns[mode]=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count()/repeats;
      }
      std::printf("count=%u old_ns=%.2f full_ns=%.2f ratio=%.3f\n",count,ns[0],ns[1],ns[1]/ns[0]);
    }
  }
}
