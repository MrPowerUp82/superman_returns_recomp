#include "../../port/src/graphics/guest/parallel_replay.h"
#include "../../port/src/native_renderer/pm4_mirror.h"
#include "../../port/src/native_renderer/game_profile.h"
#include "test_main.h"
using namespace superman_returns::graphics::guest;
using superman_returns::native::Pm4Mirror;
namespace {
void Word(WorkBatch& b,uint32_t v) {for(int s=24;s>=0;s-=8)b.bytes.push_back(uint8_t(v>>s));}
std::shared_ptr<WorkBatch> ChangingBatch() {
  auto b=std::make_shared<WorkBatch>();
  for(uint32_t i=0;i<65;++i) {
    WorkCmd c;c.op=Op::kDraw;c.command_serial=i+1;c.u[0]=4;c.u[2]=3+i;
    c.ring_offset=uint32_t(b->bytes.size());
    Word(*b,0x4000);Word(*b,0x3f800000+i);
    if(i%2==0){Word(*b,0x4400);Word(*b,0x40000000+i);}
    Word(*b,0x4900);Word(*b,i);
    Word(*b,0x4908);Word(*b,100+i);
    c.ring_bytes=uint32_t(b->bytes.size())-c.ring_offset;
    b->cmds.push_back(c);
    if(i==31){WorkCmd pass;pass.op=Op::kPassEnd;pass.u[0]=7;b->cmds.push_back(pass);}
  }
  WorkCmd swap;swap.op=Op::kSwap;swap.u[0]=77;swap.u64=88;b->cmds.push_back(swap);
  return b;
}
}
SR_TEST(parallel_replay_preserves_command_order_and_constant_versions) {
  for(auto schedule:{ReplayScheduling::kContiguous,ReplayScheduling::kInterleaved})
  for(unsigned threads:{1u,2u,3u,4u,5u,6u,7u,8u}) {
    ParallelReplayDecoder decoder(threads,schedule);Pm4Mirror serial,parallel;
    auto b=ChangingBatch();auto results=decoder.Decode(b,parallel);
    SR_CHECK_EQ(results.size(),b->cmds.size());
    for(size_t i=0;i<results.size();++i) {
      auto& r=results[i];SR_CHECK(r.ok);RenderPacket reference;std::string error;
      SR_CHECK(ReplayCapturedRenderPacket(*b,b->cmds[i],serial,reference,error,{},b));
      SR_CHECK_EQ(r.packet.index(),reference.index());
      if(auto* draw=GetDraw(r.packet)) {
        const auto* expected=GetDraw(reference);SR_CHECK(expected);if(!expected)continue;
        auto n=uint32_t(draw->command_serial-1);
        SR_CHECK_EQ(draw->count,3+n);SR_CHECK_EQ(draw->constants.vs[0],0x3f800000+n);
        SR_CHECK_EQ(draw->constants.ps[0],0x40000000+(n&~1u));
        uint32_t boolean=0,loop=0;
        std::memcpy(&boolean,draw->constants.shared.data()+256,4);
        std::memcpy(&loop,draw->constants.shared.data()+304,4);
        SR_CHECK_EQ(boolean,n);SR_CHECK_EQ(loop,100+n);
        SR_CHECK(draw->constants.vs==expected->constants.vs);
        SR_CHECK(draw->constants.ps==expected->constants.ps);
        SR_CHECK(draw->constants.shared==expected->constants.shared);
        SR_CHECK_EQ(r.vs_version,serial.vs_version);SR_CHECK_EQ(r.ps_version,serial.ps_version);
      }
    }
    SR_CHECK_EQ(std::get<SwapPacket>(results.back().packet).guest_swap,88u);
    // Another call must not rewrite prior results or borrow the mutable mirror.
    auto again=decoder.Decode(b,parallel);b.reset();parallel=Pm4Mirror{};
    SR_CHECK_EQ(GetDraw(results[0].packet)->constants.vs[0],0x3f800000u);
    SR_CHECK_EQ(GetDraw(results[64].packet)->count,66u);
    SR_CHECK_EQ(again.size(),results.size());
  }
}
SR_TEST(prepared_replay_snapshot_keeps_written_bits_and_raw_register_values) {
  auto b=std::make_shared<WorkBatch>();WorkCmd c;c.op=Op::kDraw;c.u[0]=4;
  for(auto pair:{std::pair{0x4800u,11u},{0x48bfu,12u},{0x4907u,13u},
                 {0x4927u,14u},{0x2383u,15u},{0x2081u,16u},{0x4000u,0x7fc00001u}}) {
    Word(*b,pair.first);Word(*b,pair.second);
  }
  c.ring_bytes=uint32_t(b->bytes.size());Pm4Mirror m;PreparedReplayCommand p;std::string error;
  SR_CHECK(PrepareReplayCommand(*b,c,m,p,error,b));m=Pm4Mirror{};
  SR_CHECK_EQ(p.state.reg(0x4800),11u);SR_CHECK(p.state.written(0x4800));
  SR_CHECK_EQ(p.state.reg(0x48bf),12u);SR_CHECK(p.state.written(0x48bf));
  SR_CHECK_EQ(p.state.reg(0x4907),13u);SR_CHECK_EQ(p.state.reg(0x4927),14u);
  SR_CHECK(!p.state.written(0x4901));SR_CHECK_EQ(p.state.reg(0x2383),15u);
  SR_CHECK_EQ(p.state.reg(0x2081),16u);
  RenderPacket out;SR_CHECK(DecodePreparedRenderPacket(*b,c,p,out,error));
  SR_CHECK_EQ(GetDraw(out)->constants.vs[0],0u); // normalization of a NaN
  SR_CHECK(sizeof(ReplayDrawState)<1400);
}
SR_TEST(parallel_replay_preparation_failure_returns_only_the_valid_prefix) {
  ParallelReplayDecoder decoder(4);Pm4Mirror m;auto b=ChangingBatch();
  b->cmds[8].pm4_capture_ok=false;
  auto results=decoder.Decode(b,m);SR_CHECK_EQ(results.size(),9u);
  for(size_t i=0;i<8;++i)SR_CHECK(results[i].ok);
  SR_CHECK(!results.back().ok);SR_CHECK(!results.back().error.empty());
  SR_CHECK(!GetDraw(results.back().packet));
  // Decoder remains usable after failure, and empty batches do not wake a job.
  auto empty=std::make_shared<WorkBatch>();SR_CHECK(decoder.Decode(empty,m).empty());
  b->cmds.resize(4);auto again=decoder.Decode(b,m);SR_CHECK_EQ(again.size(),4u);
  for(auto& r:again)SR_CHECK(r.ok);
}
SR_TEST(parallel_replay_decode_failure_does_not_publish_partial_draw) {
  ParallelReplayDecoder decoder(2);Pm4Mirror m;auto b=ChangingBatch();
  b->cmds[5].u[0]=UINT32_MAX;
  auto results=decoder.Decode(b,m);SR_CHECK(!results[5].ok);
  SR_CHECK(!GetDraw(results[5].packet));SR_CHECK(!results[5].error.empty());
  for(size_t i=0;i<5;++i)SR_CHECK(results[i].ok);
}
SR_TEST(parallel_device_decode_preserves_overlays_fetch_fallback_and_geometry) {
  namespace profile=superman_returns::native::profile;
  auto batch=std::make_shared<WorkBatch>();batch->bytes.resize(0x4080);
  auto store=[&](uint32_t offset,uint32_t value) {
    for(uint32_t j=0;j<4;++j)batch->bytes[offset+j]=uint8_t(value>>(24-j*8));
  };
  store(profile::kDevice.vs_bools,73);store(profile::kDevice.ps_loops+15*4,91);
  store(profile::kDevice.viewport+8,1280);store(profile::kDevice.viewport+12,720);
  store(0x4018,1);store(0x4038,0x2c2259); // declaration: USHORT2, POSITION0
  for(uint32_t i=0;i<3;++i){store(0x4040+i*4,0x01020304+i);store(0x4070+i*4,i+1);}
  StreamPlan stream;stream.stride=4;stream.size=12;stream.buffer.address=0x1000;
  stream.buffer.size=12;stream.buffer.end=12;stream.buffer.action=2;stream.buffer.decl=0x3000;
  batch->streams.push_back(stream);
  for(uint32_t i=0;i<33;++i) {
    WorkCmd c;c.op=Op::kDrawIndexed;c.device=0x10000;c.command_serial=i+1;c.u[0]=4;c.u[3]=3;
    c.stream_count=1;c.has_index=true;c.index32=true;c.index.address=0x2000;
    c.index.size=12;c.index.end=12;c.index.action=2;c.index.format=2|(2<<2);
    c.range_first=uint32_t(batch->ranges.size());c.range_count=5;
    batch->ranges.insert(batch->ranges.end(),{{0x10000,0x4000,0},{0x3000,0x40,0x4000},
        {0xa0001000,12,0x4040},{0xa0002000,12,0x4070},
        {0x10000+profile::kDevice.fetch_constants+4,4,uint32_t(batch->bytes.size())}});
    Word(*batch,200+i);
    c.ring_offset=uint32_t(batch->bytes.size());Word(*batch,0x4800);Word(*batch,300+i);c.ring_bytes=8;
    batch->cmds.push_back(c);
  }
  for(auto schedule:{ReplayScheduling::kContiguous,ReplayScheduling::kInterleaved})
  for(unsigned threads:{1u,2u,3u,4u,5u,6u,7u,8u}) {
    ParallelReplayDecoder decoder(threads,schedule);Pm4Mirror m,serial;
    auto results=decoder.Decode(batch,m);
    for(size_t i=0;i<results.size();++i) {
      SR_CHECK(results[i].ok);if(!results[i].ok)continue;
      RenderPacket reference;std::string error;
      SR_CHECK(ReplayCapturedRenderPacket(*batch,batch->cmds[i],serial,reference,error,{},batch));
      const auto* actual=GetDraw(results[i].packet);const auto* expected=GetDraw(reference);
      SR_CHECK(actual && expected);if(!actual || !expected)continue;
      SR_CHECK_EQ(actual->texture_fetch[0][0],300+i);SR_CHECK_EQ(actual->texture_fetch[0][1],200+i);
      SR_CHECK(actual->texture_fetch==expected->texture_fetch);SR_CHECK(actual->registers==expected->registers);
      SR_CHECK(actual->constants.shared==expected->constants.shared);
      uint32_t boolean=0,loop=0;
      std::memcpy(&boolean,actual->constants.shared.data()+256,4);
      std::memcpy(&loop,actual->constants.shared.data()+304+31*4,4);
      SR_CHECK_EQ(boolean,73u);SR_CHECK_EQ(loop,91u);
      SR_CHECK(actual->indices.normalized==std::vector<uint32_t>({1,2,3}));
      SR_CHECK(actual->indices.normalized==expected->indices.normalized);
      SR_CHECK(actual->streams[0].update.bytes==expected->streams[0].update.bytes);
      SR_CHECK_EQ(actual->vertex_fetch[0].type,0x2c2259u);
    }
  }
}
namespace superman_returns::graphics::guest {
struct ReplayTestAccess {
  static inline size_t failure_index=0;
  static void Before(size_t index) {if(index==failure_index)throw std::runtime_error("Injected shard failure");}
  static void FailAt(ParallelReplayDecoder& decoder,size_t index) {
    failure_index=index;decoder.before_entry_=&Before;
  }
  static void Clear(ParallelReplayDecoder& decoder) {decoder.before_entry_=nullptr;}
};
}
SR_TEST(replay_execution_limit_includes_first_error_only) {
  std::vector<ReplayResult> results(9);
  SR_CHECK_EQ(ReplayExecutionLimit({}),0u);
  for(auto& result:results)result.ok=true;
  SR_CHECK_EQ(ReplayExecutionLimit(results),9u);
  results[5].ok=false;SR_CHECK_EQ(ReplayExecutionLimit(results),6u);
  results[0].ok=false;SR_CHECK_EQ(ReplayExecutionLimit(results),1u);
}
SR_TEST(parallel_replay_small_and_mixed_batches_preserve_results) {
  for(auto schedule:{ReplayScheduling::kContiguous,ReplayScheduling::kInterleaved})
  for(unsigned n=1;n<=8;++n) {
    ParallelReplayDecoder decoder(n,schedule);
    for(size_t count:{0u,1u,3u,65u}) {
      auto batch=ChangingBatch();batch->cmds.resize(count);
      if(count>3){WorkCmd resolve;resolve.op=Op::kResolve;batch->cmds.insert(batch->cmds.begin()+2,resolve);}
      Pm4Mirror mirror,serial;auto results=decoder.Decode(batch,mirror);
      SR_CHECK_EQ(results.size(),batch->cmds.size());
      for(size_t i=0;i<results.size();++i) {
        RenderPacket reference;std::string error;
        SR_CHECK(ReplayCapturedRenderPacket(*batch,batch->cmds[i],serial,reference,error,{},batch));
        SR_CHECK(results[i].ok);SR_CHECK_EQ(results[i].packet.index(),reference.index());
        if(auto* actual=GetDraw(results[i].packet)) {
          const auto* expected=GetDraw(reference);SR_CHECK(expected);if(!expected)continue;
          SR_CHECK(actual->constants.vs==expected->constants.vs);
          SR_CHECK(actual->constants.ps==expected->constants.ps);
          SR_CHECK_EQ(actual->command_serial,expected->command_serial);
        }
      }
    }
  }
}
SR_TEST(parallel_replay_injected_shard_exception_joins_and_recovers) {
  for(auto schedule:{ReplayScheduling::kContiguous,ReplayScheduling::kInterleaved})
  for(unsigned n=1;n<=8;++n) {
    ParallelReplayDecoder decoder(n,schedule);
    for(unsigned shard=0;shard<n;++shard) {
      auto batch=ChangingBatch();batch->cmds.resize(65);
      const size_t failure=schedule==ReplayScheduling::kContiguous?65*shard/n:shard;
      ReplayTestAccess::FailAt(decoder,failure);
      Pm4Mirror mirror;auto results=decoder.Decode(batch,mirror);
      SR_CHECK_EQ(results.size(),65u);SR_CHECK(!results[failure].ok);
      SR_CHECK(!results[failure].error.empty());SR_CHECK(!GetDraw(results[failure].packet));
      SR_CHECK_EQ(ReplayExecutionLimit(results),failure+1);
      ReplayTestAccess::Clear(decoder);
      auto again=decoder.Decode(batch,mirror);
      for(const auto& result:again)SR_CHECK(result.ok);
      batch->cmds[8].pm4_capture_ok=false;
      auto prefix=decoder.Decode(batch,mirror);
      SR_CHECK_EQ(prefix.size(),9u);SR_CHECK(!prefix.back().ok);
      for(size_t i=0;i<8;++i)SR_CHECK(prefix[i].ok);
    }
  }
}
