#include "../native/test_main.h"
#include <rex/audio/xma/context.h>
#include <rex/system/xmemory.h>
#include <array>
#include <cstring>
#include <string_view>
using rex::audio::XmaContext;
using rex::audio::XMA_CONTEXT_DATA;
namespace {
// Same language-supported member access used by the existing final-flush hook.
// No guessed offsets and no changes to the runtime's public interface/layout.
template<class Tag,typename Tag::Type Member> struct Key {friend typename Tag::Type Unlock(Tag){return Member;}};
#define XMA_MEMBER(tag,type,member) struct tag {using Type=type XmaContext::*;friend Type Unlock(tag);}; template struct Key<tag,&XmaContext::member>
using Pcm=std::array<uint8_t,XmaContext::kBytesPerFrameChannel*2>;
XMA_MEMBER(Raw,Pcm,raw_frame_);
XMA_MEMBER(Remaining,uint8_t,current_frame_remaining_subframes_);
XMA_MEMBER(Limit,uint8_t,loop_frame_output_limit_);
XMA_MEMBER(Budget,int32_t,remaining_subframe_blocks_in_output_buffer_);
XMA_MEMBER(Memory,rex::memory::Memory*,memory_);
XMA_MEMBER(Address,uint32_t,guest_ptr_);
struct Fixture {
  rex::memory::Memory memory;XmaContext context;
  bool ready=false;
  static constexpr uint32_t guest=0xa4000000,physical=0x04000000,output=physical+4096,input=physical+16384;
  Fixture() {
    if(!memory.Initialize()) {SR_CHECK(false);return;}
    auto* heap=memory.LookupHeapByType(true,65536);
    if(!heap->AllocFixed(guest,65536,65536,rex::memory::kMemoryAllocationReserve|rex::memory::kMemoryAllocationCommit,
        rex::memory::kMemoryProtectRead|rex::memory::kMemoryProtectWrite)) {SR_CHECK(false);return;}
    std::memset(memory.TranslatePhysical(physical),0,65536);
    context.*Unlock(Memory{})=&memory;context.*Unlock(Address{})=guest;context.set_is_allocated(true);
    XMA_CONTEXT_DATA data(memory.TranslateVirtual(guest));
    data.input_buffer_0_packet_count=1;data.input_buffer_read_offset=32;
    data.output_buffer_ptr=output;data.output_buffer_block_count=16;data.output_buffer_valid=1;
    data.subframe_decode_count=1;data.Store(memory.TranslateVirtual(guest));
    std::memset(memory.TranslatePhysical(output),0xcc,8192);
    auto& raw=context.*Unlock(Raw{});
    for(uint32_t block=0;block<8;++block) std::fill_n(raw.data()+block*256,256,uint8_t(0x20+block));
    ready=true;
  }
  XMA_CONTEXT_DATA Data(){return XMA_CONTEXT_DATA(memory.TranslateVirtual(guest));}
  void Store(XMA_CONTEXT_DATA& data){data.Store(memory.TranslateVirtual(guest));}
  bool Work(){context.Enable();return context.Work();}
  void CheckPcm(uint32_t first,uint32_t blocks,uint32_t write=0,uint32_t capacity=16) {
    const auto* bytes=memory.TranslatePhysical<const uint8_t*>(output);
    for(uint32_t b=0;b<blocks;++b) for(uint32_t i=0;i<256;++i)
      if(bytes[((write+b)%capacity)*256+i]!=0x20+first+b) {SR_CHECK(false);return;}
  }
};
}
SR_TEST(xma_drains_pending_pcm_without_input_and_after_error4) {
  for(uint32_t stereo:{0u,1u}) for(uint32_t sdc:{0u,1u,2u}) for(uint32_t error:{0u,4u}) {
    Fixture f;if(!f.ready)return;auto data=f.Data();data.is_stereo=stereo;data.subframe_decode_count=sdc;data.error_status=error;f.Store(data);
    const uint8_t blocks=uint8_t(4<<stereo);f.context.*Unlock(Remaining{})=blocks;
    SR_CHECK(f.Work());SR_CHECK_EQ(f.context.*Unlock(Remaining{}),0u);
    SR_CHECK_EQ(f.Data().output_buffer_write_offset,blocks);f.CheckPcm(0,blocks);
    SR_CHECK(!f.context.is_enabled());SR_CHECK(!f.context.Work());
  }
}
SR_TEST(xma_capacity_budget_and_wrap_preserve_pending_pcm) {
  Fixture f;if(!f.ready)return;auto data=f.Data();data.is_stereo=1;data.output_buffer_block_count=8;
  data.output_buffer_write_offset=6;data.output_buffer_read_offset=2;data.output_buffer_padding=1;f.Store(data);
  f.context.*Unlock(Remaining{})=8;SR_CHECK(f.Work());
  // Four free blocks, one reserved: three PCM blocks admitted before yielding.
  SR_CHECK_EQ(f.context.*Unlock(Remaining{}),5u);SR_CHECK_EQ(f.Data().output_buffer_write_offset,1u);f.CheckPcm(0,3,6,8);
  data=f.Data();data.output_buffer_read_offset=7;data.output_buffer_valid=1;f.Store(data);
  SR_CHECK(f.Work());SR_CHECK_EQ(f.context.*Unlock(Remaining{}),0u);SR_CHECK_EQ(f.Data().output_buffer_write_offset,6u);
  f.CheckPcm(3,5,1,8);SR_CHECK_EQ(f.context.*Unlock(Budget{}),0);
}
SR_TEST(xma_loop_limit_discards_remainder_and_charges_padding_once) {
  for(uint32_t stereo:{0u,1u}) {
    Fixture f;if(!f.ready)return;auto data=f.Data();data.is_stereo=stereo;data.output_buffer_padding=2;f.Store(data);
    f.context.*Unlock(Remaining{})=uint8_t(4<<stereo);f.context.*Unlock(Limit{})=uint8_t(2<<stereo);
    SR_CHECK(f.Work());SR_CHECK_EQ(f.context.*Unlock(Remaining{}),0u);SR_CHECK_EQ(f.context.*Unlock(Limit{}),0u);
    const uint32_t written=2<<stereo;f.CheckPcm(0,written);SR_CHECK_EQ(f.Data().output_buffer_write_offset,written);
    SR_CHECK_EQ(f.context.*Unlock(Budget{}),int32_t(16-written-2));
  }
}
SR_TEST(xma_consume_only_obeys_capacity_and_drains_available_pcm) {
  for(uint32_t capacity:{1u,2u,16u}) {
    Fixture f;if(!f.ready)return;auto data=f.Data();data.input_buffer_0_packet_count=0;
    data.subframe_decode_count=2;data.output_buffer_padding=1;data.output_buffer_block_count=capacity;f.Store(data);
    f.context.*Unlock(Remaining{})=4;SR_CHECK(f.Work());
    const uint32_t written=capacity<3?0:4;
    SR_CHECK_EQ(f.context.*Unlock(Remaining{}),4-written);SR_CHECK_EQ(f.Data().output_buffer_write_offset,written);f.CheckPcm(0,written);
  }
}
SR_TEST(xma_pending_start_skip_uses_correct_pcm_cursor) {
  for(uint32_t stereo:{0u,1u}) for(uint32_t skipped=0;skipped<=4;++skipped) {
    Fixture f;if(!f.ready)return;auto data=f.Data();data.is_stereo=stereo;f.Store(data);
    const uint32_t first=skipped<<stereo,blocks=(4-skipped)<<stereo;
    f.context.*Unlock(Remaining{})=uint8_t(blocks);SR_CHECK(f.Work());
    SR_CHECK_EQ(f.context.*Unlock(Remaining{}),0u);SR_CHECK_EQ(f.Data().output_buffer_write_offset,blocks);f.CheckPcm(first,blocks);
  }
}
SR_TEST(xma_same_offset_buffer_switch_is_progress_even_with_old_error4) {
  for(uint32_t error:{0u,4u}) {
    Fixture f;if(!f.ready)return;auto data=f.Data();
    data.input_buffer_0_ptr=Fixture::input;data.input_buffer_1_ptr=Fixture::input+2048;
    data.input_buffer_0_valid=data.input_buffer_1_valid=1;data.input_buffer_1_packet_count=1;
    data.error_status=error;f.Store(data);
    auto* packets=f.memory.TranslatePhysical<uint8_t*>(Fixture::input);packets[3]=packets[2048+3]=0xff;
    SR_CHECK(f.Work());const auto after=f.Data();
    SR_CHECK_EQ(after.input_buffer_read_offset,32u);SR_CHECK_EQ(after.input_buffer_0_valid,0u);
    // Preserve one decode attempt with an old error4; without error4 both
    // buffers must be visited despite their identical numerical read offset.
    SR_CHECK_EQ(after.input_buffer_1_valid,error?1u:0u);SR_CHECK_EQ(after.current_buffer,error?1u:0u);
    SR_CHECK_EQ(after.output_buffer_write_offset,0u);
  }
}
SR_TEST(xma_zero_output_capacity_returns_without_modulo_or_pcm_write) {
  Fixture f;if(!f.ready)return;auto data=f.Data();data.output_buffer_block_count=0;f.Store(data);
  f.context.*Unlock(Remaining{})=4;SR_CHECK(f.Work());
  SR_CHECK_EQ(f.context.*Unlock(Remaining{}),4u);SR_CHECK_EQ(f.Data().output_buffer_write_offset,0u);
}
SR_TEST(xma_full_ring_wrap_stops_without_reconstructing_free_capacity) {
  Fixture f;if(!f.ready)return;auto data=f.Data();data.output_buffer_block_count=4;f.Store(data);
  f.context.*Unlock(Remaining{})=4;SR_CHECK(f.Work());
  SR_CHECK_EQ(f.context.*Unlock(Remaining{}),0u);SR_CHECK_EQ(f.context.*Unlock(Budget{}),0);
  SR_CHECK_EQ(f.Data().output_buffer_write_offset,0u);SR_CHECK_EQ(f.Data().output_buffer_valid,0u);
  f.CheckPcm(0,4,0,4);
}
SR_TEST(xma_unavailable_output_or_below_threshold_preserves_pcm_and_guest_fields) {
  for(uint32_t valid:{0u,1u}) {
    Fixture f;if(!f.ready)return;auto data=f.Data();data.output_buffer_block_count=2;
    data.subframe_decode_count=2;data.output_buffer_padding=1;data.output_buffer_valid=valid;
    data.input_buffer_1_ptr=0x12345000;data.unk_dwords_10_15[0]=0xabcdef01;f.Store(data);
    f.context.*Unlock(Remaining{})=4;SR_CHECK(f.Work());
    SR_CHECK_EQ(f.context.*Unlock(Remaining{}),4u);const auto after=f.Data();
    SR_CHECK_EQ(after.output_buffer_write_offset,0u);SR_CHECK_EQ(after.input_buffer_1_ptr,0x12345000u);
    SR_CHECK_EQ(after.unk_dwords_10_15[0],0xabcdef01u);
    const auto* bytes=f.memory.TranslatePhysical<const uint8_t*>(Fixture::output);
    for(size_t i=0;i<512;++i) if(bytes[i]!=0xcc) {SR_CHECK(false);break;}
  }
}
// Run separately so the pre-fix infinite loop is killed by CTest's timeout.
int CheckStall() {
  Fixture f;if(!f.ready)return 1;auto data=f.Data();data.input_buffer_0_valid=1;data.input_buffer_0_ptr=Fixture::input;
  // Out-of-bounds offset in a valid packet makes Decode return unchanged.
  // SDK Release assertions are disabled; Work must yield after one attempt.
  data.input_buffer_read_offset=XmaContext::kBitsPerPacket;f.Store(data);SR_CHECK(f.Work());
  SR_CHECK_EQ(f.context.*Unlock(Remaining{}),0u);SR_CHECK_EQ(f.Data().output_buffer_write_offset,0u);
  return sr_test::Failures()?1:0;
}
int main(int argc,char** argv) {
  if(argc==2 && std::string_view(argv[1])=="--stall")return CheckStall();
  return sr_test::RunAll();
}
