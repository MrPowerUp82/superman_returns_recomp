#include "coop_hud.h"
#include "coop_hud_packet.h"
#include "coop_second_camera.h"
#include "coop_split_render.h"
#include "native_renderer/native_bridge.h"
#include <atomic>
#include <mutex>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include "generated/default/superman_returns_init.h"
#include "title_benchmark.h"
#ifdef _WIN32
#include <windows.h>
#endif
REXCVAR_DECLARE(bool,sr_coop_split_preview);
REXCVAR_DECLARE(bool,sr_coop_input);
REXCVAR_DECLARE(bool,sr_coop_camera_transport);
REXCVAR_DECLARE(bool,sr_coop_second_camera);
namespace superman_returns::coop {
namespace {
constexpr uint32_t kManagerSize=3232,kBinSize=96,kCacheOffset=112,kCacheSize=1648;
std::atomic<uint32_t> scratch{0};
std::mutex snapshot_mutex;
hud::Snapshot snapshot;
struct Producer {
  bool active=false,captured=false,selected=false;
  uint32_t bin=0,key=0;
  hud::Snapshot value;
};
thread_local Producer producer;
thread_local bool drawing_radar=false,drawing_secondary=false,map_aspect=false,retargeted=false;
thread_local bool packet_verified=false,aspect_applied=false;
thread_local hud::Snapshot consumed;
thread_local uint32_t expected_key=0;
bool Enabled() {
  static const bool enabled=REXCVAR_GET(sr_coop_split_preview)&&REXCVAR_GET(sr_coop_input)&&
      REXCVAR_GET(sr_coop_camera_transport)&&REXCVAR_GET(sr_coop_second_camera)&&!benchmark::Active();
  return enabled;
}
bool Read(uint8_t* base,uint32_t address,void* out,size_t size) {
#ifdef _WIN32
  SIZE_T copied=0;
  return address && uint64_t(address)+size<=0x100000000ull &&
      ReadProcessMemory(GetCurrentProcess(),base+address,out,size,&copied)&&copied==size;
#else
  return false;
#endif
}
bool Write(uint8_t* base,uint32_t address,const void* in,size_t size) {
#ifdef _WIN32
  SIZE_T copied=0;
  return address && uint64_t(address)+size<=0x100000000ull &&
      WriteProcessMemory(GetCurrentProcess(),base+address,in,size,&copied)&&copied==size;
#else
  return false;
#endif
}
uint32_t Word(uint8_t* base,uint32_t address) {
  std::array<uint8_t,4> bytes{};
  return Read(base,address,bytes.data(),4)?packet::ReadWord(bytes,0):0;
}
struct RestoreContext {PPCContext& ctx;PPCContext saved;~RestoreContext(){ctx=saved;}};
void Publish(hud::Snapshot value) {std::lock_guard lock(snapshot_mutex);snapshot=value;}
hud::Snapshot Sample() {std::lock_guard lock(snapshot_mutex);return snapshot;}
void LogDraw(const SplitHudContext& view,bool restored) {
  if(!packet_verified || !(view.frame<=3 || view.frame%120==0))return;
  REXLOG_INFO("coop radar draw: frame={} generation={} view={} target={} retargeted={} packet_verified=true cache_restored={} aspect_applied={} position={},{},{} terrain={} heading={}",
      view.frame,view.generation,view.view,consumed.target,retargeted,restored,aspect_applied,
      std::bit_cast<float>(packet::ReadWord(consumed.anchor,0)),
      std::bit_cast<float>(packet::ReadWord(consumed.anchor,4)),
      std::bit_cast<float>(packet::ReadWord(consumed.anchor,8)),
      std::bit_cast<float>(packet::ReadWord(consumed.anchor,16)),
      std::bit_cast<float>(packet::ReadWord(consumed.anchor,20)));
}

void Produce(PPCContext& ctx,uint8_t* base,uint32_t manager) {
  // Retain the last complete anchor while preparing its replacement. Clearing
  // first creates a cross-thread window where an otherwise valid P2 radar
  // disappears. Failure still invalidates it; join generations never alias.
  struct Publication {
    bool complete=false;
    ~Publication(){if(!complete)Publish({});}
  } publication;
  const auto camera=SampleSecondCamera();
  const auto block=scratch.load(std::memory_order_acquire);
  if(!block || !camera.view || manager!=Word(base,0x82970174) ||
     camera.primary_owner!=Word(base,0x8296D86C) || !native::CoopSplitSupported())return;
  RestoreContext restore{ctx,ctx};
  ctx.r3.u64=camera.target;sub_821C23E0(ctx,base);
  const auto actor=ctx.r3.u32;
  if(!actor)return;
  std::array<uint8_t,kManagerSize> original{},after{};
  std::array<uint8_t,kBinSize> bin{};
  if(!Read(base,manager,original.data(),original.size()) || !Write(base,block,original.data(),original.size()))return;
  const auto bin_address=block+kManagerSize;
  packet::WriteWord(bin,64,bin_address+80);packet::WriteWord(bin,68,bin_address+84);
  packet::WriteWord(bin,80,actor);
  if(!Write(base,bin_address,bin.data(),bin.size()))return;
  producer={};producer.active=true;producer.bin=bin_address;
  producer.key=Word(base,Word(base,manager+3072)+8);
  producer.value.target=camera.target;
  producer.value.generation=packet::ReadWord(*camera.view,88);
  struct End {~End(){producer.active=false;}} end;
  ctx.r3.u64=block;__imp__sub_82308DB0(ctx,base);
  const bool unchanged=Read(base,manager,after.data(),after.size())&&original==after;
  if(!unchanged){REXLOG_ERROR("coop radar failed: primary manager changed");return;}
  if(producer.captured&&producer.selected&&hud::Valid(producer.value,producer.value.generation)) {
    Publish(producer.value);
    publication.complete=true;
    if(camera.sequence<=3 || camera.sequence%120==0)
      REXLOG_INFO("coop radar producer: seq={} target={} generation={} primary_unchanged=true position={},{},{} terrain={} heading={}",
          camera.sequence,camera.target,producer.value.generation,
          std::bit_cast<float>(packet::ReadWord(producer.value.anchor,0)),
          std::bit_cast<float>(packet::ReadWord(producer.value.anchor,4)),
          std::bit_cast<float>(packet::ReadWord(producer.value.anchor,8)),
          std::bit_cast<float>(packet::ReadWord(producer.value.anchor,16)),
          std::bit_cast<float>(packet::ReadWord(producer.value.anchor,20)));
  }
}
} // namespace
void InstallCoopHud(rex::Runtime& runtime) {
  if(!Enabled())return;
  const auto block=runtime.memory()->SystemHeapAlloc(kManagerSize+kBinSize);
  scratch.store(block,std::memory_order_release);
  if(block)REXLOG_INFO("coop radar enabled: private producer={:08X}",block);
  else REXLOG_ERROR("coop radar failed: producer allocation");
}
bool CaptureCoopHud(PPCContext& ctx,uint8_t* base) {
  if(!producer.active)return false;
  if(ctx.r4.u32==producer.key && Word(base,ctx.r3.u32)==0x820215D0)
    producer.captured=Read(base,ctx.r3.u32+16,producer.value.anchor.data(),producer.value.anchor.size());
  return true;
}
} // namespace superman_returns::coop

