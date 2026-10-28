#include <rex/ppc.h>
#include "../../port/src/native_renderer/xdk_draw_submission.h"
#include "../../port/src/native_renderer/pm4_mirror.h"
#include <cstdio>
#include <memory>
#include <vector>
#include <random>
#include <cstdlib>
REX_FUNC(reference_draw);
REX_FUNC(reference_indexed);
using namespace superman_returns::native;
void Put(uint8_t* bytes,size_t offset,uint64_t value,unsigned count=4) {
  for(unsigned i=0;i<count;++i)bytes[offset+i]=uint8_t(value>>((count-1-i)*8));
}
int main() {
  constexpr size_t bytes=0x60000,dev=0x1000,index=0x8000;
  auto release=[](uint8_t* p){_aligned_free(p);};
  std::unique_ptr<uint8_t,decltype(release)> expected(static_cast<uint8_t*>(_aligned_malloc(bytes,32)),release);
  std::unique_ptr<uint8_t,decltype(release)> actual(static_cast<uint8_t*>(_aligned_malloc(bytes,32)),release);
  if(!expected || !actual)return 2;
  for(bool indexed:{false,true}) for(bool shader_program:{false,true}) for(bool alt_ones:{false,true})
      for(uint64_t shader_mask:{uint64_t{1}<<49,(uint64_t{1}<<49)|(uint64_t{1}<<50),
          (uint64_t{1}<<49)|(uint64_t{1}<<51)|(uint64_t{1}<<52),
          (uint64_t{1}<<49)|(uint64_t{1}<<50)|(uint64_t{1}<<51)|(uint64_t{1}<<52)}) {
    constexpr uint32_t ring=0x10000,ps=0x20000,vs=0x24000,aux=0x28000;
    std::memset(expected.get(),0,bytes);
    Put(expected.get(),dev+16,shader_mask,8);
    Put(expected.get(),dev+40,ring);Put(expected.get(),dev+44,0x50000);Put(expected.get(),dev+48,0x50000);
    Put(expected.get(),dev+10432,0x1234);Put(expected.get(),dev+10452,4);
    Put(expected.get(),dev+12420,ps);Put(expected.get(),dev+12416,vs);Put(expected.get(),dev+11536,aux);
    Put(expected.get(),vs+64,0x100);Put(expected.get(),ps+896,0x100);Put(expected.get(),ps+872,0);
    const uint32_t vs_meta=vs+0x100+40,ps_meta=ps+0x100+872;
    Put(expected.get(),vs_meta+8,0x20000000);Put(expected.get(),vs_meta+12,0x01020304);
    Put(expected.get(),ps_meta+8,0x04000000);Put(expected.get(),ps_meta+12,0x10203040);
    Put(expected.get(),vs_meta+20,0x12345);Put(expected.get(),ps_meta+20,0x12345);Put(expected.get(),vs_meta+28,0);
    if(shader_mask&(uint64_t{1}<<51)) {
      Put(expected.get(),ps+40,0xabc00123);Put(expected.get(),aux+48,0xabc00123);
      Put(expected.get(),ps+48,0x1122334455667788ull,8);Put(expected.get(),dev+12256,0x1122334455667788ull,8);
      Put(expected.get(),ps+56,0x8877665544332211ull,8);Put(expected.get(),dev+12264,0x8877665544332211ull,8);
      Put(expected.get(),aux+32,~uint64_t{0},8);Put(expected.get(),aux+40,~uint64_t{0},8);
      Put(expected.get(),dev+10780,0x13579bdf);
    }
    Put(expected.get(),ps+904,0x200);
    Put(expected.get(),ps+0x200+880,alt_ones?0xffffffffu:0x00a0b0c0);
    Put(expected.get(),ps+0x200+884,0x0d0e0f01);
    Put(expected.get(),dev+10408,0x55);Put(expected.get(),dev+12432,0x777);Put(expected.get(),dev+12436,0x888);
    expected.get()[dev+10808]=shader_program?0x40:0;
    Put(expected.get(),dev+12164,index);Put(expected.get(),index,0);Put(expected.get(),index+24,0x30000);
    std::memcpy(actual.get(),expected.get(),bytes);
    PPCContext ctx{};ctx.r1.u64=0xf000;ctx.r3.u64=dev;ctx.r4.u64=5;ctx.r5.u64=7;
    ctx.r6.u64=indexed?3:11;ctx.r7.u64=11;
    const XdkDrawArguments args{indexed,ctx.r4.u32,ctx.r5.u32,indexed?ctx.r7.u32:ctx.r6.u32,
        indexed?ctx.r6.u32:0,0,0x30000};
    (indexed?reference_indexed:reference_draw)(ctx,expected.get());
    const auto result=SubmitNativeXdkDraw({actual.get()+dev,0x5700},
        {actual.get()+ring+4,0x50000-ring},args,{actual.get(),bytes});
    if(result!=XdkDrawResult::Submitted) {
      std::fprintf(stderr,"Refused shader-prep fixture indexed=%d program=%d mask=%llx\n",
          indexed,shader_program,(unsigned long long)(shader_mask>>49));return 1;
    }
    const auto expected_end=ReadXdkWord({expected.get()+dev,0x5700},40);
    const auto actual_end=ReadXdkWord({actual.get()+dev,0x5700},40);
    if(expected_end!=actual_end || std::memcmp(expected.get()+ring+4,actual.get()+ring+4,expected_end-ring)) {
      std::fprintf(stderr,"Shader-prep command stream differs indexed=%d program=%d mask=%llx expected_end=%x actual_end=%x\n",
          indexed,shader_program,(unsigned long long)(shader_mask>>49),expected_end,actual_end);
      const size_t expected_words=(expected_end-ring)/4,actual_words=(actual_end-ring)/4;
      const size_t words=expected_words>actual_words?expected_words:actual_words;
      for(size_t i=0;i<words;++i) {
        const uint32_t ew=i<expected_words?ReadXdkWord({expected.get()+ring+4,expected_end-ring},i*4):0xffffffffu;
        const uint32_t aw=i<actual_words?ReadXdkWord({actual.get()+ring+4,actual_end-ring},i*4):0xffffffffu;
        std::fprintf(stderr,"%02zu: %08x %08x%s\n",i,ew,aw,ew==aw?"":" *");
      }
      return 1;
    }
    for(size_t at=0;at<0x5700;++at) if((at<40 || at>=44) && expected.get()[dev+at]!=actual.get()[dev+at]) {
      std::fprintf(stderr,"Shader-prep side effect differs indexed=%d program=%d mask=%llx offset=%zx expected=%02x actual=%02x\n",
          indexed,shader_program,(unsigned long long)(shader_mask>>49),at,expected.get()[dev+at],actual.get()[dev+at]);return 1;
    }
    if(expected.get()[ps+64]!=actual.get()[ps+64] ||
        std::memcmp(expected.get()+ps+64,actual.get()+ps+64,4)) {
      std::fprintf(stderr,"Shader-prep guest cache stamp differs indexed=%d program=%d mask=%llx\n",
          indexed,shader_program,(unsigned long long)(shader_mask>>49));return 1;
    }
  }
  std::mt19937_64 random(0x454107ed);unsigned cases=0;
  for(bool indexed:{false,true}) for(unsigned test=0;test<1024;++test) {
    for(size_t i=0;i<bytes;i+=8) {const auto value=random();std::memcpy(expected.get()+i,&value,8);}
    const uint32_t ring=0x10000+(test%4)*4;
    std::memset(expected.get()+dev,0,40);
    Put(expected.get(),dev,test<64?uint64_t{1}<<test:random(),8);
    Put(expected.get(),dev+8,test<64?uint64_t{1}<<(63-test):random(),8);
    constexpr uint64_t allowed[3]={((uint64_t{1}<<49)-1)&~uint64_t{256},
        (uint64_t{1}<<60)-2,(uint64_t{1}<<46)-1};
    for(unsigned bank=0;bank<3;++bank) {
      const uint64_t dirty=test<192?(bank==test/64?uint64_t{1}<<(test%64):0):random();
      Put(expected.get(),dev+16+bank*8,(test==1023?~uint64_t{0}:dirty)&allowed[bank],8);
    }
    if(test==1023){Put(expected.get(),dev,~uint64_t{0},8);Put(expected.get(),dev+8,~uint64_t{0},8);}
    Put(expected.get(),dev+40,ring);Put(expected.get(),dev+44,0x50000);Put(expected.get(),dev+48,0x50000);
    expected.get()[dev+10808]=0;expected.get()[dev+11915]=uint8_t(test);
    Put(expected.get(),dev+12164,index);
    const uint32_t header=(uint32_t(random())&0xe0000000u),address=uint32_t(random());
    Put(expected.get(),index,header);Put(expected.get(),index+24,address);
    std::memcpy(actual.get(),expected.get(),bytes);
    PPCContext ctx{};ctx.r1.u64=0xf000;ctx.r3.u64=dev;ctx.r4.u64=test%32;
    ctx.r5.u64=uint32_t(random());ctx.r6.u64=indexed?uint32_t(random()):uint16_t(random());ctx.r7.u64=uint16_t(random());
    const XdkDrawArguments args{indexed,ctx.r4.u32,ctx.r5.u32,indexed?ctx.r7.u32:ctx.r6.u32,
        indexed?ctx.r6.u32:0,header,address};
    (indexed?reference_indexed:reference_draw)(ctx,expected.get());
    const auto result=SubmitNativeXdkDraw({actual.get()+dev,0x5700},
        {actual.get()+ring+4,0x50000-ring},args);
    if(result!=XdkDrawResult::Submitted){std::fprintf(stderr,"Refused fixture %u\n",test);return 1;}
    Pm4Mirror a,b;
    const auto old_end=ReadXdkWord({expected.get()+dev,0x5700},40),new_end=ReadXdkWord({actual.get()+dev,0x5700},40);
    // Native fast submission retains original alignment and packet grouping,
    // so compare the whole command stream as well as its interpreted state.
    if(old_end!=new_end || std::memcmp(expected.get()+ring+4,actual.get()+ring+4,old_end-ring)) {
      std::fprintf(stderr,"Full command stream differs indexed=%d fixture=%u\n",indexed,test);return 1;
    }
    // The mirror intentionally ignores DRAW_INDX payloads. Compare the final
    // VGT offset + complete draw packet as well, including visibility, count,
    // index32, physical address and endian/DMA length, after constant padding.
    const size_t draw_bytes=indexed?28:20;
    if(old_end-ring<draw_bytes || new_end-ring<draw_bytes ||
        std::memcmp(expected.get()+old_end+4-draw_bytes,actual.get()+new_end+4-draw_bytes,draw_bytes)) {
      std::fprintf(stderr,"Draw packet differs indexed=%d fixture=%u\n",indexed,test);return 1;
    }
    a.ScanCopy(nullptr,expected.get()+ring+4,old_end-ring);b.ScanCopy(nullptr,actual.get()+ring+4,new_end-ring);
    for(uint32_t reg=0;reg<Pm4Mirror::kRegisterCount;++reg)
      if(a.reg(reg)!=b.reg(reg) || a.written(reg)!=b.written(reg)) {
        std::fprintf(stderr,"PM4 mismatch indexed=%d fixture=%u reg=%x expected=%x actual=%x\n",indexed,test,reg,a.reg(reg),b.reg(reg));return 1;
      }
    for(size_t at=0;at<0x5700;++at)if((at<40 || at>=44) && expected.get()[dev+at]!=actual.get()[dev+at]) {
      std::fprintf(stderr,"Device side effect differs indexed=%d fixture=%u offset=%zx\n",indexed,test,at);return 1;
    }
    ++cases;
  }
  std::printf("%u generated XDK/native draws: PM4 state and device side effects matched\n",cases);
}
