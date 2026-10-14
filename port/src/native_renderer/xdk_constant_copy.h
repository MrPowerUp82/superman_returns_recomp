#pragma once
#include <rex/ppc.h>
#include <cstring>

namespace superman_returns::native {
// Copy complete leading float4 groups natively, then let the original XDK
// run its final group and tail. That tail reconstructs every guest register,
// vector register, condition flag and stack/dirty-mask write of the full call.
inline bool TryCopyConstantPrefix(PPCContext& ctx,uint8_t* base,bool pixel) {
  const uint64_t count=ctx.r6.u64,start=ctx.r4.u64,device=ctx.r3.u64,source=ctx.r5.u64;
  if(!base || count<8 || count>256 || start>256-count || (device&15) ||
     device>=0xa0000000ull || source>=0xa0000000ull) return false;
  const uint64_t destination=device+((pixel?368ull:112ull)+start)*16;
  const uint64_t bytes=count*16,stack=ctx.r1.u32;
  // Physical aliases and address-wrap cases keep the generated path. For
  // ordinary virtual buffers the raw host view matches the generated access.
  if(source+bytes>0xa0000000ull || destination+bytes>0xa0000000ull ||
     stack<32 || stack+56>0xa0000000ull) return false;
  auto overlap=[](uint64_t a,uint64_t n,uint64_t b,uint64_t m) {return a<b+m && b<a+n;};
  if(overlap(source,bytes,destination,bytes) ||
     overlap(source,bytes,stack-32,4) || overlap(source,bytes,stack+48,8) ||
     overlap(destination,bytes,stack-32,4) || overlap(destination,bytes,stack+48,8)) return false;
  const uint64_t prefix=(count/4-1)*4;
  std::memcpy(base+destination,base+source,size_t(prefix*16));
  ctx.r4.u64+=prefix;ctx.r5.u64+=prefix*16;ctx.r6.u64-=prefix;
  return true;
}
}
