#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "instance_probe.h"
#include "../graphics/observed_instance_lifetimes.h"
#include "../graphics/observed_rigid_motion.h"
#include "../graphics/observed_vehicle_batch.h"
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

REXCVAR_DECLARE(bool,sr_native_object_motion);
REXCVAR_DECLARE(bool,sr_native_debug_observed_skin_motion);
REXCVAR_DECLARE(std::string,sr_native_api);
REXCVAR_DECLARE(std::string,sr_renderer);

namespace superman_returns::native {
namespace {
using graphics::ObservedInstanceToken;
std::atomic<bool> failed{false};
struct State {
  graphics::ObservedInstanceLifetimes lifetimes,cloth_topologies;
  graphics::ObservedVehicleBatchRegistry vehicle_batches;
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
struct VehicleEnqueueScope {
  graphics::VehicleBatchMember member;
  uint32_t descriptor=0,packet=0,owner=0,stack=0;
  bool active=false;
};
struct VehicleAppendScope {
  graphics::VehicleBatchMember member;
  graphics::VehicleBatchShape shape;
  uint32_t entry=0,group=0,before=0,stack=0,descriptor=0;
  bool active=false,known=false;
};
struct VehicleFlushScope {
  std::array<uint32_t,9> header{};
  std::array<graphics::VehicleBatchShape,2> shapes{};
  std::array<uint32_t,2> counts{};
  uint32_t entry=0;
  bool active=false;
};
struct VehicleUploadScope {
  std::array<uint32_t,504> words{};
  uint32_t source=0,rows=0;
  bool active=false;
};
thread_local VehicleEnqueueScope vehicle_enqueue;
thread_local VehicleAppendScope vehicle_append;
thread_local VehicleFlushScope vehicle_flush;
thread_local VehicleUploadScope vehicle_upload;
thread_local bool vehicle_flush_bracketed=false;
bool Word(uint8_t* base,uint32_t address,uint32_t& value) {
  if(!base || address<0x10000 || uint64_t(address)+4>(uint64_t{1}<<32)) return false;
  uint32_t raw=0;SIZE_T size=0;
  if(!ReadProcessMemory(GetCurrentProcess(),base+address,&raw,4,&size) || size!=4) return false;
  value=__builtin_bswap32(raw);return true;
}
bool Words(uint8_t* base,uint32_t address,std::span<uint32_t> values) {
  if(!base || address<0x10000 || uint64_t(address)+values.size_bytes()>(uint64_t{1}<<32)) return false;
  SIZE_T read=0;
  if(!ReadProcessMemory(GetCurrentProcess(),base+address,values.data(),values.size_bytes(),&read) || read!=values.size_bytes()) return false;
  for(auto& word:values)word=__builtin_bswap32(word);return true;
}
bool DiagnoseVehicleBatch() {
  static const bool requested=REXCVAR_GET(sr_native_debug_observed_skin_motion);return requested;
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
  static const bool requested=REXCVAR_GET(sr_native_instance_probe) ||
      (REXCVAR_GET(sr_native_object_motion) && REXCVAR_GET(sr_native_api)=="d3d12" &&
       REXCVAR_GET(sr_renderer)=="native");
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
    // Direct rigid-part submit only. Source: 8230F8E0/823103C8, documented in
    // docs/graphics-milestone-1-rigid-object-design-2026-11-05.md. The existing
    // lifetime/dispatch scope and 8261CD90 hook are reused unchanged.
    const bool rigid_single=current.render==0x8230F8E0 && current.vtable==0x820211C0 && caller==0x82310280;
    const bool rigid_base=current.render==0x8230F8E0 && current.vtable==0x820211C0 && (caller==0x823102B0 || rigid_single);
    const bool rigid_derived=current.render==0x823103C8 && current.vtable==0x82021930 && caller==0x82310D50;
    if(rigid_base || rigid_derived) {
      graphics::ObservedRigidPartRoute route;
      route.token=current.token;route.render=current.render;route.vtable=current.vtable;
      route.caller=caller;route.register_owner=ctx.r30.u32;
      route.slot=rigid_base?ctx.r24.u32:ctx.r23.u32;route.cursor=ctx.r29.u32;
      route.packet=current_packet;
      const uint32_t model_offset=rigid_base?132:136;uint32_t enabled=0;
      if(current.owner>UINT32_MAX-88 || ctx.r1.u32>UINT32_MAX-136 ||
         !Word(base,current.owner+88,route.owner_link) || route.owner_link>UINT32_MAX-12 ||
         !Word(base,route.owner_link+12,route.model_link) || route.model_link>UINT32_MAX-12 ||
         !Word(base,route.model_link+12,route.root_model) || route.root_model>UINT32_MAX-111 ||
         !Word(base,ctx.r1.u32+model_offset,route.saved_model) ||
         !Word(base,route.root_model+8,route.count) || !Word(base,route.root_model+12,route.parts) ||
         !Word(base,route.root_model+108,enabled) || !Word(base,route.cursor,route.selected_packet)) return;
      route.array_enabled=(enabled>>24)!=0;
      if(rigid_base) {
        if(!Word(base,ctx.r1.u32+136,route.register_count)) return;
      } else route.register_count=ctx.r14.u32;
      if(rigid_single) {
        graphics::ObservedSingleInstancePartRoute single;
        single.part=route;single.stack=ctx.r1.u32;single.counts_pointer=ctx.r5.u32;single.matrix_array=ctx.r6.u32;
        single.groups=ctx.r7.u32;single.rows=ctx.r8.u32;single.extra=ctx.r9.u32;single.enabled=ctx.r10.u32;
        uint32_t mode=0;
        if(uint64_t(current.owner)+192>UINT32_MAX || !Word(base,current.owner+68,mode) ||
           !Word(base,single.counts_pointer,single.count) || !Word(base,single.matrix_array,single.matrix_pointer)) return;
        single.mode=mode>>24;
        for(uint32_t i=0;i<16;++i)
          if(uint64_t(single.matrix_pointer)+i*4>UINT32_MAX ||
             !Word(base,current.owner+128+i*4,single.owner_world[i]) ||
             !Word(base,single.matrix_pointer+i*4,single.submitted_world[i])) return;
        graphics::CaptureObservedSingleInstancePartSelection(single,current_part);
      } else graphics::CaptureObservedRigidPartSelection(route,current_part);
      return;
    }
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
thread_local std::optional<InstanceProbePacketScope> single_instance_scope;
void BeginSingleInstanceProbe(uint8_t* base,PPCContext& ctx) noexcept {
  if(!InstanceProbeEnabled()) return;
  // Only the paired manifest call site enters this synchronous scope. A
  // reentrant use is unsupported and disables qualification conservatively.
  if(single_instance_scope) {failed.store(true);current_part={};return;}
  single_instance_scope.emplace(ctx,base);
}
void EndSingleInstanceProbe() noexcept {single_instance_scope.reset();}
void BeginVehicleEnqueueProbe(uint8_t* base,PPCContext& ctx) noexcept {
  if(!InstanceProbeEnabled()) return;
  if(vehicle_enqueue.active) {failed.store(true);return;}
  vehicle_enqueue={};
  try {
    static std::atomic<uint32_t> attempts{0};
    if(DiagnoseVehicleBatch() && attempts.fetch_add(1)<8)
      REXLOG_INFO("native vehicle enqueue source: owner={:08X} token={:08X}/{} render={:08X} vtable={:08X} dispatch={} stack={:08X} descriptor={:08X} slot={} cursor={:08X} source_verified=false",
          ctx.r30.u32,current.token.address,current.token.generation,current.render,current.vtable,current.dispatch,ctx.r1.u32,ctx.r6.u32,ctx.r23.u32,ctx.r29.u32);
    if(!current.dispatch || current.render!=0x823103C8 || current.vtable!=0x82021930 ||
       current.owner!=ctx.r30.u32 || current.token.address!=current.owner ||
       uint64_t(current.owner)+256>UINT32_MAX || uint64_t(ctx.r1.u32)+224>UINT32_MAX ||
       ctx.r6.u32!=ctx.r1.u32+160) return;
    std::array<uint32_t,5> descriptor{};uint32_t table=0,target=0,flags=0,mode=0;
    if(!Words(base,ctx.r6.u32,descriptor) || !Word(base,current.owner,table) || table!=current.vtable ||
       !Word(base,table+8,target) || target!=current.render || !Word(base,current.owner+16,flags) ||
       !Word(base,current.owner+68,mode) || (mode>>24)!=1 || descriptor[0]!=6 ||
       descriptor[1]!=current.owner+128 || descriptor[2]!=flags || descriptor[3]!=ctx.r1.u32+192 || descriptor[4]!=2) return;
    graphics::ObservedRigidPartRoute route;
    route.token=current.token;route.render=current.render;route.vtable=current.vtable;
    // Reuse only the pure asset-cursor validator of the derived direct route.
    // This scope's actual source is the separate manifest call82310C70.
    route.caller=0x82310D50;route.register_owner=ctx.r30.u32;route.slot=ctx.r23.u32;
    route.cursor=ctx.r29.u32;route.packet=ctx.r4.u32;route.register_count=ctx.r14.u32;
    uint32_t enabled=0;
    if(!Word(base,current.owner+88,route.owner_link) || uint64_t(route.owner_link)+12>UINT32_MAX ||
       !Word(base,route.owner_link+12,route.model_link) || uint64_t(route.model_link)+12>UINT32_MAX ||
       !Word(base,route.model_link+12,route.root_model) || uint64_t(route.root_model)+112>UINT32_MAX ||
       !Word(base,ctx.r1.u32+136,route.saved_model) || !Word(base,route.root_model+8,route.count) ||
       !Word(base,route.root_model+12,route.parts) || !Word(base,route.root_model+108,enabled) ||
       !Word(base,route.cursor,route.selected_packet)) return;
    route.array_enabled=(enabled>>24)!=0;graphics::ObservedPartSelection part;
    if(!graphics::CaptureObservedRigidPartSelection(route,part)) return;
    {auto& state=Probe();std::lock_guard lock(state.mutex);if(!state.lifetimes.IsAlive(current.token)) return;}
    vehicle_enqueue.member.token=current.token;
    vehicle_enqueue.member.part_words={current.token.address,current.render,current.vtable,
        part.model,part.sections,9,part.group,part.stream_offset,part.submesh};
    vehicle_enqueue.descriptor=ctx.r6.u32;vehicle_enqueue.packet=ctx.r4.u32;
    vehicle_enqueue.owner=current.owner;vehicle_enqueue.stack=ctx.r1.u32;vehicle_enqueue.active=true;
    static std::atomic<uint32_t> accepted{0};
    if(DiagnoseVehicleBatch() && accepted.fetch_add(1)<8)
      REXLOG_INFO("native vehicle enqueue qualified: owner={:08X} generation={} model={:08X} packet={:08X} part={} palette_rows=6 upload_identity_unverified=true",
          current.owner,current.token.generation,part.model,vehicle_enqueue.packet,part.group);
  } catch(...) {failed.store(true);vehicle_enqueue={};}
}
void EndVehicleEnqueueProbe() noexcept {vehicle_enqueue={};}
void BeginVehicleAppendProbe(uint8_t* base,PPCContext& ctx) noexcept {
  if(!InstanceProbeEnabled()) return;
  if(vehicle_append.active) {failed.store(true);return;}
  vehicle_append={};
  const uint32_t entry=ctx.r3.u32,group=ctx.r5.u32;
  try {
    auto& state=Probe();
    const bool candidate=vehicle_enqueue.active && ctx.r4.u32==vehicle_enqueue.descriptor;
    {std::lock_guard lock(state.mutex);if(!candidate && !state.vehicle_batches.Contains(entry)) return;}
    std::array<uint32_t,9> header{};
    if(group>1 || !Words(base,entry,header) || header[5]!=6 || header[6]<1 || header[6]>2 || group>=header[6]) {
      std::lock_guard lock(state.mutex);state.vehicle_batches.Clear(entry);return;
    }
    vehicle_append.entry=entry;vehicle_append.group=group;vehicle_append.before=header[3+group];
    vehicle_append.stack=ctx.r1.u32;vehicle_append.descriptor=ctx.r4.u32;
    vehicle_append.shape={header[group],header[8],header[5],header[6]};vehicle_append.active=true;
    if(!candidate || header[8]!=vehicle_enqueue.packet) return;
    std::array<uint32_t,5> descriptor{};uint32_t flags=0;
    if(!Words(base,ctx.r4.u32,descriptor) || descriptor[0]!=6 || descriptor[1]!=vehicle_enqueue.owner+128 ||
       descriptor[3]!=vehicle_enqueue.stack+192 || descriptor[4]!=2 ||
       !Word(base,vehicle_enqueue.owner+16,flags) || descriptor[2]!=flags) return;
    auto member=vehicle_enqueue.member;
    if(!Words(base,descriptor[1],std::span(member.palette).first(16)) ||
       !Words(base,descriptor[3],std::span(member.palette).subspan(16,8))) return;
    vehicle_append.member=member;vehicle_append.known=graphics::ValidVehicleBatchMember(member);
  } catch(...) {failed.store(true);vehicle_append={};}
}
void EndVehicleAppendProbe(uint8_t* base,PPCContext& ctx) noexcept {
  if(!vehicle_append.active) return;
  const auto pending=vehicle_append;vehicle_append={};
  try {
    auto& state=Probe();std::array<uint32_t,9> header{};std::array<uint32_t,24> copied{};
    const bool shape_ok=uint64_t(ctx.r1.u32)+128==pending.stack && ctx.r30.u32==pending.entry &&
        ctx.r31.u32==pending.descriptor && ctx.r27.u32==4*(pending.group+3) && Words(base,pending.entry,header) &&
        graphics::VehicleBatchShape{header[pending.group],header[8],header[5],header[6]}==pending.shape;
    if(!shape_ok) {std::lock_guard lock(state.mutex);state.vehicle_batches.Clear(pending.entry);return;}
    if(pending.known && header[3+pending.group]==pending.before+1) {
      const uint64_t destination=uint64_t(pending.shape.buffer)+uint64_t(pending.before)*96;
      if(destination>UINT32_MAX || !Words(base,uint32_t(destination),copied)) {
        std::lock_guard lock(state.mutex);state.vehicle_batches.Clear(pending.entry);return;
      }
    }
    std::lock_guard lock(state.mutex);
    const bool recorded=state.vehicle_batches.ObserveAppend(pending.entry,pending.group,pending.shape,pending.before,
        header[3+pending.group],pending.known?&pending.member:nullptr,copied);
    static std::atomic<uint32_t> reports{0};
    if(DiagnoseVehicleBatch() && reports.fetch_add(1)<8)
      REXLOG_INFO("native vehicle append observed: entry={:08X} group={} before={} after={} source_member={} copied_verified={} packet={:08X} upload_identity_unverified=true",
          pending.entry,pending.group,pending.before,header[3+pending.group],pending.known,recorded,pending.shape.packet);
  } catch(...) {failed.store(true);}
}
void BeginVehicleFlushProbe(uint8_t* base,PPCContext& ctx) noexcept {
  if(!InstanceProbeEnabled()) return;
  if(vehicle_flush_bracketed) {failed.store(true);return;}
  vehicle_flush_bracketed=true;
  vehicle_flush={};vehicle_upload={};
  try {
    auto& state=Probe();const uint32_t entry=ctx.r31.u32;
    {std::lock_guard lock(state.mutex);if(!state.vehicle_batches.Contains(entry)) return;}
    auto& header=vehicle_flush.header;
    if(uint64_t(entry)+36>UINT32_MAX || !Words(base,entry,header) || !header[2] || header[5]!=6 ||
       header[6]<1 || header[6]>2 || ctx.r3.u32!=header[8] || ctx.r4.u32!=header[2] ||
       ctx.r5.u32!=entry+12 || ctx.r6.u32!=entry || ctx.r7.u32!=header[6] || ctx.r8.u32!=6 ||
       ctx.r9.u32 || ctx.r10.u32!=1) return;
    vehicle_flush.entry=entry;
    for(uint32_t group=0;group<header[6];++group) {
      vehicle_flush.shapes[group]={header[group],header[8],6,header[6]};
      vehicle_flush.counts[group]=header[3+group];
      if(header[3+group]>100 || uint64_t(header[group])+uint64_t(header[3+group])*96>(uint64_t{1}<<32)) return;
    }
    vehicle_flush.active=true;
  } catch(...) {failed.store(true);vehicle_flush={};}
}
void EndVehicleFlushProbe(uint32_t entry) noexcept {
  vehicle_upload={};vehicle_flush={};vehicle_flush_bracketed=false;
  try {auto& state=Probe();std::lock_guard lock(state.mutex);state.vehicle_batches.Clear(entry);}
  catch(...) {failed.store(true);}
}
void ObserveVehiclePaletteProbe(uint8_t* base,uint32_t first,uint32_t source,uint32_t rows) noexcept {
  if(!InstanceProbeEnabled() || !vehicle_flush.active || first!=100) return;
  vehicle_upload={};
  if(!rows || rows%6 || rows>126) return;
  try {
    std::array<uint32_t,9> header{};
    if(!Words(base,vehicle_flush.entry,header) || header!=vehicle_flush.header ||
       !Words(base,source,std::span(vehicle_upload.words).first(rows*4))) return;
    vehicle_upload.source=source;vehicle_upload.rows=rows;vehicle_upload.active=true;
  } catch(...) {failed.store(true);vehicle_upload={};}
}
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
  if(!InstanceProbeEnabled()) return {};
  // An enqueue can flush older actors while its own render scope is active.
  // Never inherit that current actor as the identity of queued geometry.
  if(vehicle_flush_bracketed) {
    graphics::ObservedRenderInvocation invocation;
    if(vehicle_flush.active && vehicle_upload.active && current_draw_caller==0x8261B5A8) {
      const auto upload=vehicle_upload;vehicle_upload={};
      try {
        std::array<uint32_t,9> header{};
        if(Words(base,vehicle_flush.entry,header) && header==vehicle_flush.header) {
          auto& state=Probe();std::lock_guard lock(state.mutex);
          const bool captured=state.vehicle_batches.CaptureUpload(vehicle_flush.entry,vehicle_flush.shapes,vehicle_flush.counts,
              upload.source,upload.rows,std::span(upload.words).first(upload.rows*4),
              state.lifetimes,invocation.vehicle_batch);
          static std::atomic<uint32_t> reports{0};
          if(DiagnoseVehicleBatch() && reports.fetch_add(1)<8)
            REXLOG_INFO("native vehicle upload observed: entry={:08X} source={:08X} rows={} counts={},{} source_verified={} members={} worker_fetch_unverified=true",
                vehicle_flush.entry,upload.source,upload.rows,vehicle_flush.counts[0],vehicle_flush.counts[1],captured,invocation.vehicle_batch.members);
        }
      } catch(...) {failed.store(true);}
    }
    return invocation;
  }
  if(!current.dispatch) return {};
  try {
    auto& state=Probe();std::lock_guard lock(state.mutex);
    if(state.lifetimes.IsAlive(current.token)) {
      uint32_t palette=0;
      const bool packet_ok=current_packet && current_packet<=UINT32_MAX-16 && Word(base,current_packet+16,palette);
      graphics::ObservedRenderInvocation invocation{current.token,current.render,current.vtable,
          packet_ok?current_packet:0,packet_ok?palette:0,packet_ok?current_part:graphics::ObservedPartSelection{}};
      const bool rigid=(current.render==0x8230F8E0 && current.vtable==0x820211C0) ||
          (current.render==0x823103C8 && current.vtable==0x82021930);
      uint32_t table=0,target=0;
      if(rigid && current.owner==current.token.address && current.owner<=UINT32_MAX-191 &&
         Word(base,current.owner,table) && table==current.vtable &&
         Word(base,table+8,target) && target==current.render) {
        std::array<uint32_t,16> raw{};SIZE_T read=0;
        if(ReadProcessMemory(GetCurrentProcess(),base+current.owner+128,raw.data(),sizeof(raw),&read) &&
           read==sizeof(raw)) {
          for(size_t i=0;i<raw.size();++i) invocation.rigid_owner_world[i]=__builtin_bswap32(raw[i]);
          invocation.rigid_owner_world_observed=true;
        }
      }
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
    const bool vehicle_effect_candidate=vs==0x8E5232F5E5E6760Eull || vs==0x1023A9B52C1407C1ull;
    if(family.samples<8 && (state.samples<256 || (vehicle_effect_candidate && state.samples<272))) {
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
