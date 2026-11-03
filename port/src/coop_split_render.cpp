#include "coop_split_render.h"
#include "coop_camera_packet.h"
#include "coop_camera_preview.h"
#include "coop_split_frame.h"
#include "native_renderer/native_bridge.h"
#include <array>
#include <cstring>
#include <rex/cvar.h>
#include <rex/logging.h>
#include "generated/default/superman_returns_init.h"
#include "title_benchmark.h"
#ifdef _WIN32
#include <windows.h>
#endif
REXCVAR_DEFINE_BOOL(sr_coop_split_preview,false,"Superman Returns",
                    "Development: two native world draws composed side by side")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DECLARE(bool,sr_coop_input);
REXCVAR_DECLARE(bool,sr_coop_camera_transport);
REXCVAR_DECLARE(bool,sr_coop_second_camera);
namespace superman_returns::coop {
namespace {
bool Enabled() {
  static const bool enabled=REXCVAR_GET(sr_coop_split_preview) && REXCVAR_GET(sr_coop_input) &&
      REXCVAR_GET(sr_coop_camera_transport) && REXCVAR_GET(sr_coop_second_camera) && !benchmark::Active();
  return enabled;
}
bool Read(uint8_t* base,uint32_t address,void* out,size_t length) {
#ifdef _WIN32
  SIZE_T copied=0;
  return address && uint64_t(address)+length<=0x100000000ull &&
      ReadProcessMemory(GetCurrentProcess(),base+address,out,length,&copied) && copied==length;
#else
  return false;
#endif
}
uint32_t Word(uint8_t* base,uint32_t address) {
  std::array<uint8_t,4> bytes{};
  return Read(base,address,bytes.data(),4)?packet::ReadWord(bytes,0):0;
}
bool Write(uint8_t* base,uint32_t address,uint32_t value) {
#ifdef _WIN32
  std::array<uint8_t,4> bytes{};packet::WriteWord(bytes,0,value);
  SIZE_T copied=0;
  return address && WriteProcessMemory(GetCurrentProcess(),base+address,bytes.data(),4,&copied) && copied==4;
#else
  return false;
#endif
}
CameraSelection Selection(uint8_t* base,uint32_t graphics) {
  const auto list=Word(base,Word(base,graphics+96));
  return {list?Word(base,list):0,list?Word(base,list+4):0};
}
struct Restore {PPCContext& ctx;PPCContext saved;~Restore(){ctx=saved;}};
struct State {
  uint32_t graphics=0,address=0,generation=0;
  CameraSelection cameras;
  std::array<uint8_t,packet::kPacketSize> packet{};
  packet::DecodedView second;
  bool drawing=false;
  bool secondary=false;
  uint64_t frame=0;
  ReplayPhase phase;
};
thread_local State state;
thread_local uint64_t frames=0;
std::optional<ReplayPhase> ReadPhase(uint8_t* base) {
  ReplayPhase result;
  const auto owner=Word(base,0x829A9398);
  for(size_t i=0;i<3;++i) {
    const auto asset=Word(base,owner+80+uint32_t(i)*4);
    auto& effect=result.effects[i];effect.owner=asset?Word(base,asset+12):0;
    if(!effect.owner)return {};
    std::array<uint8_t,4> clock{},cadence{};
    if(!Read(base,effect.owner+80,clock.data(),4) || !Read(base,effect.owner+300,cadence.data(),4))return {};
    effect.clock=packet::ReadWord(clock,0);effect.cadence=packet::ReadWord(cadence,0);
  }
  result.frame_time=(uint64_t(Word(base,state.graphics+80))<<32)|Word(base,state.graphics+84);
  return result;
}
bool WritePhase(uint8_t* base,const ReplayPhase& phase) {
  bool ok=true;
  for(const auto& effect:phase.effects) {
    ok=Write(base,effect.owner+80,effect.clock)&&ok;
    ok=Write(base,effect.owner+300,effect.cadence)&&ok;
  }
  ok=Write(base,state.graphics+80,uint32_t(phase.frame_time>>32))&&ok;
  return Write(base,state.graphics+84,uint32_t(phase.frame_time))&&ok;
}
void Apply(PPCContext& ctx,uint8_t* base,uint32_t camera,uint32_t matrix,
           const std::array<float,4>& p) {
  auto scalar=[&](auto fn,float value){ctx.r3.u64=camera;ctx.f1.f64=value;fn(ctx,base);};
  scalar(sub_827858F0,p[2]);scalar(sub_82785960,8.f/9.f);
  scalar(sub_82785758,p[0]);scalar(sub_82785768,p[1]);
  ctx.r3.u64=camera;ctx.r4.u64=0;ctx.r5.u64=0;ctx.r6.u64=1280;ctx.r7.u64=720;
  sub_82785778(ctx,base);
  ctx.r3.u64=camera;ctx.r4.u64=matrix;sub_82786208(ctx,base);
  ctx.r3.u64=camera;sub_827863D0(ctx,base);
}
std::array<float,4> PrimaryProjection() {
  std::array<float,4> p{};
  for(size_t i=0;i<4;++i)p[i]=std::bit_cast<float>(packet::ReadWord(state.packet,80+i*4));
  return p;
}
bool Verify(uint8_t* base,size_t offset) {
  std::array<uint8_t,packet::kPacketSize> packet_after{};
  std::array<uint8_t,64> matrix{};
  return Read(base,state.address,packet_after.data(),packet_after.size()) && packet_after==state.packet &&
      Read(base,state.cameras.world+112,matrix.data(),matrix.size()) &&
      PreviewMatrixMatches(state.cameras,Selection(base,state.graphics),
          std::span(state.packet).subspan(offset,64),matrix);
}
}
bool PrepareSplitCamera(PPCContext& ctx,uint8_t* base,uint32_t graphics,uint32_t view,uint32_t caller) {
  if(!Enabled() || view || caller!=0x82776000)return false;
  state={};
  if(!native::CoopSplitSupported())return false;
  const auto consumer=Word(base,0x829A9398);
  if(!consumer)return false;
  std::array<uint8_t,packet::kPacketSize> data{};
  if(!Read(base,consumer+96,data.data(),data.size()))return false;
  const auto second=packet::DecodeOwnedSecondary(data);
  const auto cameras=Selection(base,graphics);
  if(!second || !cameras.world || !cameras.hud || cameras.world==cameras.hud)return false;
  state.graphics=graphics;state.address=consumer+96;state.generation=second->generation;
  state.cameras=cameras;state.packet=data;state.second=*second;state.frame=++frames;
  Restore restore{ctx,ctx};
  Apply(ctx,base,cameras.world,state.address+16,PrimaryProjection());
  if(!Verify(base,16)) {REXLOG_ERROR("coop split failed: primary preparation");state={};}
  return state.graphics!=0;
}
bool BeginSplitWorld(uint8_t* base,uint32_t graphics) {
  if(!Enabled() || state.graphics!=graphics || !state.generation || state.drawing)return false;
  if(!Verify(base,16)){REXLOG_ERROR("coop split failed: primary draw selection");return false;}
  const auto phase=ReadPhase(base);
  if(!phase){REXLOG_ERROR("coop split failed: unreadable effect phase");return false;}
  state.phase=*phase;
  state.drawing=native::QueueCoopSplitMarker(base,0,state.frame,state.generation);
  return state.drawing;
}
void EndSplitWorld(PPCContext& ctx,uint8_t* base,uint32_t graphics) {
  if(!state.drawing || graphics!=state.graphics)return;
  Restore restore{ctx,ctx};
  std::array<uint8_t,64> hud_before{},hud_after{};
  const bool hud_read=Read(base,state.cameras.hud+112,hud_before.data(),hud_before.size());
  native::QueueCoopSplitMarker(base,1,state.frame,state.generation);
  native::QueueCoopSplitMarker(base,2,state.frame,state.generation);
  Apply(ctx,base,state.cameras.world,state.address+112,state.second.projection);
  state.secondary=true;
  // Render-list preparation only: do not consume state packets or step the world
  // again. Zero delta avoids advancing render participants' time a second time.
  ctx.r3.u64=Word(base,0x829A93B8);ctx.f1.f64=0;
  if(ctx.r3.u32)sub_82783B48(ctx,base);
  const bool selected=Verify(base,112);
  bool phase_restored=false;
  if(selected) {
    phase_restored=ReplayAtOriginalPhase(state.phase,[&]{return ReadPhase(base);},
        [&](const ReplayPhase& phase){return WritePhase(base,phase);},[&]{
          ctx.r3.u64=graphics;ctx.f1.f64=0;ctx.r5.u64=1;
          __imp__sub_82776038(ctx,base);
        });
    if(!phase_restored)REXLOG_ERROR("coop split failed: effect phase restoration");
    native::QueueCoopSplitMarker(base,phase_restored?3:4,state.frame,state.generation);
  } else {
    REXLOG_ERROR("coop split failed: secondary draw selection");
    native::QueueCoopSplitMarker(base,4,state.frame,state.generation);
  }
  Apply(ctx,base,state.cameras.world,state.address+16,PrimaryProjection());
  const bool restored=Verify(base,16);
  const bool hud_unchanged=hud_read && Read(base,state.cameras.hud+112,hud_after.data(),hud_after.size()) &&
      hud_before==hud_after;
  if(!restored || !hud_unchanged)REXLOG_ERROR("coop split failed: primary/HUD restoration");
  if(state.frame<=3 || state.frame%120==0)
    REXLOG_INFO("coop split world: frame={} generation={} selected={} primary_restored={} hud_unchanged={} phase_restored={} passes=2 extra_prepare=1 extra_dt=0 primary={},{},{} secondary={},{},{}",
        state.frame,state.generation,selected,restored,hud_unchanged,phase_restored,
        std::bit_cast<float>(packet::ReadWord(state.packet,64)),
        std::bit_cast<float>(packet::ReadWord(state.packet,68)),
        std::bit_cast<float>(packet::ReadWord(state.packet,72)),
        state.second.matrix[12],state.second.matrix[13],state.second.matrix[14]);
  state.drawing=false;
}
std::optional<SplitHudContext> CurrentSplitHud() {
  if(!state.drawing)return {};
  return SplitHudContext{state.frame,state.generation,state.secondary?1u:0u};
}
}
