#include "coop_idle_policy.h"
#include "coop_second_camera.h"
#include <array>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include "generated/default/superman_returns_init.h"
#include "title_benchmark.h"
#ifdef _WIN32
#include <windows.h>
#endif
REXCVAR_DEFINE_BOOL(sr_coop_idle,false,"Superman Returns",
                    "Development: P1 owns shared idle camera/HUD; either co-op port keeps it awake")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DECLARE(bool,sr_coop_input);
REXCVAR_DECLARE(bool,sr_coop_second_camera);
namespace {
using namespace superman_returns::coop;
thread_local bool combining=false;
thread_local uint64_t primary_updates=0,secondary_skips=0;
thread_local uint32_t generation=0;
bool Enabled() {
  static const bool enabled=REXCVAR_GET(sr_coop_idle)&&REXCVAR_GET(sr_coop_input)&&
      REXCVAR_GET(sr_coop_second_camera)&&!superman_returns::benchmark::Active();
  return enabled;
}
uint32_t Word(uint8_t* base,uint32_t address) {
  std::array<uint8_t,4> bytes{};
#ifdef _WIN32
  SIZE_T copied=0;
  if(address && ReadProcessMemory(GetCurrentProcess(),base+address,bytes.data(),4,&copied)&&copied==4)
    return packet::ReadWord(bytes,0);
#endif
  return 0;
}
IdleBinding Binding(uint8_t* base) {
  const auto snapshot=SampleSecondCamera();
  if(!snapshot.view || snapshot.primary_owner!=Word(base,0x8296D86C))return {};
  const auto input=Word(base,0x8296E8C8);
  if(!input || (Word(base,input+8)>>24)<2)return {};
  const auto slots=Word(base,input+4);
  if(!slots || Word(base,slots+4)!=snapshot.target)return {};
  return {Word(base,slots),snapshot.target,packet::ReadWord(*snapshot.view,88)};
}
void LogTransition(PPCContext& ctx,uint8_t* base,bool enter) {
  const auto camera=Word(base,0x8296D86C),hud=Word(base,0x82970174);
  REXLOG_INFO("coop idle transition: enter={} caller={:08X} mode={} radar_visible={} generation={} primary_updates={} secondary_skips={}",
      enter,uint32_t(ctx.lr),camera?Word(base,camera+4896):0,
      hud?(Word(base,hud+3120)>>24):0,Binding(base).generation,primary_updates,secondary_skips);
}
}
REX_HOOK_RAW(sub_82258240) {
  if(!Enabled() || ctx.r3.u32!=Word(base,0x8296E0E4)) {
    __imp__sub_82258240(ctx,base);return;
  }
  const auto binding=Binding(base);
  const auto actor=ctx.r4.u32?Word(base,ctx.r4.u32+88):0;
  const auto route=SelectIdleRoute(binding,actor,ctx.r5.u32);
  if(generation!=binding.generation) {
    generation=binding.generation;primary_updates=secondary_skips=0;
    REXLOG_INFO("coop idle binding: generation={} primary={} secondary={}",generation,binding.primary,binding.secondary);
  }
  if(route==IdleRoute::Secondary) {++secondary_skips;return;}
  if(route!=IdleRoute::Primary) {__imp__sub_82258240(ctx,base);return;}
  ++primary_updates;
  const bool previous=combining;combining=true;
  struct Restore {bool previous;~Restore(){combining=previous;}} restore{previous};
  __imp__sub_82258240(ctx,base);
}
REX_HOOK_RAW(sub_8229AAD8) {
  const auto input=ctx.r3.u32,port=ctx.r4.u32,caller=uint32_t(ctx.lr);
  __imp__sub_8229AAD8(ctx,base);
  if(!combining || caller!=0x822582F4 || port!=0 || input!=Word(base,0x8296E8C8))return;
  const PPCContext saved=ctx;
  const auto primary=ctx.f1.f64;
  ctx.r3.u64=input;ctx.r4.u64=1;
  __imp__sub_8229AAD8(ctx,base);
  const auto secondary=ctx.f1.f64;
  ctx=saved;ctx.f1.f64=CombinedIdle(primary,secondary);
  if(primary_updates<=3 || primary_updates%120==0)
    REXLOG_INFO("coop idle sample: seq={} generation={} primary_idle={} secondary_idle={} combined_idle={} primary_updates={} secondary_skips={} context_preserved=true",
        primary_updates,generation,primary,secondary,ctx.f1.f64,primary_updates,secondary_skips);
}
REX_HOOK_RAW(sub_8224A978) {
  const auto tracker=ctx.r3.u32;
  const auto before=Enabled()?Word(base,tracker+40)&0x00FF0000u:0;
  __imp__sub_8224A978(ctx,base);
  if(Enabled() && !before && (Word(base,tracker+40)&0x00FF0000u))LogTransition(ctx,base,true);
}
REX_HOOK_RAW(sub_8224AA68) {
  const auto tracker=ctx.r3.u32;
  const auto before=Enabled()?Word(base,tracker+40)&0x00FF0000u:0;
  __imp__sub_8224AA68(ctx,base);
  if(Enabled() && before && !(Word(base,tracker+40)&0x00FF0000u))LogTransition(ctx,base,false);
}
