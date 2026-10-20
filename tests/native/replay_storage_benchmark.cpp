// Manual Release diagnostic: time arena construction and complete owned replay
// separately. Keep decoded payloads alive as the Vulkan packet queue does.
#include "../../port/src/graphics/guest/render_packet.h"
#include "../../port/src/graphics/guest/parallel_replay.h"
#include "../../port/src/native_renderer/pm4_mirror.h"
#include "../../port/src/native_renderer/game_profile.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string_view>
#include <charconv>
using namespace superman_returns::graphics::guest;
int main(int argc,char** argv) {
  bool pooled=false,batched=false,vary=false,device=false;size_t blocks=128,requested_batch=32;unsigned threads=0;
  for(int i=1;i<argc;++i) {
    std::string_view arg(argv[i]);
    if(arg=="--pool") pooled=true;
    else if(arg=="--batched") batched=true;
    else if(arg=="--vary-banks") vary=true;
    else if(arg=="--device")device=true;
    else if(arg.starts_with("--batch-draws=")) {
      arg.remove_prefix(14);auto result=std::from_chars(arg.data(),arg.data()+arg.size(),requested_batch);
      if(result.ec!=std::errc{} || result.ptr!=arg.data()+arg.size() || !requested_batch || requested_batch>3008)return 4;
      batched=true;
    }
    else if(arg.starts_with("--threads=")) {
      arg.remove_prefix(10);auto result=std::from_chars(arg.data(),arg.data()+arg.size(),threads);
      if(result.ec!=std::errc{} || result.ptr!=arg.data()+arg.size() || threads<1 || threads>4)return 4;
    }
    else if(arg.starts_with("--pool-blocks=")) {
      arg.remove_prefix(14);
      auto result=std::from_chars(arg.data(),arg.data()+arg.size(),blocks);
      if(result.ec!=std::errc{} || result.ptr!=arg.data()+arg.size() || blocks>4096) return 4;
    } else return 4;
  }
  auto pool=pooled?std::make_shared<DrawPacketPool>(128ull*1024*1024,blocks):nullptr;
  constexpr size_t draws=3008,rounds=10;
  auto batch=std::make_shared<WorkBatch>();WorkCmd cmd;cmd.op=Op::kDraw;cmd.u[0]=4;cmd.u[2]=3;
  if(device) {
    namespace profile=superman_returns::native::profile;
    batch->bytes.resize(0x4000);batch->ranges={{0x10000,0x4000,0}};cmd.device=0x10000;cmd.range_count=1;
    auto store=[&](uint32_t offset,uint32_t value) {
      for(uint32_t i=0;i<4;++i)batch->bytes[offset+i]=uint8_t(value>>(24-i*8));
    };
    store(profile::kDevice.viewport+8,1280);store(profile::kDevice.viewport+12,720);
    for(auto range:profile::kRegisterShadow)
      for(uint32_t i=0;i<range.count;++i)store(range.offset+i*4,(range.first+i)*17);
    for(uint32_t i=0;i<192;++i)store(profile::kDevice.fetch_constants+i*4,i*31);
  }
  std::vector<std::pair<uint32_t,uint32_t>> ring(draws);
  uint64_t expected_banks=0;uint32_t ps_value=0;
  auto put=[&](uint32_t word) {for(int shift=24;shift>=0;shift-=8)batch->bytes.push_back(uint8_t(word>>shift));};
  for(size_t i=0;i<draws;++i) {
    const uint32_t offset=uint32_t(batch->bytes.size());
    // Change ~70% of VS banks and ~40% of PS banks, close to observed gameplay.
    if(vary && i%10<7){put(0x4000);put(0x3f000000u+uint32_t(i));}
    if(vary && i%10<4){ps_value=0x3f000000u+uint32_t(i);put(0x4400);put(ps_value);}
    ring[i]={offset,uint32_t(batch->bytes.size())-offset};expected_banks+=ps_value;
  }
  superman_returns::native::Pm4Mirror mirror;
  std::unique_ptr<ParallelReplayDecoder> decoder;
  if(threads)decoder=std::make_unique<ParallelReplayDecoder>(threads);
  const size_t batch_draws=batched?requested_batch:draws;
  std::vector<std::shared_ptr<WorkBatch>> batches;
  for(size_t offset=0;offset<draws;offset+=batch_draws) {
    auto part=std::make_shared<WorkBatch>();part->bytes=batch->bytes;part->ranges=batch->ranges;
    for(size_t i=offset;i<std::min(offset+batch_draws,draws);++i) {
      auto next=cmd;next.command_serial=i+1;next.ring_offset=ring[i].first;next.ring_bytes=ring[i].second;
      part->cmds.push_back(std::move(next));
    }
    batches.push_back(std::move(part));
  }
  std::vector<double> setup,decode,total,release,cycle;uint64_t checksum=0;
  // Keep the preceding batch alive while replay creates the next one, as the
  // asynchronous recorder does. Include its final release in the cycle cost.
  std::vector<RenderPacket> previous;
  for(size_t round=0;round<rounds;++round) {
    std::vector<RenderPacket> packets;packets.reserve(draws);std::string error;
    const auto begin=std::chrono::steady_clock::now();
    std::chrono::steady_clock::duration setup_time{},decode_time{};
    for(const auto& part:batches) {
      const auto setup_begin=std::chrono::steady_clock::now();
      if(decoder) {
        auto results=decoder->Decode(part,mirror,nullptr,true,pool);
        for(auto& result:results) {
          if(!result.ok){std::fprintf(stderr,"Parallel replay failed: %s\n",result.error.c_str());return 1;}
          packets.push_back(std::move(result.packet));
        }
        decode_time+=std::chrono::steady_clock::now()-setup_begin;
        continue;
      }
      DrawPacketArena arena(part->cmds.size(),true,pool);
      const auto initialized=std::chrono::steady_clock::now();
      setup_time+=initialized-setup_begin;
      for(const auto& next:part->cmds) {
        RenderPacket packet;
        if(!ReplayCapturedRenderPacket(*part,next,mirror,packet,error,arena.Next(),part)) {
          std::fprintf(stderr,"Replay failed: %s\n",error.c_str());return 1;
        }
        packets.push_back(std::move(packet));
      }
      decode_time+=std::chrono::steady_clock::now()-initialized;
    }
    const auto filled=std::chrono::steady_clock::now();
    for(auto& packet:packets) {
      const auto* draw=GetDraw(packet);if(!draw) return 2;
      checksum+=draw->command_serial+draw->constants.shared.back()+draw->vertex_fetch.back().type+draw->constants.ps[0];
    }
    const auto release_begin=std::chrono::steady_clock::now();
    previous.clear();previous.swap(packets);
    const auto released=std::chrono::steady_clock::now();
    if(round>=2) {
      auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
      setup.push_back(std::chrono::duration<double,std::milli>(setup_time).count());
      decode.push_back(std::chrono::duration<double,std::milli>(decode_time).count());total.push_back(ms(begin,filled));
      release.push_back(ms(release_begin,released));cycle.push_back(ms(begin,released));
    }
  }
  auto median=[](auto values){std::sort(values.begin(),values.end());return (values[values.size()/2-1]+values[values.size()/2])/2;};
  if(checksum!=rounds*(draws*(draws+1)/2+expected_banks)) return 3;
  std::printf("replay_storage_bench threads=%u pool=%d vary_banks=%d device=%d packet_bytes=%zu packet_constants_bytes=%zu batch_draws=%zu pool_blocks=%zu overlap=2 draws=%zu rounds=%zu arena_ms=%.4f decode_ms=%.4f total_ms=%.4f release_ms=%.4f cycle_ms=%.4f checksum=%llu\n",
              threads,int(pooled),int(vary),int(device),sizeof(DrawPacket),sizeof(PacketConstants),batch_draws,blocks,draws,rounds-2,median(setup),median(decode),median(total),median(release),median(cycle),static_cast<unsigned long long>(checksum));
}
