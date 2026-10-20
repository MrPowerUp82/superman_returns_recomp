// Compare allocation traffic rather than wall-clock thresholds: replaying PM4
// must not allocate a second copy of the same captured guest bytes.
#include "../../port/src/graphics/guest/render_packet.h"
#include "../../port/src/graphics/guest/pm4_capture.h"
#include "../../port/src/native_renderer/pm4_mirror.h"
#include "../../port/src/native_renderer/native_frontend.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include "../../port/src/native_renderer/checked_guest_memory.h"
#endif

static thread_local size_t allocated_bytes = 0;
static thread_local size_t allocation_calls = 0;
static thread_local bool counting = false;
void* operator new(size_t size) {
  if (counting) {allocated_bytes += size;++allocation_calls;}
  if (auto p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }

int main() {
  using namespace superman_returns::graphics::guest;
  {
    // A warm arena must not allocate another draw-sized backing store.
    // Count bytes, not pointer equality: malloc may reuse a just-freed address.
    auto pool=std::make_shared<DrawPacketPool>();
    {DrawPacketArena warm(32,true,pool);}
    allocated_bytes=allocation_calls=0;counting=true;
    {DrawPacketArena warm(32,true,pool);}
    counting=false;
    std::printf("Pooled arena: %zu bytes allocated for 32 warm draws\n",allocated_bytes);
    if(allocated_bytes>=sizeof(DrawPacket)) {
      std::fprintf(stderr,"Warm arena allocated a new draw backing store\n");return 1;
    }
    // The frontend flushes at 32 commands, not once per frame. All of a
    // frame's small batches stay alive until the recorder consumes the frame.
    std::vector<DrawPacketOwner> held;held.reserve(64);
    auto batched=std::make_shared<DrawPacketPool>();
    auto frame=[&] {
      for(size_t i=0;i<64;++i) {
        DrawPacketArena arena(32,true,batched);held.push_back(arena.Next());
      }
    };
    frame();held.clear();
    allocated_bytes=0;counting=true;frame();counting=false;
    held.clear();
    std::printf("Pooled batched frame: %zu allocated bytes for 64 warm batches\n",allocated_bytes);
    if(allocated_bytes>=sizeof(DrawPacket)) {
      std::fprintf(stderr,"Pool cannot retain a frame split into small batches\n");return 1;
    }
    // Byte and block limits are independent. Oversized blocks must be freed.
    auto tiny=std::make_shared<DrawPacketPool>(sizeof(DrawPacket),1);
    {DrawPacketArena oversized(32,true,tiny);}
    allocated_bytes=0;counting=true;
    {DrawPacketArena oversized(32,true,tiny);}
    counting=false;
    if(allocated_bytes<32*sizeof(DrawPacket)) {
      std::fprintf(stderr,"Pool retained an oversized allocation\n");return 1;
    }
    auto none=std::make_shared<DrawPacketPool>(128ull*1024*1024,0);
    {DrawPacketArena uncached(32,true,none);}
    allocated_bytes=0;counting=true;
    {DrawPacketArena uncached(32,true,none);}
    counting=false;
    if(allocated_bytes<32*sizeof(DrawPacket)) {
      std::fprintf(stderr,"Zero-block pool retained an allocation\n");return 1;
    }
    // Two returned arenas must not exceed either total idle-byte or block cap.
    for(bool byte_limited:{false,true}) {
      auto bounded=std::make_shared<DrawPacketPool>(byte_limited?32*sizeof(DrawPacket):128ull*1024*1024,byte_limited?8:1);
      {DrawPacketArena first(32,true,bounded),second(32,true,bounded);}
      allocated_bytes=0;counting=true;
      DrawPacketArena first(32,true,bounded);
      counting=false;
      if(allocated_bytes>=sizeof(DrawPacket)) {std::fprintf(stderr,"Pool failed to retain one eligible arena\n");return 1;}
      allocated_bytes=0;counting=true;
      DrawPacketArena second(32,true,bounded);
      counting=false;
      if(allocated_bytes<32*sizeof(DrawPacket)) {std::fprintf(stderr,"Pool exceeded its idle retention limit\n");return 1;}
    }
  }
  WorkBatch batch;
  WorkCmd cmd;
  cmd.op = Op::kDraw;
  cmd.u[0] = 4;
  cmd.range_count = 32;
  // A real Type0 PM4 write to VS c0.x precedes the captured data.
  cmd.ring_bytes = 8;
  batch.bytes = {0, 0, 0x40, 0, 0x3f, 0x80, 0, 0};
  batch.bytes.resize(8 + 32 * 2048, 7);
  for (uint32_t i = 0; i < cmd.range_count; ++i)
    batch.ranges.push_back({0x1000 + i * 2048, 2048, 8 + i * 2048});
  superman_returns::native::Pm4Mirror mirror;
  // Measure steady replay after initializing the captured PM4 version and its
  // immutable banks. Their initial allocations are not per-command storage.
  mirror.ScanCopy(nullptr,batch.bytes.data(),cmd.ring_bytes);
  mirror.ShareFloatConstants(false);mirror.ShareFloatConstants(true);
  std::string error;
  {
    WorkBatch empty;WorkCmd draw;draw.op=Op::kDraw;draw.u[0]=4;
    DrawPacketArena arena(32);std::vector<RenderPacket> packets;packets.reserve(32);
    allocation_calls=0;counting=true;
    for(unsigned i=0;i<32;++i) {
      RenderPacket packet;
      if(!ReplayCapturedRenderPacket(empty,draw,mirror,packet,error,arena.Next())) std::exit(2);
      packets.push_back(std::move(packet));
    }
    counting=false;
    std::printf("Batched payloads: %zu allocations over 32 commands, packet=%zu payload=%zu bytes\n",allocation_calls,sizeof(RenderPacket),sizeof(DrawPacket));
    // One immutable captured-memory header per command; no draw-sized heap
    // allocation, copying or relocation as the frame queue receives packets.
    if(allocation_calls>32) {std::fprintf(stderr,"Replay allocated draw payloads outside the batch arena\n");return 1;}
  }
  {
    auto owned=std::make_shared<WorkBatch>(batch);
    DrawPacketArena arena(32);std::vector<RenderPacket> packets;packets.reserve(32);
    allocated_bytes=allocation_calls=0;counting=true;
    for(unsigned i=0;i<32;++i) {
      RenderPacket packet;
      if(!ReplayCapturedRenderPacket(*owned,cmd,mirror,packet,error,arena.Next(),owned)) std::exit(2);
      packets.push_back(std::move(packet));
    }
    counting=false;
    std::printf("Frozen batch replay: %zu allocations, %zu allocated bytes over 32 captured commands\n",allocation_calls,allocated_bytes);
    if(allocation_calls || allocated_bytes) {std::fprintf(stderr,"Frozen replay copied or allocated captured memory\n");return 1;}
  }
  WorkBatch primary_only;WorkCmd primary_cmd;
  primary_only.bytes.resize(4096,0);
  for(size_t i=0;i<primary_only.bytes.size();i+=4) primary_only.bytes[i]=0x80; // Type2 NOPs.
  primary_cmd.ring_bytes=uint32_t(primary_only.bytes.size());
  GuestMemoryReader reader=[](uint32_t,uint32_t)->std::span<const uint8_t> {return {};};
  allocated_bytes=0;
  counting=true;
  const bool captured=CapturePm4Dependencies(primary_only,primary_cmd,reader,error,&mirror);
  counting=false;
  std::printf("PM4 without dependencies: %zu allocated bytes/command\n",allocated_bytes);
  // Allow the type-erased reader callback, but no primary copy or eagerly
  // allocated container for external dependencies that do not exist.
  if(!captured || allocated_bytes>64) {
    std::fprintf(stderr,"PM4 capture allocated temporary byte storage without dependencies\n");
    return 1;
  }
  WorkBatch growing;WorkCmd dependent;
  growing.bytes={0xc0,0x02,0x2f,0,0,0,1,0,0,0,0,0,0,0,0,1};
  growing.bytes.shrink_to_fit();dependent.ring_bytes=16;
  const std::array<uint8_t,4> constant{0x3f,0x80,0,0};
  GuestMemoryReader dependency_reader=[&](uint32_t,uint32_t)->std::span<const uint8_t> {return constant;};
  allocated_bytes=0;counting=true;
  for(unsigned i=0;i<32;++i) if(!CapturePm4Dependencies(growing,dependent,dependency_reader,error,&mirror)) std::exit(2);
  counting=false;
  std::printf("Growing PM4 batch: %zu allocated bytes over 32 commands\n",allocated_bytes);
  if(allocated_bytes>8192) {std::fprintf(stderr,"Dependency capture exceeds amortized batch growth budget\n");return 1;}
#ifdef _WIN32
  superman_returns::native::CheckedGuestReads reads;
  std::array<uint8_t,128> source{};
  for(unsigned i=0;i<32;++i) reads.Read(source.data(),uint32_t(source.size()));
  reads.Reset();
  allocated_bytes=0;counting=true;
  for(unsigned draw=0;draw<100;++draw) {
    for(unsigned i=0;i<32;++i) if(reads.Read(source.data(),uint32_t(source.size())).size()!=source.size()) std::exit(2);
    reads.Reset();
  }
  counting=false;
  std::printf("Warm checked reads: %zu allocated bytes over 100 commands\n",allocated_bytes);
  if(allocated_bytes) {std::fprintf(stderr,"Checked guest reads recreated their byte storage after Reset\n");return 1;}
#endif
  superman_returns::native::NativeFrontend frontend;
  std::vector<std::array<uint32_t,6>> fetches(1500);
  for(uint32_t i=0;i<fetches.size();++i) {
    fetches[i]={2,i*4096,3,4,5,6};frontend.captured_textures_[fetches[i]].watch_seq=i;
  }
  auto lookup_bench=[&](bool cached) {
    uint64_t checksum=0;
    const auto start=std::chrono::steady_clock::now();
    for(uint32_t draw=0;draw<30000;++draw) for(uint32_t slot=0;slot<32;++slot) {
      const auto& fetch=fetches[(draw/32+slot)%fetches.size()];
      checksum+=(cached?frontend.CapturedTexture(slot,fetch):frontend.captured_textures_[fetch]).watch_seq;
    }
    const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/30000;
    std::printf("Texture lookup %s: %.3f us/draw, checksum=%llu\n",cached?"slot":"map",us,static_cast<unsigned long long>(checksum));
    return checksum;
  };
  if(lookup_bench(false)!=lookup_bench(true)) return 1;
  auto measure = [&](bool replay) {
    allocated_bytes = 0;
    auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 1000; ++i) {
      RenderPacket packet;
      counting = true;
      bool ok = replay ? ReplayCapturedRenderPacket(batch, cmd, mirror, packet, error)
                       : DecodeRenderPacket(batch, cmd, mirror, packet, error);
      counting = false;
      if (!ok) { std::fprintf(stderr, "%s\n", error.c_str()); std::exit(2); }
    }
    auto us = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - start).count() / 1000;
    auto bytes = allocated_bytes / 1000;
    std::printf("%s: %zu allocated bytes/command, %.3f us/command\n",
                replay ? "replay" : "decode", bytes, us);
    return bytes;
  };
  auto decode = measure(false), replay = measure(true);
  if (replay > decode) {
    std::fprintf(stderr, "Replay exceeds the allocation budget of one decode\n");
    return 1;
  }
}
