#include "test_main.h"
#include "xdk_draw_submission.h"
#include "pm4_mirror.h"
#include <array>

using namespace superman_returns::native;
namespace {
void Put(std::span<uint8_t> bytes,size_t offset,uint64_t value,size_t count=4) {
  for(size_t i=0;i<count;++i) bytes[offset+i]=uint8_t(value>>((count-1-i)*8));
}
}
SR_TEST(xdk_native_draw_copies_dirty_float_groups_and_keeps_clean_constants) {
  std::array<uint8_t,0x5700> device{};
  std::array<uint8_t,16384> ring{};
  // Dirty bits enumerate groups of four float4s, from the MSB.
  Put(device,0,(uint64_t{1}<<63)|1,8);Put(device,8,uint64_t{1}<<62,8);
  Put(device,48,ring.size());
  for(size_t i=0;i<8192;i+=4) Put(device,0x700+i,0x3f800000u+uint32_t(i));
  auto result=SubmitNativeXdkDraw(device,ring,{false,4,17,23,0,0,0});
  SR_CHECK(result==XdkDrawResult::Submitted);
  Pm4Mirror mirror;mirror.ScanCopy(nullptr,ring.data(),uint32_t(ReadXdkWord(device,40)));
  SR_CHECK_EQ(mirror.reg(0x4000),0x3f800000u);
  SR_CHECK_EQ(mirror.reg(0x43ff),0x3f800ffcu);
  SR_CHECK(!mirror.written(0x4010));
  SR_CHECK_EQ(mirror.reg(0x4410),0x3f801040u);
  SR_CHECK_EQ(mirror.reg(0x2102),17u);
  for(size_t i=0;i<16;++i)SR_CHECK_EQ(device[i],0u);
}
SR_TEST(xdk_native_draw_refuses_unsupported_or_full_ring_without_any_mutation) {
  for(unsigned kind=0;kind<6;++kind) {
    std::array<uint8_t,0x5700> device{};std::array<uint8_t,32> ring{};
    XdkDrawArguments args{false,4,0,3,0,0,0};
    if(kind==0)Put(device,16,uint64_t{1}<<49,8);
    if(kind==1)device[10808]=2;
    if(kind==2)args.count=65536;
    if(kind==3)Put(device,0,uint64_t{1}<<63,8);
    if(kind==4)Put(device,24,1,8);
    if(kind==5)Put(device,32,uint64_t{1}<<49,8);
    auto before=device;auto before_ring=ring;
    SR_CHECK(SubmitNativeXdkDraw(device,ring,args)!=XdkDrawResult::Submitted);
    SR_CHECK(device==before);SR_CHECK(ring==before_ring);
  }
}

SR_TEST(xdk_native_shader_prep_stages_side_effects_until_final_ring_admission) {
  for(bool shader_program:{false,true}) {
    std::array<uint8_t,0x5700> device{};std::array<uint8_t,64> ring{};
    std::array<uint8_t,0x3000> guest{};
    constexpr uint32_t ps=0x1000,vs=0x400,aux=0x2000;
    Put(device,16,(uint64_t{1}<<49)|(uint64_t{1}<<50),8);
    Put(device,10452,4);Put(device,12420,ps);Put(device,12416,vs);Put(device,11536,aux);
    device[10808]=shader_program?0x40:0;
    Put(guest,vs+64,0x100);Put(guest,ps+896,0x100);Put(guest,ps+872,0);
    const uint32_t vs_meta=vs+0x100+40,ps_meta=ps+0x100+872;
    Put(guest,vs_meta+8,0x20000000);Put(guest,vs_meta+20,0x12345);Put(guest,ps_meta+20,0x12345);
    Put(guest,vs_meta+28,0);
    const auto before=device;
    const auto old_ring=ring;
    SR_CHECK(SubmitNativeXdkDraw(device,ring,{false,4,0,3,0,0,0},{guest.data(),guest.size()})==
        XdkDrawResult::RingFull);
    SR_CHECK(device==before);SR_CHECK(ring==old_ring);
  }
}
SR_TEST(xdk_native_draw_index_dma_preserves_base_endian_and_physical_alias) {
  std::array<uint8_t,0x5700> device{};std::array<uint8_t,256> ring{};
  auto result=SubmitNativeXdkDraw(device,ring,{true,4,0xfffffffdu,12,7,0xa0000000u,0xe0001000u});
  SR_CHECK(result==XdkDrawResult::Submitted);
  SR_CHECK_EQ(ReadXdkWord(ring,4),0xfffffffdu);
  SR_CHECK_EQ(ReadXdkWord(ring,16),(12u<<16)|4u|2048u);
  SR_CHECK_EQ(ReadXdkWord(ring,20),0x201cu);
  SR_CHECK_EQ(ReadXdkWord(ring,24),0x40000018u);
}

