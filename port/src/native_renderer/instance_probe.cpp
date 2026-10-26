#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "instance_probe.h"
#include "../graphics/observed_instance_lifetimes.h"
#include "game_profile.h"
#include "shader_registry.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <windows.h>
#include <atomic>
#include <map>
#include <mutex>

REXCVAR_DEFINE_BOOL(sr_native_instance_probe,false,"Superman Returns Native",
    "Observe renderable constructor/cleanup and draw scopes; does not qualify object motion")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace superman_returns::native {
namespace {
using graphics::ObservedInstanceToken;
std::atomic<bool> failed{false};
struct State {
  graphics::ObservedInstanceLifetimes lifetimes,cloth_topologies;
  std::mutex mutex;
  std::atomic<bool> armed{false};
  uint64_t created=0,destroyed=0,unreadable=0,entered=0,tracked_scopes=0,draws=0,scoped_draws=0,tracked_draws=0;
  struct Family {uint64_t draws=0,scoped=0,tracked=0;uint32_t samples=0;};
  std::map<uint64_t,Family> families;
  uint32_t samples=0;
};
State& Probe() {static State state;return state;}
struct ActiveScope {uint32_t owner=0,render=0,vtable=0;ObservedInstanceToken token;bool dispatch=false;};
thread_local ActiveScope current;
thread_local uint32_t current_packet=0,current_draw_caller=0;
thread_local graphics::ObservedPartSelection current_part;
bool Word(uint8_t* base,uint32_t address,uint32_t& value) {
  if(!base || address<0x10000 || uint64_t(address)+4>(uint64_t{1}<<32)) return false;
  uint32_t raw=0;SIZE_T size=0;
  if(!ReadProcessMemory(GetCurrentProcess(),base+address,&raw,4,&size) || size!=4) return false;
  value=__builtin_bswap32(raw);return true;
}
void Created(uint8_t* base,uint32_t owner,uint32_t expected_table=0x820211C0) noexcept {
  if(!InstanceProbeEnabled()) return;
  try {
    auto& state=Probe();uint32_t table=0;
    std::lock_guard lock(state.mutex);
    // Base constructor returns with its own vptr before derived construction.
    if(!Word(base,owner,table) || table!=expected_table) {++state.unreadable;return;}
    const auto token=state.lifetimes.Create(owner);
    state.cloth_topologies.Destroy(owner);
    if(token) {
      ++state.created;
      if(expected_table==0x82021DA8) state.cloth_topologies.Create(owner);
    }
    if(state.cloth_topologies.exhausted()) failed.store(true);
    if(state.lifetimes.exhausted()) failed.store(true);
  } catch(...) {failed.store(true);}
}
void Destroyed(uint32_t owner) noexcept {
  if(!InstanceProbeEnabled()) return;
  try {auto& state=Probe();std::lock_guard lock(state.mutex);
    if(state.lifetimes.Find(owner)) ++state.destroyed;
    state.lifetimes.Destroy(owner);
    state.cloth_topologies.Destroy(owner);
  } catch(...) {failed.store(true);}
}
}
bool InstanceProbeEnabled() noexcept {
  // Observation cannot be toggled mid-process after missing lifecycle events.
  static const bool requested=REXCVAR_GET(sr_native_instance_probe);
  return requested && !failed.load();
}
bool Half(uint8_t* base,uint32_t address,uint32_t& value) noexcept {
  if(!base || address<0x10000 || address==UINT32_MAX) return false;
  uint16_t raw=0;SIZE_T size=0;
  if(!ReadProcessMemory(GetCurrentProcess(),base+address,&raw,2,&size) || size!=2) return false;
  value=__builtin_bswap16(raw);return true;
}
class InstanceProbePacketScope {
  uint32_t previous_;
  graphics::ObservedPartSelection previous_part_;
 public:
  InstanceProbePacketScope(PPCContext& ctx,uint8_t* base) noexcept:
      previous_(current_packet),previous_part_(current_part) {
    current_packet=ctx.r3.u32;current_part={};
    // Source registers at the TWO audited call sites in82300328, before the
    // descriptor submitter's prologue. These are asset selections, not draw order.
    const uint32_t caller=uint32_t(ctx.lr);
    if(current.render==0x82306BB0 && current.vtable==0x820217E0 &&
       current.owner==ctx.r30.u32 && caller==0x82307298) {
      auto& part=current_part;uint32_t a=0,b=0,root=0,count=0,packet=0;
      part.model=ctx.r16.u32;
      if(current.owner>UINT32_MAX-256 || !Word(base,current.owner+88,a) || a>UINT32_MAX-12 ||
         !Word(base,a+12,b) || b>UINT32_MAX-12 || !Word(base,b+12,root) || root!=part.model ||
         part.model>UINT32_MAX-12 || !Word(base,part.model+8,count) || count!=ctx.r17.u32 ||
         ctx.r24.u32>=count || ctx.r24.u32>0xFFFF || !Word(base,part.model+12,part.sections) ||
         uint64_t(part.sections)+uint64_t(ctx.r24.u32)*4!=ctx.r26.u32 ||
         !Word(base,ctx.r26.u32,packet) || packet!=current_packet ||
         !Word(base,current.owner+256,part.actor_palette)) {part={};return;}
      part.origin=3;part.group=ctx.r24.u32;part.submesh=ctx.r24.u32;
      part.stream_offset=ctx.r24.u32*4;part.source_route_observed=true;return;
    }
    if(current.render!=0x82300328 || current.vtable!=0x82021D18 ||
       current.owner!=ctx.r25.u32 || (caller!=0x82300F2C && caller!=0x82301068)) return;
    auto& part=current_part;uint32_t descriptors=0,packet=0;
    if(ctx.r1.u32>UINT32_MAX-208 || !Word(base,ctx.r1.u32+208,part.model) ||
       part.model>UINT32_MAX-64 || !Word(base,part.model+64,part.sections) ||
       part.sections!=ctx.r24.u32 || !Word(base,part.model+12,descriptors) || descriptors!=ctx.r21.u32 ||
       !Half(base,ctx.r27.u32,part.submesh) || uint64_t(part.submesh)*4!=ctx.r29.u32 ||
       uint64_t(descriptors)+ctx.r29.u32>UINT32_MAX ||
       !Word(base,descriptors+ctx.r29.u32,packet) || packet!=current_packet ||
       ctx.r27.u32<part.sections || ctx.r27.u32-part.sections>=0x20000) {part={};return;}
    if(current.owner>UINT32_MAX-256 || !Word(base,current.owner+256,part.actor_palette)) {part={};return;}
    part.origin=caller==0x82300F2C?1:2;part.lod_slice=ctx.r22.u32;
    part.stream_offset=ctx.r27.u32-part.sections;
    if(part.origin==1) {
      if(ctx.r20.u32&1) {part={};return;}
      part.group=ctx.r20.u32/2;
    } else {
      uint32_t selections=0;
      if(ctx.r20.u32%12 || uint64_t(ctx.r20.u32/12)*2!=ctx.r19.u32 ||
         current.owner>UINT32_MAX-92 || !Word(base,current.owner+92,selections) ||
         uint64_t(selections)+ctx.r19.u32>UINT32_MAX ||
         !Half(base,selections+ctx.r19.u32,part.attachment) || part.attachment==0xFFFF) {part={};return;}
      part.group=ctx.r19.u32/2;
    }
    part.source_route_observed=true;
  }
  ~InstanceProbePacketScope() {current_packet=previous_;current_part=previous_part_;}
};
class InstanceProbeScope {
  ActiveScope previous_;
  uint32_t previous_packet_,previous_draw_caller_;
  graphics::ObservedPartSelection previous_part_;
 public:
  InstanceProbeScope(uint8_t* base,uint32_t owner,uint32_t render):previous_(current),
      previous_packet_(current_packet),previous_draw_caller_(current_draw_caller),previous_part_(current_part) {
    current={};current.owner=owner;current.render=render;current_packet=0;current_draw_caller=0;current_part={};
    try {
      auto& state=Probe();uint32_t target=0;
      current.dispatch=Word(base,owner,current.vtable) && current.vtable<=UINT32_MAX-8 &&
          Word(base,current.vtable+8,target) && target==render;
      std::lock_guard lock(state.mutex);++state.entered;
      if(current.dispatch) if(const auto token=state.lifetimes.Find(owner)) {
        current.token=*token;++state.tracked_scopes;
      }
    } catch(...) {failed.store(true);}
  }
  InstanceProbeScope(const InstanceProbeScope&)=delete;
  ~InstanceProbeScope() {current=previous_;current_packet=previous_packet_;current_draw_caller=previous_draw_caller_;current_part=previous_part_;}
};
bool ClothBuffer(uint8_t* base,uint32_t object,bool index,graphics::ObservedClothBuffer& out,bool* index32=nullptr) noexcept {
  uint32_t data=0,size=0,header=0;
  if(object>UINT32_MAX-0x1C || !Word(base,object+0x18,data) || !Word(base,object+0x1C,size) ||
     (index && !Word(base,object,header))) return false;
  if(!index) data&=~3u;
  const uint32_t physical=(data&0x1FFFFFFFu)+(data>=0xE0000000u?0x1000u:0u);
  const uint32_t bytes=index?(size&0xFFFFFF):((size>>2)&0xFFFFFF)*4;
  if(!physical || !bytes || uint64_t(physical)+bytes>0x20000000ull) return false;
  out={object,physical,bytes};if(index32)*index32=(header&0x80000000u)!=0;return true;
}
graphics::ObservedClothSelection CaptureCloth(uint8_t* base,const graphics::ObservedInstanceToken& topology) noexcept {
  graphics::ObservedClothSelection c;
  if(current.render!=0x82301510 || current.vtable!=0x82021DA8 ||
     current_draw_caller!=0x822F43C0 || current.owner>UINT32_MAX-28264 || !topology.generation) return c;
  c.caller=current_draw_caller;c.topology_generation=topology.generation;
  const uint32_t owner=current.owner;
  if(!Word(base,owner+22544,c.vertices) || !Word(base,owner+22548,c.quads) ||
     !Word(base,owner+28260,c.ring) || c.ring>2 || !c.vertices || c.vertices>0x1000000 ||
     !c.quads || uint64_t(c.quads)*6>4194303) return c;
  const uint32_t offsets[]{28196+c.ring*4,28208+c.ring*4,28232,28220+c.ring*4};
  for(uint32_t i=0;i<4;++i) {
    uint32_t object=0;
    if(!Word(base,owner+offsets[i],object) || !ClothBuffer(base,object,false,c.streams[i]) ||
       uint64_t(c.vertices)*(i==2?8:16)>c.streams[i].bytes) return c;
  }
  uint32_t ib=0;
  if(!Word(base,owner+28236,ib) || !ClothBuffer(base,ib,true,c.indices,&c.index32) ||
     c.index32 || uint64_t(c.quads)*8>c.indices.bytes) return c;
  c.caller=current_draw_caller;c.topology_generation=topology.generation;c.source_route_observed=true;return c;
}
graphics::ObservedRenderInvocation CaptureInstanceProbeInvocation(uint8_t* base) noexcept {
  if(!InstanceProbeEnabled() || !current.dispatch) return {};
  try {
    auto& state=Probe();std::lock_guard lock(state.mutex);
    if(state.lifetimes.IsAlive(current.token)) {
      uint32_t palette=0;
      const bool packet_ok=current_packet && current_packet<=UINT32_MAX-16 && Word(base,current_packet+16,palette);
      graphics::ObservedRenderInvocation invocation{current.token,current.render,current.vtable,
          packet_ok?current_packet:0,packet_ok?palette:0,packet_ok?current_part:graphics::ObservedPartSelection{}};
      if(const auto topology=state.cloth_topologies.Find(current.owner)) invocation.cloth=CaptureCloth(base,*topology);
      return invocation;
    }
  } catch(...) {failed.store(true);}
  return {};
}
void ArmInstanceProbe() noexcept {
  if(!InstanceProbeEnabled()) return;
  try {auto& state=Probe();if(state.armed.exchange(true)) return;
    std::lock_guard lock(state.mutex);
    REXLOG_INFO("native instance probe armed: constructed={} destroyed={} unreadable={} render_scopes={} tracked_scopes={} (lifecycle observations; association unverified)",
        state.created,state.destroyed,state.unreadable,state.entered,state.tracked_scopes);
  } catch(...) {failed.store(true);}
}
void NoteInstanceProbeDraw(uint8_t* base,uint32_t device,const char* kind,uint32_t count,uint32_t caller,uint32_t stack) noexcept {
  if(!InstanceProbeEnabled()) return;
  current_draw_caller=caller;
  try {auto& state=Probe();if(!state.armed.load()) return;
    uint64_t vs=0,ps=0;
    for(const uint32_t offset:{profile::kDevice.shader_a,profile::kDevice.shader_b}) {
      uint32_t object=0;
      if(uint64_t(device)+offset<=UINT32_MAX && Word(base,device+offset,object))
        if(const auto shader=CaptureGuestShader(object)) (shader->vertex?vs:ps)=shader->hash;
    }
    std::lock_guard lock(state.mutex);++state.draws;
    const bool scoped=current.owner!=0;
    const bool tracked=scoped && current.dispatch && state.lifetimes.IsAlive(current.token);
    state.scoped_draws+=scoped;state.tracked_draws+=tracked;
    if(!state.families.contains(vs) && state.families.size()>=128) return;
    auto& family=state.families[vs];++family.draws;family.scoped+=scoped;family.tracked+=tracked;
    if(family.samples<8 && state.samples<256) {
      ++family.samples;++state.samples;
      // Bounded PPC backchain diagnostics only; never an identity source.
      std::string trace=fmt::format("{:08X}",caller);
      uint32_t frame=stack;
      for(uint32_t depth=0;depth<8;++depth) {
        uint32_t parent=0,return_pc=0;
        if(!Word(base,frame,parent) || parent<=frame || parent-frame>0x100000 ||
           (parent&15) || !Word(base,parent-8,return_pc) ||
           return_pc<0x82000000 || return_pc>=0x829E0000 || (return_pc&3)) break;
        trace+=fmt::format(",{:08X}",return_pc);frame=parent;
      }
      REXLOG_INFO("native instance draw callers: vs={:016X} ps={:016X} stack={:08X} lr_chain={}",vs,ps,stack,trace);
      REXLOG_INFO("native instance draw association: kind={} count={} vs={:016X} ps={:016X} owner={:08X} generation={} render={:08X} vtable={:08X} dispatch_match={} ctor_observed={} motion_identity_verified=false",
          kind,count,vs,ps,current.owner,current.token.generation,current.render,current.vtable,current.dispatch,tracked);
    }
  } catch(...) {failed.store(true);}
}
void ReportInstanceProbe() noexcept {
  if(!InstanceProbeEnabled()) return;
  try {auto& state=Probe();std::lock_guard lock(state.mutex);
    REXLOG_INFO("native instance probe summary: constructed={} destroyed={} scopes={} tracked_scopes={} draws={} scoped_draws={} tracked_draws={} (frontend diagnostics only; no history attestation)",
        state.created,state.destroyed,state.entered,state.tracked_scopes,state.draws,state.scoped_draws,state.tracked_draws);
    for(const auto& [vs,f]:state.families)
      REXLOG_INFO("native instance probe family: vs={:016X} draws={} scoped={} tracked={}",vs,f.draws,f.scoped,f.tracked);
  } catch(...) {failed.store(true);}
}
graphics::ObservedInstanceToken BeginClothRebuild(uint8_t* base,uint32_t owner) noexcept {
  if(!InstanceProbeEnabled()) return {};
  try {auto& state=Probe();std::lock_guard lock(state.mutex);uint32_t table=0;
    if(!Word(base,owner,table) || table!=0x82021DA8)return {};
    if(const auto token=state.lifetimes.Find(owner)) {
      state.cloth_topologies.Destroy(owner);return *token;
    }
  } catch(...) {failed.store(true);}
  return {};
}
void EndClothRebuild(uint8_t* base,graphics::ObservedInstanceToken token) noexcept {
  if(!InstanceProbeEnabled() || !token.generation)return;
  try {auto& state=Probe();std::lock_guard lock(state.mutex);uint32_t table=0;
    if(state.lifetimes.IsAlive(token) && Word(base,token.address,table) && table==0x82021DA8) {
      state.cloth_topologies.Create(token.address);
      if(state.cloth_topologies.exhausted())failed.store(true);
    }
  } catch(...) {failed.store(true);}
}
void InvokeInstanceProbeRender(uint32_t address,PPCFunc* original,PPCContext& ctx,uint8_t* base) {
  if(!InstanceProbeEnabled()) {original(ctx,base);return;}
  const InstanceProbeScope scope(base,ctx.r3.u32,address);
  original(ctx,base);
}
}

