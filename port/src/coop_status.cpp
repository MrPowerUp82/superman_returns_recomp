#include "coop_status_packet.h"
#include "coop_status.h"
#include "coop_apt_probe.h"
#include "coop_second_camera.h"
#include "coop_split_render.h"
#include <mutex>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include "generated/default/superman_returns_init.h"
#include "title_benchmark.h"
#ifdef _WIN32
#include <windows.h>
#endif
REXCVAR_DEFINE_BOOL(sr_coop_status_probe,false,"Superman Returns",
                    "Development: hand off distinct native stamina to each split HUD pass; no APT override")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DECLARE(bool,sr_coop_input);
REXCVAR_DECLARE(bool,sr_coop_second_camera);
namespace {
using namespace superman_returns::coop;
std::mutex snapshot_mutex;
std::optional<status::Pair> snapshot;
bool Enabled() {
  static const bool enabled=REXCVAR_GET(sr_coop_status_probe)&&REXCVAR_GET(sr_coop_input)&&
      REXCVAR_GET(sr_coop_second_camera)&&!superman_returns::benchmark::Active();
  return enabled;
}
bool Read(uint8_t* base,uint32_t address,void* out,size_t count) {
#ifdef _WIN32
  SIZE_T copied=0;
  return address && uint64_t(address)+count<=0x100000000ull &&
      ReadProcessMemory(GetCurrentProcess(),base+address,out,count,&copied)&&copied==count;
#else
  return false;
#endif
}
uint32_t Word(uint8_t* base,uint32_t address) {
  std::array<uint8_t,4> bytes{};
  return Read(base,address,bytes.data(),4)?packet::ReadWord(bytes,0):0;
}
struct RestoreContext {PPCContext& ctx;PPCContext saved;~RestoreContext(){ctx=saved;}};
void Publish(std::optional<status::Pair> pair) {
  std::lock_guard lock(snapshot_mutex);snapshot=pair;
}
std::optional<status::Pair> Sample() {
  std::lock_guard lock(snapshot_mutex);return snapshot;
}
std::optional<status::Stamina> ReadStamina(PPCContext& ctx,uint8_t* base,uint32_t target) {
  if(!target || target==255)return {};
  ctx.r3.u64=target;sub_821C23E0(ctx,base);
  const auto actor=ctx.r3.u32;
  if(!actor || Word(base,actor+88)!=target)return {};
  const auto method=Word(base,Word(base,actor)+36);
  if(!method)return {};
  ctx.r3.u64=actor;ctx.r4.u64=0x41435452; // ACTR, same lookup as 822A9F80
  REX_CALL_INDIRECT_FUNC(method);
  const auto active=ctx.r3.u32,powers=active?Word(base,active+104):0;
  if(!powers)return {};
  ctx.r3.u64=powers;ctx.r4.u64=0x8201C788; // StaminaPoints
  sub_82226C50(ctx,base);
  const auto power=ctx.r3.u32,definition=power?Word(base,power+4):0;
  std::array<uint8_t,4> current{},maximum{};
  if(!definition || !Read(base,power+16,current.data(),4) || !Read(base,definition+16,maximum.data(),4))return {};
  return status::Normalize(target,std::bit_cast<int32_t>(packet::ReadWord(current,0)),
      std::bit_cast<int32_t>(packet::ReadWord(maximum,0)));
}
void Produce(PPCContext& ctx,uint8_t* base,uint32_t root,uint32_t caller) {
  static uint64_t sequence=0;
  static uint32_t logged_generation=0;
  ++sequence;
  std::optional<status::Pair> pair;
  RestoreContext restore{ctx,ctx};
  const auto camera=SampleSecondCamera();
  const auto input=Word(base,0x8296E8C8),slots=input?Word(base,input+4):0;
  const auto primary=slots?Word(base,slots):0;
  const auto secondary=slots && (Word(base,input+8)>>24)>1?Word(base,slots+4):0;
  const bool bound=camera.view && camera.target==secondary &&
      camera.primary_owner==Word(base,0x8296D86C) &&
      primary && primary!=255 && secondary && secondary!=255 && primary!=secondary;
  if(bound) {
    std::array<uint8_t,12> before{},after{};
    const bool read=Read(base,root+48,before.data(),before.size());
    const auto a=ReadStamina(ctx,base,primary),b=ReadStamina(ctx,base,secondary);
    const bool unchanged=read && Read(base,root+48,after.data(),after.size()) && before==after;
    if(!unchanged)REXLOG_ERROR("coop status failed: native HUD model readback");
    if(unchanged && a && b) {
      pair=status::MakePair(packet::ReadWord(*camera.view,88),sequence,*a,*b);
      if(pair && (sequence<=3 || sequence%120==0 || logged_generation!=pair->generation)) {
        const bool primary_matches=packet::ReadWord(before,0)==uint32_t(a->scaled) &&
            packet::ReadWord(before,8)==uint32_t(a->hud_maximum);
        REXLOG_INFO("coop status producer: seq={} generation={} primary={} primary_current={} primary_max={} primary_scaled={} secondary={} secondary_current={} secondary_max={} secondary_scaled={} model_unchanged=true primary_meter_matches={} city_cache={:08X} caller={:08X} apt_override=false",
            sequence,pair->generation,a->target,a->current,a->maximum,a->scaled,
            b->target,b->current,b->maximum,b->scaled,primary_matches,packet::ReadWord(before,4),caller);
      }
    }
  }
  const auto generation=pair?pair->generation:0;
  if(generation!=logged_generation) {
    REXLOG_INFO("coop status binding: seq={} generation={} primary={} secondary={}",
        sequence,generation,pair?pair->players[0].target:0,pair?pair->players[1].target:0);
    logged_generation=generation;
  }
  // Publish one complete replacement; do not expose a transient empty pair
  // while sampling. Invalid/removed bindings explicitly clear the snapshot.
  Publish(pair);
}
void ObserveHudEntry() {
  const auto view=CurrentSplitHud();
  if(!view || !(view->frame<=3 || view->frame%120==0))return;
  const auto pair=Sample();
  const auto value=status::Select(pair,view->generation,view->view);
  if(!value) {
    REXLOG_INFO("coop status draw: frame={} generation={} view={} ready=false apt_override=false",
        view->frame,view->generation,view->view);return;
  }
  REXLOG_INFO("coop status draw: frame={} generation={} view={} ready=true source_seq={} target={} current={} maximum={} scaled={} apt_override=false",
      view->frame,view->generation,view->view,pair->sequence,value->target,
      value->current,value->maximum,value->scaled);
}
}
void superman_returns::coop::UpdateCoopStatus(PPCContext& ctx,uint8_t* base) {
  if(!Enabled())return;
  const auto root=Word(base,0x8296EE7C);
  if(!root){Publish({});return;}
  Produce(ctx,base,root,uint32_t(ctx.lr));
}
REX_HOOK_RAW(sub_82775EF8) {
  BeginAptHudProbe();
  if(Enabled())ObserveHudEntry();
  __imp__sub_82775EF8(ctx,base);
  EndAptHudProbe();
}