using namespace superman_returns::coop;
REX_HOOK_RAW(sub_82308DB0) {
  const auto manager=ctx.r3.u32;
  __imp__sub_82308DB0(ctx,base);
  if(Enabled())Produce(ctx,base,manager);
}
REX_HOOK_RAW(sub_821CC2E0) {
  // Only this exact SUPERMAN-bin query in the private HUD producer is replaced.
  // Neither the real bin nor the global actor/camera selection is modified.
  if(producer.active && ctx.lr==0x82308EBC && ctx.r4.u32==0x820209D0) {
    ctx.r3.u64=producer.bin;producer.selected=true;return;
  }
  __imp__sub_821CC2E0(ctx,base);
}
REX_HOOK_RAW(sub_822F2668) {
  const auto address=ctx.r3.u32,key=ctx.r4.u32;
  __imp__sub_822F2668(ctx,base);
  if(!drawing_radar || key!=expected_key || !ctx.r3.u32)return;
  const auto view=CurrentSplitHud();
  if(!view)return;
  std::array<uint8_t,hud::kStateSize> data{};
  if(!drawing_secondary) {
    packet_verified=Read(base,address+16,consumed.anchor.data(),consumed.anchor.size());
    return;
  }
  const auto value=Sample();
  if(Read(base,address,data.data(),data.size())&&hud::Retarget(data,value,view->generation)) {
    std::array<uint8_t,hud::kStateSize> verified{};
    retargeted=Write(base,address+16,data.data()+16,hud::kAnchorSize)&&
        Read(base,address,verified.data(),verified.size())&&verified==data;
    packet_verified=retargeted;consumed=value;
    if(!retargeted)REXLOG_ERROR("coop radar failed: retarget readback");
  }
  // Refuse a P1 radar in the P2 pane when the matching binding isn't available.
  if(!retargeted)ctx.r3.u64=0;
}
REX_HOOK_RAW(sub_823149B8) {
  const auto view=CurrentSplitHud();
  if(!Enabled() || !view){__imp__sub_823149B8(ctx,base);return;}
  const auto object=ctx.r3.u32;
  drawing_radar=true;drawing_secondary=view->view==1;
  retargeted=packet_verified=aspect_applied=false;consumed={};expected_key=Word(base,object+8);
  struct EndDraw {~EndDraw(){drawing_radar=drawing_secondary=false;}} end_draw;
  if(!drawing_secondary) {
    __imp__sub_823149B8(ctx,base);LogDraw(*view,true);return;
  }
  static thread_local uint32_t owner=0,generation=0;
  static thread_local std::array<uint8_t,kCacheSize> cache{};
  std::array<uint8_t,kCacheSize> primary{},after{};
  if(!Read(base,object+kCacheOffset,primary.data(),primary.size()))return;
  if(owner!=object || generation!=view->generation) {
    cache=primary;owner=object;generation=view->generation;
  }
  struct RestoreCache {
    uint8_t* base;uint32_t object;const std::array<uint8_t,kCacheSize>& primary;
    ~RestoreCache(){Write(base,object+kCacheOffset,primary.data(),primary.size());}
  } restore{base,object,primary};
  if(!Write(base,object+kCacheOffset,cache.data(),cache.size())) {
    REXLOG_ERROR("coop radar failed: private cache activation");return;
  }
  __imp__sub_823149B8(ctx,base);
  const bool saved=Read(base,object+kCacheOffset,cache.data(),cache.size());
  const bool restored=Write(base,object+kCacheOffset,primary.data(),primary.size())&&
      Read(base,object+kCacheOffset,after.data(),after.size())&&after==primary;
  if(!saved || !restored)REXLOG_ERROR("coop radar failed: private cache restoration");
  LogDraw(*view,restored);
}
REX_HOOK_RAW(sub_8230EC00) {
  const bool previous=map_aspect;
  map_aspect=Enabled()&&CurrentSplitHud().has_value();
  struct Restore {bool previous;~Restore(){map_aspect=previous;}} restore{previous};
  __imp__sub_8230EC00(ctx,base);
}
REX_HOOK_RAW(sub_827759C0) {
  __imp__sub_827759C0(ctx,base);
  if(map_aspect && ctx.lr==0x8230EC70){ctx.f1.f64=8.0/9.0;aspect_applied=true;}
}
