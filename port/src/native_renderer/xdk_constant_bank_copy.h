#pragma once
#include <rex/ppc.h>
#include <bit>
#include <cstring>

namespace superman_returns::native {
// Aligned VS/PS float banks are byte copies. Reconstruct the final volatile
// context as well: some internal XDK callers observe more than the public ABI.
// Unaligned, overlapping and physical-alias inputs keep the generated path.
inline bool TryCopyConstantBank(PPCContext& ctx, uint8_t* base, bool pixel) {
  const uint64_t count=ctx.r6.u64, start=ctx.r4.u64;
  const uint64_t device=ctx.r3.u64, source=ctx.r5.u64, stack=ctx.r1.u32;
  if (!base || count>256 || start>256-count || (device&15) || (source&15) ||
      device>=0xa0000000ull || source>=0xa0000000ull || stack<32 ||
      stack+56>0xa0000000ull) return false;
  const uint64_t destination=device+((pixel?368ull:112ull)+start)*16, bytes=count*16;
  if (source+bytes>0xa0000000ull || destination+bytes>0xa0000000ull) return false;
  const auto overlaps=[](uint64_t a,uint64_t n,uint64_t b,uint64_t m) {
    return n && m && a<b+m && b<a+n;
  };
  if (overlaps(source,bytes,destination,bytes) ||
      overlaps(source,bytes,stack-32,4) || overlaps(source,bytes,stack+48,8) ||
      overlaps(destination,bytes,stack-32,4) || overlaps(destination,bytes,stack+48,8)) return false;

  const auto store64=[&](uint64_t address,uint64_t value) {
    value=std::byteswap(value);std::memcpy(base+address,&value,8);
  };
  const auto load64=[&](uint64_t address) {
    uint64_t value;std::memcpy(&value,base+address,8);return std::byteswap(value);
  };
  store64(stack+48,ctx.r7.u64);
  const uint32_t saved_device=std::byteswap(ctx.r3.u32);
  std::memcpy(base+stack-32,&saved_device,4);
  if (bytes) std::memcpy(base+destination,base+source,size_t(bytes));
  const auto vector_at=[&](uint64_t address) {
    return simde_mm_shuffle_epi8(simde_mm_loadu_si128(
        reinterpret_cast<const simde__m128i*>(base+address)),
        simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(VectorMaskL)));
  };
  const auto put=[](PPCVRegister& v,simde__m128i value) {
    simde_mm_store_si128(reinterpret_cast<simde__m128i*>(v.u8),value);
  };
  const uint64_t groups=count/4, remainder=count%4;
  ctx.r8.s64=128;
  if (groups) {
    const uint64_t last_source=source+(groups-1)*64;
    const uint64_t last_destination=destination+(groups-1)*64;
    put(ctx.v0,vector_at(last_source));
    put(ctx.v13,vector_at(last_source+16));
    put(ctx.v12,vector_at(last_source+32));
    put(ctx.v10,vector_at(last_source+32));
    put(ctx.v11,vector_at(last_source+48));
    put(ctx.v8,vector_at(last_source+48));
    put(ctx.v7,simde_mm_setzero_si128()); put(ctx.v9,simde_mm_setzero_si128());
    ctx.r4.u64=last_destination+32; ctx.r5.s64=64;
    ctx.r6.u64=last_destination+48; ctx.r7.u64=last_destination+16;
    ctx.r8.s64=0;
  }
  ctx.cr6.compare<uint32_t>(uint32_t(remainder),0,ctx.xer);
  if (remainder) {
    const uint64_t last_source=source+(count-1)*16;
    put(ctx.v0,vector_at(last_source)); put(ctx.v13,simde_mm_setzero_si128());
    ctx.r8.u64=last_source; ctx.r7.u64=last_source; ctx.r6.s64=16;
  }
  if (count) {
    ctx.xer.ca=1;
    ctx.cr0.compare<int32_t>(0,0,ctx.xer);
  }
  uint32_t device_word;std::memcpy(&device_word,base+stack-32,4);
  ctx.r11.u64=std::byteswap(device_word);
  ctx.r9.u64=load64(stack+48);
  const uint64_t dirty_address=ctx.r11.u32+(pixel?8:0);
  ctx.r10.u64=load64(dirty_address)|ctx.r9.u64;
  store64(dirty_address,ctx.r10.u64);
  return true;
}
}