// Source-audited base construction/cleanup, covering the derived chain once.
// See docs/instance-owner-lifetime-evidence-2026-10-26.md. No generated edits.
REX_EXTERN(__imp__sub_822E8BD0);
extern "C" REX_FUNC(sub_822E8BD0) {
  const uint32_t owner=ctx.r3.u32;
  __imp__sub_822E8BD0(ctx,base);
  superman_returns::native::Created(base,owner);
}
REX_EXTERN(__imp__sub_822F54D8);
extern "C" REX_FUNC(sub_822F54D8) {
  superman_returns::native::Destroyed(ctx.r3.u32);
  __imp__sub_822F54D8(ctx,base);
}

// Actual C209 caller trace enters this descriptor submitter at82301068.
REX_EXTERN(__imp__sub_8261CD90);
extern "C" REX_FUNC(sub_8261CD90) {
  if(!superman_returns::native::InstanceProbeEnabled()) {__imp__sub_8261CD90(ctx,base);return;}
  const superman_returns::native::InstanceProbePacketScope scope(ctx,base);
  __imp__sub_8261CD90(ctx,base);
}

REX_EXTERN(__imp__sub_82306BB0);
extern "C" REX_FUNC(sub_82306BB0) {
  superman_returns::native::InvokeInstanceProbeRender(0x82306BB0,__imp__sub_82306BB0,ctx,base);
}
REX_EXTERN(__imp__sub_82301510);
extern "C" REX_FUNC(sub_82301510) {
  superman_returns::native::InvokeInstanceProbeRender(0x82301510,__imp__sub_82301510,ctx,base);
}
REX_EXTERN(__imp__sub_823011E0);
extern "C" REX_FUNC(sub_823011E0) {
  const uint32_t owner=ctx.r3.u32;
  __imp__sub_823011E0(ctx,base);
  superman_returns::native::Created(base,owner,0x82021DA8);
}
REX_EXTERN(__imp__sub_82301368);
extern "C" REX_FUNC(sub_82301368) {
  superman_returns::native::Destroyed(ctx.r3.u32);
  __imp__sub_82301368(ctx,base);
}

REX_EXTERN(__imp__sub_822F3638);
extern "C" REX_FUNC(sub_822F3638) {
  const auto token=superman_returns::native::BeginClothRebuild(base,ctx.r3.u32);
  __imp__sub_822F3638(ctx,base);
  superman_returns::native::EndClothRebuild(base,token);
}