SR_TEST(xdk_native_draw_preserves_xdk_reserved_ring_tail_before_emitting_draw) {
  // 820FCF90 reserves 160 bytes between +48 and +44. The generated draw
  // checks +48 after flushing constants, before writing its draw packet.
  for(bool indexed:{false,true})for(uint32_t reserve:{76u,77u,100u}) {
    std::array<uint8_t,0x5700> device{};std::array<uint8_t,256> ring{};
    Put(device,0,uint64_t{1}<<63,8);Put(device,48,reserve);
    const auto result=SubmitNativeXdkDraw(device,ring,{indexed,4,0,3,0,0,0});
    // Eight bytes of original Type2 padding, header and 64 bytes end at76.
    SR_CHECK(result==XdkDrawResult::Submitted);
  }
  std::array<uint8_t,0x5700> device{};std::array<uint8_t,256> ring{};
  Put(device,0,uint64_t{1}<<63,8);Put(device,48,75);
  const auto before=device;const auto old_ring=ring;
  SR_CHECK(SubmitNativeXdkDraw(device,ring,{false,4,0,3,0,0,0})==XdkDrawResult::RingFull);
  SR_CHECK(device==before);SR_CHECK(ring==old_ring);
}

SR_TEST(xdk_native_ring_plan_matches_generated_physical_alias_adjustment) {
  for(const auto [write,limit,host]:{std::array<uint64_t,3>{0x10000,0x20000,0x10004},
      {0xc0010000,0xc0020000,0xc0010004},{0xe0010000,0xe0020000,0xe0011004}}) {
    const auto plan=PlanNativeXdkRing(uint32_t(write),uint32_t(limit),0x1000,0x5700);
    SR_CHECK(plan.has_value());
    if(plan){SR_CHECK_EQ(plan->host_offset,host);SR_CHECK_EQ(plan->bytes,0x10000u);}
  }
  for(const auto [write,limit]:{std::array<uint32_t,2>{0xfffffff0,0xfffffffcu},
      {0xdffffff0,0xe0000010},{0x10001,0x20000},{0x1000,0x2000},{0x20000,0x10000}})
    SR_CHECK(!PlanNativeXdkRing(write,limit,0x1000,0x5700));
}

SR_TEST(xdk_native_generic_state_commits_bool_marker_only_after_ring_admission) {
  std::array<uint8_t,0x5700> device{};std::array<uint8_t,16384> ring{};
  Put(device,24,2,8);Put(device,48,16384);
  for(unsigned i=0;i<40;++i) Put(device,9984+i*4,0x10000000u+i);
  const auto before=device;
  SR_CHECK(SubmitNativeXdkDraw(device,std::span(ring).first(164),{false,4,0,3,0,0,0})==XdkDrawResult::RingFull);
  SR_CHECK(device==before);for(auto byte:ring)SR_CHECK_EQ(byte,0u);
  SR_CHECK(SubmitNativeXdkDraw(device,ring,{false,4,0,3,0,0,0})==XdkDrawResult::Submitted);
  Pm4Mirror mirror;mirror.ScanCopy(nullptr,ring.data(),uint32_t(ReadXdkWord(device,40)));
  for(unsigned i=0;i<40;++i)SR_CHECK_EQ(mirror.reg(0x4900+i),0x10000000u+i);
  SR_CHECK_EQ(ReadXdkWord(device,11568),0xffffffffu);
  SR_CHECK_EQ(ReadXdkWord(device,11572),0xff000000u);
}

SR_TEST(xdk_native_fetch_refuses_original_slow_path_hidden_by_float_padding) {
  std::array<uint8_t,0x5700> device{};std::array<uint8_t,1224> ring{};
  uint64_t mask=0;for(unsigned i=0;i<15;++i)mask|=uint64_t{1}<<(63-i*2);
  Put(device,0,mask,8);Put(device,16,uint64_t{1}<<43,8);
  Put(device,40,0x1000c);Put(device,48,0x1000c+1064);
  const auto before=device;const auto old_ring=ring;
  // Original aligned float groups use1200 bytes. Fetch cannot use its fast
  // footer with only24 hard-limit bytes left, even though an unpadded native
  // stream would fit. Refuse without committing dirty masks/packets.
  SR_CHECK(SubmitNativeXdkDraw(device,ring,{false,4,0,3,0,0,0})==XdkDrawResult::RingFull);
  SR_CHECK(device==before);SR_CHECK(ring==old_ring);
}

SR_TEST(xdk_native_admission_reports_exact_unsupported_state_bits) {
  std::array<uint8_t,0x5700> device{};
  Put(device,16,(uint64_t{1}<<5)|(uint64_t{1}<<8)|(uint64_t{1}<<49),8);
  Put(device,24,(uint64_t{1}<<0)|(uint64_t{1}<<2)|(uint64_t{1}<<60),8);
  Put(device,32,(uint64_t{1}<<3)|(uint64_t{1}<<46)|(uint64_t{1}<<49),8);
  device[10808]=2;
  const auto unsupported=InspectNativeXdkUnsupportedState(
      device,{false,4,0,70000,0,0,0});
  SR_CHECK(!unsupported.device_too_small);
  SR_CHECK(unsupported.count_too_large);
  SR_CHECK(unsupported.draw_flags);
  SR_CHECK_EQ(unsupported.state,(uint64_t{1}<<8)|(uint64_t{1}<<49));
  SR_CHECK_EQ(unsupported.context,(uint64_t{1}<<0)|(uint64_t{1}<<60));
  SR_CHECK_EQ(unsupported.misc,(uint64_t{1}<<46)|(uint64_t{1}<<49));
  SR_CHECK(unsupported.any());

  const auto short_device=InspectNativeXdkUnsupportedState(
      std::span<const uint8_t>(device).first(0x100),{false,4,0,3,0,0,0});
  SR_CHECK(short_device.device_too_small);
  SR_CHECK(short_device.any());
}

SR_TEST(xdk_native_draw_direct_mirror_sync_matches_ring_scan_bit_for_bit) {
  {
    Pm4Mirror direct_methods;
    direct_methods.WriteRegister(0x2102, 0x12345678u);
    SR_CHECK_EQ(direct_methods.reg(0x2102), 0x12345678u);
    const std::array<uint8_t, 8> be_words = {
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
    };
    direct_methods.WriteBigEndianRange(0x2200, be_words.data(), 2);
    SR_CHECK_EQ(direct_methods.reg(0x2200), 0x11223344u);
    SR_CHECK_EQ(direct_methods.reg(0x2201), 0x55667788u);
    direct_methods.SetBinMaskLo(0x12345678u);
    direct_methods.SetBinMaskHi(0x9ABCDEF0u);
  }

  for (bool indexed : {false, true}) {
    std::array<uint8_t, 0x5700> device_direct{};
    std::array<uint8_t, 16384> ring_direct{};
    Pm4Mirror mirror_direct;

    Put(device_direct, 0, (uint64_t{1} << 63) | 1, 8);
    Put(device_direct, 8, uint64_t{1} << 62, 8);
    Put(device_direct, 16, uint64_t{1} << 43, 8);
    Put(device_direct, 24, 2, 8);
    Put(device_direct, 48, ring_direct.size());

    for (size_t i = 0; i < 8192; i += 4)
      Put(device_direct, 0x700 + i, 0x3f800000u + uint32_t(i));
    for (size_t i = 0; i < 384; i += 4)
      Put(device_direct, 1024 + i, 0x00010000u + uint32_t(i));
    for (size_t i = 0; i < 160; i += 4)
      Put(device_direct, 9984 + i, 0x01020304u + uint32_t(i));

    auto device_baseline = device_direct;
    auto ring_baseline = ring_direct;
    Pm4Mirror mirror_baseline;

    XdkDrawArguments args{indexed, 4, 17, 23, 0, 0, 0};
    if (indexed) {
      args.index_header = 0xa0000000u;
      args.index_address = 0xe0001000u;
    }

    const auto res_direct = SubmitNativeXdkDrawDirectToMirror(
        device_direct, ring_direct, mirror_direct, args);
    SR_CHECK(res_direct == XdkDrawResult::Submitted);

    const auto res_baseline = SubmitNativeXdkDraw(
        device_baseline, ring_baseline, args);
    SR_CHECK(res_baseline == XdkDrawResult::Submitted);

    const uint32_t written_size = ReadXdkWord(device_baseline, 40);
    mirror_baseline.ScanCopy(nullptr, ring_baseline.data(), written_size);

    SR_CHECK(device_direct == device_baseline);
    SR_CHECK(ring_direct == ring_baseline);

    for (uint32_t r = 0; r < Pm4Mirror::kRegisterCount; ++r) {
      SR_CHECK_EQ(mirror_direct.reg(r), mirror_baseline.reg(r));
    }
    SR_CHECK_EQ(mirror_direct.vs_version, mirror_baseline.vs_version);
    SR_CHECK_EQ(mirror_direct.ps_version, mirror_baseline.ps_version);
    SR_CHECK(mirror_direct.FloatConstants(false) == mirror_baseline.FloatConstants(false));
    SR_CHECK(mirror_direct.FloatConstants(true) == mirror_baseline.FloatConstants(true));
    for (uint32_t slot = 0; slot < 16; ++slot) {
      SR_CHECK_EQ(mirror_direct.fetch_version(slot), mirror_baseline.fetch_version(slot));
      SR_CHECK_EQ(mirror_direct.fetch_complete(slot), mirror_baseline.fetch_complete(slot));
    }
  }
}
