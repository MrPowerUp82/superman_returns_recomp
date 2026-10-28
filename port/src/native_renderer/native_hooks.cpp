// Ported from crazyriddler/rexglue-native-kit @136bc6c4,
// reference/conan/port/src/native/d3d_capture.cpp and native_hooks.cpp
// (Conan native renderer). The kit ships no license file at that revision.
//
// Hooks on the game's statically linked XDK Direct3D functions. Normally these
// call the recompiled original first (as in the kit: the original flushes
// the draw's dirty state into the command segment the PM4 mirror parses),
// then forwards to the renderer when it is active. With the renderer off the
// hooks are pass-throughs plus, with sr_native_capture, a call record.
// SR_NATIVE_XDK_DRAW_SUBMISSION=1 replaces admitted regular/indexed draws and
// their generic dirty-state emitters natively. Unported special states retain
// the original path; this is an experimental step toward complete XDK bypass.
//
// Changes from the kit:
//  * Addresses come from game_profile.h by role. A hook is compiled only when
//    its role is confirmed and not absent (SR_HOOK_ENABLED), so an unconfirmed
//    candidate can never redirect a game function.
//  * The device is taken from each call's r3 (NoteGuestDevice) instead of
//    Conan's global 0x82C81A64.
//  * Conan's 27 render-pass hooks, its Present hook and the EXP-047 name
//    lookup cache are not ported (docs/native-port-plan.md section 2.2); the
//    kit's JSON draw catalog (d3d_capture_out) is replaced by the simpler
//    capture record of native_bridge.cpp.
//  * D3DDevice_Swap at sub_82112050 is already hooked by frame_stats.cpp,
//    which forwards to native_bridge.cpp (OnFrameStatsSwap).

#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <map>
#include <mutex>

#include <rex/ppc.h>
#include <rex/logging.h>

#include "game_profile.h"
#include "hang_watchdog.h"
#include "native_bridge.h"
#include "native_graphics_system.h"
#include "native_renderer.h"
#include "instance_probe.h"
#include "sdk_compat.h"
#include "checked_guest_memory.h"
#include "idle_yield.h"
#include "xdk_constant_copy.h"
#include "xdk_draw_submission.h"
#include "guest_cpu_profile.h"
#include "resource_unlock_audit.h"

namespace {

namespace native = superman_returns::native;

bool ResourceUnlockAuditing() {
  static const bool enabled = [] {
    const char* value = std::getenv("SR_NATIVE_RESOURCE_UNLOCK_AUDIT");
    return value && *value && *value != '0';
  }();
  return enabled;
}
struct UnlockAuditCounters {
  uint64_t calls=0, candidates=0, unreadable=0, invalid_fetch=0, other=0, dropped_callers=0;
  uint64_t details=0;
  uint64_t validation_checks=0, matched_checks=0, matched_fetches=0, dropped_fetches=0;
  uint64_t matched_range_checks=0, matched_range_sets=0, dropped_range_sets=0;
  std::map<uint32_t, std::array<uint64_t,2>> callers;
  std::map<std::array<uint32_t,6>, bool> fetches;
  std::map<native::UnlockRangeKey,bool> range_sets;
  std::map<std::array<uint32_t,4>,uint64_t> producers;
  uint64_t producer_unreadable=0, producer_dropped=0;
};
UnlockAuditCounters unlock_audit;
std::mutex unlock_audit_mutex;
struct HandoffObservation {
  uint64_t sequence=0;
  uint32_t object=0, descriptor_address=0, caller=0, target=0, next_target=0;
  native::FrameHandoffDescriptor descriptor{};
  bool readable=false;
};
struct PendingHandoff {
  HandoffObservation observation{};
  uint32_t result=0, mask=0;
  bool active=false;
};
thread_local PendingHandoff pending_handoff;
thread_local uint32_t handoff_depth=0;
thread_local bool handoff_nested=false;
struct HandoffCounters {
  uint64_t calls=0, returns=0, nonnegative=0, negative=0, unreadable=0, unsupported=0;
  uint64_t nested=0, abandoned=0, plane_matches=0, plane_misses=0, unattributed=0, groups=0;
} handoff_audit;

std::span<const uint8_t> ReadAuditGuest(native::CheckedGuestReads& reads,
    uint8_t* base, uint32_t address, uint32_t size) {
  if(!address || uint64_t(address)+size>(uint64_t{1}<<32)) return {};
  return reads.Read(base+address,size);
}
uint32_t AuditWord(std::span<const uint8_t> bytes, size_t offset) {
  return (uint32_t(bytes[offset])<<24)|(uint32_t(bytes[offset+1])<<16)|
         (uint32_t(bytes[offset+2])<<8)|bytes[offset+3];
}
HandoffObservation BeginHandoff(uint8_t* base, uint32_t object,
                                uint32_t descriptor, uint32_t caller) {
  HandoffObservation observation;
  observation.object=object; observation.descriptor_address=descriptor; observation.caller=caller;
  native::CheckedGuestReads reads;
  const auto header=ReadAuditGuest(reads,base,object,4);
  const auto table=header.size()==4 ? ReadAuditGuest(reads,base,AuditWord(header,0),80)
                                   : std::span<const uint8_t>{};
  const auto data=ReadAuditGuest(reads,base,descriptor,48);
  observation.readable=table.size()==80 && native::ReadFrameHandoffDescriptor(data,observation.descriptor);
  if(observation.readable) {
    observation.target=AuditWord(table,72); observation.next_target=AuditWord(table,76);
  }
  std::lock_guard lock(unlock_audit_mutex);
  observation.sequence=++handoff_audit.calls;
  if(pending_handoff.active) ++handoff_audit.abandoned;
  pending_handoff={};
  if(observation.sequence<=12)
    REXLOG_INFO("native frame handoff entry: seq={} object={:08X} descriptor={:08X} caller={:08X} target={:08X} next_target={:08X} readable={} kind={} destinations={:08X},{:08X},{:08X} strides={},{},{}",
        observation.sequence,object,descriptor,caller,observation.target,observation.next_target,
        observation.readable,observation.descriptor.kind,observation.descriptor.destinations[0],
        observation.descriptor.destinations[1],observation.descriptor.destinations[2],
        observation.descriptor.strides[0],observation.descriptor.strides[1],observation.descriptor.strides[2]);
  return observation;
}
void EndHandoff(const HandoffObservation& observation, uint32_t result, bool nested) {
  std::lock_guard lock(unlock_audit_mutex);
  auto& s=handoff_audit;
  ++s.returns;
  if(int32_t(result)<0) ++s.negative; else ++s.nonnegative;
  if(nested) ++s.nested;
  else if(!observation.readable) ++s.unreadable;
  else if(observation.caller!=0x8235cb5cu || observation.descriptor.kind!=1 ||
          observation.target!=0x8247e9d0u || observation.next_target!=0x82480d80u) ++s.unsupported;
  else pending_handoff={observation,result,0,true};
  if(nested) pending_handoff={};
  if(observation.sequence<=12)
    REXLOG_INFO("native frame handoff return: seq={} result={:08X} nested={} pending={}",
        observation.sequence,result,nested,pending_handoff.active);
}
void ObserveHandoffUnlock(uint32_t caller, uint32_t base_page, uint32_t object) {
  // Call only while holding unlock_audit_mutex. Failure-path unlocks have no
  // completed handoff and must not consume a previous call's observation.
  uint32_t plane=3;
  if(caller==0x8235cbd4u) plane=0;
  else if(caller==0x8235cbe0u) plane=1;
  else if(caller==0x8235cbecu) plane=2;
  else if(caller==0x8235cb7cu || caller==0x8235cb88u || caller==0x8235cb94u) {
    if(pending_handoff.active) ++handoff_audit.abandoned;
    pending_handoff={};
    return;
  } else return;
  if(!pending_handoff.active) { ++handoff_audit.unattributed; return; }
  auto& p=pending_handoff;
  if(!native::ConsumeHandoffPlane(p.observation.descriptor,plane,base_page,p.mask)) {
    ++handoff_audit.plane_misses;
    return;
  }
  ++handoff_audit.plane_matches;
  if(p.observation.sequence<=12)
    REXLOG_INFO("native frame handoff unlock: seq={} plane={} object={:08X} base={:08X} result={:08X} mask={}",
        p.observation.sequence,plane,object,base_page,p.result,p.mask);
  if(p.mask==7) { ++handoff_audit.groups; p.active=false; }
}
void ObserveResourceUnlock(uint8_t* base, uint32_t object, uint32_t base_page,
                           uint32_t mip_page, uint32_t caller) {
  std::lock_guard lock(unlock_audit_mutex);
  auto& s=unlock_audit;
  ++s.calls;
  ObserveHandoffUnlock(caller,base_page,object);
  native::CheckedGuestReads reads;
  // Only the three successful return sites of the statically inspected
  // planar update. Read the current producer after its call has returned;
  // this is a sampled vtable target, not proof of the call's actual target.
  if (caller==0x8235cbd4u || caller==0x8235cbe0u || caller==0x8235cbecu) {
    auto read_word=[&](uint32_t address, uint32_t& value) {
      if (!address || uint64_t(address)+4>(uint64_t{1}<<32)) return false;
      const auto data=reads.Read(base+address,4);
      if (data.size()!=4) return false;
      value=(uint32_t(data[0])<<24)|(uint32_t(data[1])<<16)|(uint32_t(data[2])<<8)|data[3];
      return true;
    };
    uint32_t producer=0, vtable=0, target=0, next_target=0;
    if (!read_word(0x829761d4u,producer) || !read_word(producer,vtable) ||
        uint64_t(vtable)+76+4>(uint64_t{1}<<32) || !read_word(vtable+72,target) ||
        !read_word(vtable+76,next_target)) {
      ++s.producer_unreadable;
    } else {
      const std::array<uint32_t,4> key{producer,vtable,target,next_target};
      auto found=s.producers.find(key);
      if (found==s.producers.end() && s.producers.size()<16) {
        found=s.producers.emplace(key,0).first;
        REXLOG_INFO("native texture producer sampled: object={:08X} vtable={:08X} target={:08X} next_target={:08X} caller={:08X} plane={:08X}",producer,vtable,target,next_target,caller,object);
      }
      if (found!=s.producers.end()) ++found->second;
      else ++s.producer_dropped;
    }
  }
  auto bytes = object && uint64_t(object)+52 <= (uint64_t{1}<<32)
      ? reads.Read(base+object,52) : std::span<const uint8_t>{};
  std::array<uint32_t,6> fetch{};
  bool candidate=false, fresh_fetch=false;
  std::vector<superman_returns::graphics::guest::TextureRange> ranges;
  std::string error;
  if (bytes.empty()) ++s.unreadable;
  else if (!native::ReadUnlockTextureFetch(bytes,base_page,mip_page,fetch)) ++s.other;
  else if (!native::NormalizeUnlockTextureFetch(fetch) ||
           !superman_returns::graphics::guest::DescribeTextureRanges(fetch,ranges,error)) ++s.invalid_fetch;
  else {
    candidate=true; ++s.candidates;
    if (s.fetches.contains(fetch) || s.fetches.size()<1024) fresh_fetch=s.fetches.try_emplace(fetch,false).second;
    else ++s.dropped_fetches;
    native::UnlockRangeKey key;
    if (native::MakeUnlockRangeKey(ranges,key)) {
      if (s.range_sets.contains(key) || s.range_sets.size()<1024) s.range_sets.try_emplace(key,false);
      else ++s.dropped_range_sets;
    } else ++s.dropped_range_sets;
  }
  auto it=s.callers.find(caller);
  if (it==s.callers.end() && s.callers.size()<64) it=s.callers.emplace(caller,std::array<uint64_t,2>{}).first;
  if (it!=s.callers.end()) { ++it->second[0]; it->second[1]+=candidate; }
  else ++s.dropped_callers;
  if (candidate && fresh_fetch && s.details<256) {
    ++s.details;
    uint64_t size=0;
    for (const auto& range:ranges) size+=range.length;
    REXLOG_INFO("native resource unlock candidate: object={:08X} caller={:08X} base={:08X} mip={:08X} bytes={} ranges={} fetch={:08X},{:08X},{:08X},{:08X},{:08X},{:08X}",
        object,caller,base_page,mip_page,size,ranges.size(),fetch[0],fetch[1],fetch[2],fetch[3],fetch[4],fetch[5]);
  }
}

inline uint32_t GuestLoad32(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

// Common entry of the hooks that receive the device in r3.
inline void OnDeviceCall(const char* role, uint32_t dev, uint32_t arg) {
  native::NoteGuestDevice(dev);
  native::NoteHookCall(role, dev, arg);
}

// DrawVerticesUP calls BeginVertices/EndVertices itself.
thread_local bool t_in_draw_up = false;

// Inline-vertex draw opened by BeginVertices, consumed by EndVertices.
struct PendingInlineDraw {
  bool active = false;
  uint32_t prim = 0, count = 0, stride = 0, data = 0;
};
PendingInlineDraw g_pending_inline;

// BlockOnFence's arguments for the poll hook below.
thread_local uint32_t t_fence_dev = 0, t_fence_value = 0;

}  // namespace

void superman_returns::native::FlushResourceUnlockAudit(uint64_t frame) {
  if (!ResourceUnlockAuditing()) return;
  std::lock_guard lock(unlock_audit_mutex);
  const auto& s=unlock_audit;
  const auto& h=handoff_audit;
  REXLOG_INFO("native frame handoff audit: frame={} calls={} returns={} nonnegative={} negative={} unreadable={} unsupported={} nested={} abandoned={} plane_matches={} plane_misses={} unattributed={} groups={} cumulative=true",
      frame,h.calls,h.returns,h.nonnegative,h.negative,h.unreadable,h.unsupported,h.nested,
      h.abandoned,h.plane_matches,h.plane_misses,h.unattributed,h.groups);
  for (const auto& [key,count]:s.producers)
    REXLOG_INFO("native texture producer audit: frame={} object={:08X} vtable={:08X} target={:08X} next_target={:08X} samples={} unreadable={} dropped={} cumulative=true",frame,key[0],key[1],key[2],key[3],count,s.producer_unreadable,s.producer_dropped);
  REXLOG_INFO("native resource unlock audit: frame={} calls={} candidates={} unreadable={} invalid_fetch={} other={} callers={} dropped_callers={} details={} fetches={} dropped_fetches={} validation_checks={} matched_checks={} matched_fetches={} range_sets={} dropped_range_sets={} matched_range_checks={} matched_range_sets={} cumulative=true",
      frame,s.calls,s.candidates,s.unreadable,s.invalid_fetch,s.other,s.callers.size(),s.dropped_callers,s.details,
      s.fetches.size(),s.dropped_fetches,s.validation_checks,s.matched_checks,s.matched_fetches,
      s.range_sets.size(),s.dropped_range_sets,s.matched_range_checks,s.matched_range_sets);
  for (const auto& [caller,counts]:s.callers)
    REXLOG_INFO("native resource unlock caller: frame={} caller={:08X} calls={} candidates={} cumulative=true",
        frame,caller,counts[0],counts[1]);
}

void superman_returns::native::ObserveTextureValidationForUnlockAudit(const uint32_t* words,
    std::span<const graphics::guest::TextureRange> ranges) {
  if (!ResourceUnlockAuditing()) return;
  std::array<uint32_t,6> fetch;
  std::copy_n(words,6,fetch.begin());
  std::lock_guard lock(unlock_audit_mutex);
  auto& s=unlock_audit;
  ++s.validation_checks;
  // Physical coverage is a separate observation from exact fetch identity.
  // Equal ranges may still belong to reused allocations; neither match is an
  // authoritative invalidation/version signal.
  UnlockRangeKey key;
  if (MakeUnlockRangeKey(ranges,key)) {
    const auto found=s.range_sets.find(key);
    if (found!=s.range_sets.end()) {
      ++s.matched_range_checks;
      if (!found->second) {
        found->second=true;
        ++s.matched_range_sets;
        if (s.matched_range_sets<=32)
          REXLOG_INFO("native resource unlock validated ranges: base={:08X} mip={:08X} ranges={} fetch={:08X},{:08X},{:08X},{:08X},{:08X},{:08X}",
              fetch[1]&0xfffff000u,fetch[5]&0xfffff000u,ranges.size(),fetch[0],fetch[1],fetch[2],fetch[3],fetch[4],fetch[5]);
      }
    }
  }
  const auto it=s.fetches.find(fetch);
  if (it==s.fetches.end()) return;
  ++s.matched_checks;
  if (!it->second) {
    it->second=true;
    ++s.matched_fetches;
    if (s.matched_fetches<=32)
      REXLOG_INFO("native resource unlock validated fetch: base={:08X} mip={:08X} fetch={:08X},{:08X},{:08X},{:08X},{:08X},{:08X}",
          fetch[1]&0xfffff000u,fetch[5]&0xfffff000u,fetch[0],fetch[1],fetch[2],fetch[3],fetch[4],fetch[5]);
  }
}

#define SR_DEFINE_HOOK_(addr) \
  REX_EXTERN(__imp__sub_##addr); \
  extern "C" REX_FUNC(sub_##addr)
#define SR_DEFINE_HOOK(addr) SR_DEFINE_HOOK_(addr)
#define SR_ORIGINAL_(addr) __imp__sub_##addr
#define SR_ORIGINAL(addr) SR_ORIGINAL_(addr)
#define SR_ADDRESS_(addr) 0x##addr
#define SR_ADDRESS(addr) SR_ADDRESS_(addr)

namespace {
bool NativeBulkConstantsEnabled() {
  static const bool enabled=[] {
    const char* value=std::getenv("SR_NATIVE_XDK_BULK_COPY");
    return !value || !*value || *value!='0';
  }();
  return enabled;
}

bool TryNativeDrawSubmission(PPCContext& ctx,uint8_t* base,bool indexed) {
  static const bool enabled=[] {
    const auto* value=std::getenv("SR_NATIVE_XDK_DRAW_SUBMISSION");
    return value && *value=='1';
  }();
  if(!enabled || !native::Enabled() || !base) return false;
  const uint64_t dev=ctx.r3.u64;
  if(!dev || (dev&15) || dev+native::profile::kDevice.size>0xa0000000ull) return false;
  auto device=std::span<uint8_t>(base+dev,native::profile::kDevice.size);
  const uint32_t write=native::ReadXdkWord(device,40),limit=native::ReadXdkWord(device,44);
  native::XdkDrawArguments args{indexed,ctx.r4.u32,ctx.r5.u32,
      indexed?ctx.r7.u32:ctx.r6.u32,indexed?ctx.r6.u32:0,0,0};
  if(indexed) {
    const uint32_t object=native::ReadXdkWord(device,native::profile::kDevice.index_buffer);
    if(!object || uint64_t(object)+28>0xa0000000ull) return false;
    const auto index=std::span<const uint8_t>(base+object,28);
    args.index_header=native::ReadXdkWord(index,0);args.index_address=native::ReadXdkWord(index,24);
  }
  native::XdkDrawResult result=native::XdkDrawResult::RingFull;
  native::XdkDrawRefusal refusal=native::XdkDrawRefusal::None;
  const auto ring=native::PlanNativeXdkRing(write,limit,uint32_t(dev),uint32_t(device.size()));
  if(ring) result=native::SubmitNativeXdkDraw(device,{base+ring->host_offset,ring->bytes},args,
      {base,0xa0000000u},&refusal);
  thread_local std::array<uint64_t,3> counts{};
  thread_local std::array<uint64_t,size_t(native::XdkDrawRefusal::Count)> unsupported_refusal_reasons{};
  thread_local std::array<uint64_t,64> unsupported_state_bits{},unsupported_context_bits{},unsupported_misc_bits{};
  thread_local std::array<uint64_t,16> unsupported_shader_masks{};
  thread_local std::array<uint64_t,256> unsupported_shader_mask3_signatures{};
  thread_local std::array<uint64_t,8> unsupported_shader_mask3_modes{};
  thread_local std::array<uint64_t,2> unsupported_shader_mask3_metadata_diff{};
  thread_local std::array<uint64_t,2> unsupported_shader_mask3_ps_alt{};
  thread_local std::array<uint64_t,2> unsupported_shader_mask3_rt_complex{};
  thread_local uint64_t unsupported_shader_mask3_metadata_unreadable=0;
  thread_local std::array<uint64_t,16> unsupported_shader_simple_08470{};
  thread_local std::array<uint64_t,16> unsupported_shader_heavy_08520{};
  thread_local std::array<uint64_t,16> unsupported_shader_heavy_08300{};
  thread_local std::array<uint64_t,16> unsupported_shader_branch_unreadable{};
  thread_local std::array<uint64_t,16> unsupported_shader_08520_cache_hit{};
  thread_local std::array<uint64_t,16> unsupported_shader_08520_cache_miss{};
  thread_local std::array<uint64_t,16> unsupported_shader_08520_ps_alt{};
  thread_local std::array<uint64_t,16> unsupported_shader_08520_program{};
  thread_local uint64_t unsupported_count=0,unsupported_flags=0,unsupported_layout=0;
  if(result==native::XdkDrawResult::UnsupportedState) {
    const auto unsupported=native::InspectNativeXdkUnsupportedState(device,args);
    unsupported_count+=unsupported.count_too_large;
    unsupported_flags+=unsupported.draw_flags;
    unsupported_layout+=unsupported.device_too_small;
    auto accumulate=[](uint64_t mask,std::array<uint64_t,64>& bits) {
      while(mask) {const unsigned bit=std::countr_zero(mask);++bits[bit];mask&=mask-1;}
    };
    accumulate(unsupported.state,unsupported_state_bits);
    accumulate(unsupported.context,unsupported_context_bits);
    accumulate(unsupported.misc,unsupported_misc_bits);
    const unsigned shader_mask=(unsupported.state>>49)&0xfu;
    ++unsupported_shader_masks[shader_mask];
    if(shader_mask==0xdu || shader_mask==0xfu) {
      const uint32_t ps=native::ReadXdkWord(device,12420);
      const uint32_t vs=native::ReadXdkWord(device,12416);
      const uint32_t aux=native::ReadXdkWord(device,11536);
      bool readable=ps!=0 && vs!=0 &&
          uint64_t(ps)+908<=0xa0000000ull && uint64_t(vs)+68<=0xa0000000ull;
      bool metadata_diff=false;
      uint32_t ps_flags=0;
      if(readable) {
        const auto ps_head=std::span<const uint8_t>(base+ps,908);
        const auto vs_head=std::span<const uint8_t>(base+vs,68);
        ps_flags=native::ReadXdkWord(ps_head,872);
        const bool ps_alt=(ps_flags&0x20u)!=0;
        const uint32_t vs_block=native::ReadXdkWord(vs_head,64);
        const uint32_t ps_block=native::ReadXdkWord(ps_head,ps_alt?904:896);
        const uint64_t vs_meta=uint64_t(vs)+vs_block+40;
        const uint64_t ps_meta=uint64_t(ps)+ps_block+872;
        readable=vs_meta+24<=0xa0000000ull && ps_meta+24<=0xa0000000ull;
        if(readable) {
          const auto vs_view=std::span<const uint8_t>(base+vs_meta,24);
          const auto ps_view=std::span<const uint8_t>(base+ps_meta,24);
          metadata_diff=((native::ReadXdkWord(vs_view,20)^native::ReadXdkWord(ps_view,20))&0x7ffffu)!=0;
        }
      }
      if(!readable) {
        ++unsupported_shader_branch_unreadable[shader_mask];
      } else if((device[10810]&0x80u)!=0 || metadata_diff) {
        ++unsupported_shader_heavy_08300[shader_mask];
      } else if(aux!=0 && (ps_flags&0x40u)==0) {
        if((device[10808]&0x80u)!=0) {
          ++unsupported_shader_heavy_08300[shader_mask];
        } else {
          ++unsupported_shader_heavy_08520[shader_mask];
          unsupported_shader_08520_ps_alt[shader_mask]+=((ps_flags&0x20u)!=0);
          unsupported_shader_08520_program[shader_mask]+=((device[10808]&0x40u)!=0);
          auto guest64=[&](uint64_t address) {
            const auto view=std::span<const uint8_t>(base+size_t(address),8);
            return (uint64_t(native::ReadXdkWord(view,0))<<32)|native::ReadXdkWord(view,4);
          };
          const bool cache_readable=uint64_t(ps)+64<=0xa0000000ull &&
              uint64_t(aux)+56<=0xa0000000ull;
          const bool cache_hit=cache_readable &&
              native::ReadXdkWord(std::span<const uint8_t>(base+ps,64),40)==
                  native::ReadXdkWord(std::span<const uint8_t>(base+aux,56),48) &&
              (((guest64(uint64_t(ps)+48)^guest64(uint64_t(dev)+12256))&
                  guest64(uint64_t(aux)+32))==0) &&
              (((guest64(uint64_t(ps)+56)^guest64(uint64_t(dev)+12264))&
                  guest64(uint64_t(aux)+40))==0);
          ++(cache_hit?unsupported_shader_08520_cache_hit[shader_mask]:
              unsupported_shader_08520_cache_miss[shader_mask]);
        }
      } else {
        ++unsupported_shader_simple_08470[shader_mask];
      }
    }
    if(shader_mask==3) {
      unsigned signature=0;
      signature|=native::ReadXdkWord(device,12420)!=0 ? 1u<<0 : 0;
      signature|=native::ReadXdkWord(device,12416)!=0 ? 1u<<1 : 0;
      signature|=native::ReadXdkWord(device,11536)!=0 ? 1u<<2 : 0;
      signature|=(device[10808]&0x40)!=0 ? 1u<<3 : 0;
      signature|=(device[10810]&0x80)!=0 ? 1u<<4 : 0;
      signature|=(device[10810]&0x40)!=0 ? 1u<<5 : 0;
      signature|=(unsupported.state&(uint64_t{1}<<8))!=0 ? 1u<<6 : 0;
      signature|=(native::ReadXdkWord(device,10452)&7u)==5u ? 1u<<7 : 0;
      ++unsupported_shader_mask3_signatures[signature];
      ++unsupported_shader_mask3_modes[native::ReadXdkWord(device,10452)&7u];
      ++unsupported_shader_mask3_rt_complex[(device[10811]&0x20)!=0];
      const uint32_t ps=native::ReadXdkWord(device,12420),vs=native::ReadXdkWord(device,12416);
      bool readable=ps!=0 && vs!=0 && uint64_t(ps)+908<=0xa0000000ull && uint64_t(vs)+68<=0xa0000000ull;
      bool ps_alt=false,metadata_diff=false;
      if(readable) {
        const auto ps_head=std::span<const uint8_t>(base+ps,908);
        const auto vs_head=std::span<const uint8_t>(base+vs,68);
        const uint32_t vs_block=native::ReadXdkWord(vs_head,64);
        const uint32_t ps_block=native::ReadXdkWord(ps_head,896);
        ps_alt=(native::ReadXdkWord(ps_head,872)&0x20u)!=0;
        const uint32_t ps_alt_block=ps_alt?native::ReadXdkWord(ps_head,904):ps_block;
        const uint64_t vs_meta=uint64_t(vs)+vs_block+40;
        const uint64_t ps_meta=uint64_t(ps)+ps_alt_block+872;
        readable=vs_meta+24<=0xa0000000ull && ps_meta+24<=0xa0000000ull;
        if(readable) {
          const auto vs_view=std::span<const uint8_t>(base+vs_meta,24);
          const auto ps_view=std::span<const uint8_t>(base+ps_meta,24);
          metadata_diff=((native::ReadXdkWord(vs_view,20)^native::ReadXdkWord(ps_view,20))&0x7ffffu)!=0;
        }
      }
      if(readable) {
        ++unsupported_shader_mask3_metadata_diff[metadata_diff];
        ++unsupported_shader_mask3_ps_alt[ps_alt];
      } else ++unsupported_shader_mask3_metadata_unreadable;
    }
  }
  if(counts[0]+counts[1]+counts[2]==0)
    REXLOG_INFO("native XDK draw ring: write={:08X} hard_limit={:08X} soft_limit={:08X} mapped={}",
        write,limit,native::ReadXdkWord(device,48),bool(ring));
  ++counts[size_t(result)];
  ++unsupported_refusal_reasons[size_t(refusal)];
  if((counts[0]+counts[1]+counts[2])%65536==0) {
    auto top=[](const std::array<uint64_t,64>& bits) {
      unsigned index=0;for(unsigned i=1;i<bits.size();++i)if(bits[i]>bits[index])index=i;
      return std::pair<unsigned,uint64_t>{index,bits[index]};
    };
    const auto [state_bit,state_hits]=top(unsupported_state_bits);
    const auto [context_bit,context_hits]=top(unsupported_context_bits);
    const auto [misc_bit,misc_hits]=top(unsupported_misc_bits);
    unsigned shader_mask=0;
    for(unsigned i=1;i<unsupported_shader_masks.size();++i)
      if(unsupported_shader_masks[i]>unsupported_shader_masks[shader_mask])shader_mask=i;
    unsigned shader_mask3_signature=0;
    for(unsigned i=1;i<unsupported_shader_mask3_signatures.size();++i)
      if(unsupported_shader_mask3_signatures[i]>unsupported_shader_mask3_signatures[shader_mask3_signature])shader_mask3_signature=i;
    REXLOG_INFO("native XDK draw submission: submitted={} unsupported_state={} ring_full={} (thread cumulative)",
        counts[0],counts[1],counts[2]);
    REXLOG_INFO("native XDK unsupported: count={} flags={} layout={} top_state=b{}:{} top_context=b{}:{} top_misc=b{}:{} shader_mask={:X}:{}",
        unsupported_count,unsupported_flags,unsupported_layout,state_bit,state_hits,
        context_bit,context_hits,misc_bit,misc_hits,shader_mask,unsupported_shader_masks[shader_mask]);
    REXLOG_INFO("native XDK shader masks: 1={} 2={} 3={} 5={} 9={} B={} D={} F={}",
        unsupported_shader_masks[1],unsupported_shader_masks[2],unsupported_shader_masks[3],
        unsupported_shader_masks[5],unsupported_shader_masks[9],unsupported_shader_masks[11],
        unsupported_shader_masks[13],unsupported_shader_masks[15]);
    REXLOG_INFO("native XDK shader D/F branches: D simple={} cache08520={} direct08300={} unreadable={} ; F simple={} cache08520={} direct08300={} unreadable={}",
        unsupported_shader_simple_08470[13],unsupported_shader_heavy_08520[13],
        unsupported_shader_heavy_08300[13],unsupported_shader_branch_unreadable[13],
        unsupported_shader_simple_08470[15],unsupported_shader_heavy_08520[15],
        unsupported_shader_heavy_08300[15],unsupported_shader_branch_unreadable[15]);
    REXLOG_INFO("native XDK shader D/F cache: D hit={} miss={} ps_alt={} program={} ; F hit={} miss={} ps_alt={} program={}",
        unsupported_shader_08520_cache_hit[13],unsupported_shader_08520_cache_miss[13],
        unsupported_shader_08520_ps_alt[13],unsupported_shader_08520_program[13],
        unsupported_shader_08520_cache_hit[15],unsupported_shader_08520_cache_miss[15],
        unsupported_shader_08520_ps_alt[15],unsupported_shader_08520_program[15]);
    REXLOG_INFO("native XDK refusal reasons: gen_unsupported={} miss_ptr={} mode_not_4={} dev_flags={} prog_bit48={} ps_alt20={} meta_diff={} heavy_ps40={} dev10808_80={} vs_list={} ps_list={} cache_tag={} cache_mask={} unreadable={} ring_limit={} ring_full={}",
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::GeneralUnsupported)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::MissingPointers)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::Mode10452Not4)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::DeviceFlags10810_10811)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::ProgramDirtyBit48)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::PsFlagsAlt20)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::VsPsMetaMismatch)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::PsFlagsHeavy40)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::Device10808_80)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::VsListNotEmpty)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::PsListNotEmpty)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::CacheTagMismatch)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::CacheMaskMismatch)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::UnreadableMemory)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::FastRingLimit)],
        unsupported_refusal_reasons[size_t(native::XdkDrawRefusal::RingBufferFull)]);
    REXLOG_INFO("native XDK shader mask3 signature: 0x{:02X}:{} [ps={} vs={} aux={} f10808_40={} f10810_80={} f10810_40={} state8={} mode5={}]",
        shader_mask3_signature,unsupported_shader_mask3_signatures[shader_mask3_signature],
        bool(shader_mask3_signature&1),bool(shader_mask3_signature&2),bool(shader_mask3_signature&4),
        bool(shader_mask3_signature&8),bool(shader_mask3_signature&16),bool(shader_mask3_signature&32),
        bool(shader_mask3_signature&64),bool(shader_mask3_signature&128));
    unsigned shader_mode=0;
    for(unsigned i=1;i<unsupported_shader_mask3_modes.size();++i)
      if(unsupported_shader_mask3_modes[i]>unsupported_shader_mask3_modes[shader_mode])shader_mode=i;
    REXLOG_INFO("native XDK shader mask3 branch: mode={}:{} metadata_same={} metadata_diff={} ps_primary={} ps_alt={} unreadable={}",
        shader_mode,unsupported_shader_mask3_modes[shader_mode],unsupported_shader_mask3_metadata_diff[0],
        unsupported_shader_mask3_metadata_diff[1],unsupported_shader_mask3_ps_alt[0],
        unsupported_shader_mask3_ps_alt[1],unsupported_shader_mask3_metadata_unreadable);
    REXLOG_INFO("native XDK shader mask3 RT branch: simple={} complex={}",
        unsupported_shader_mask3_rt_complex[0],unsupported_shader_mask3_rt_complex[1]);
  }
  return result==native::XdkDrawResult::Submitted;
}
}
#if SR_HOOK_ENABLED(FLOAT_COPY_VS)
SR_DEFINE_HOOK(SR_ADDR_FLOAT_COPY_VS) {
  if(NativeBulkConstantsEnabled() && native::Enabled()) native::TryCopyConstantPrefix(ctx,base,false);
  SR_ORIGINAL(SR_ADDR_FLOAT_COPY_VS)(ctx,base);
}
#endif
#if SR_HOOK_ENABLED(FLOAT_COPY_PS)
SR_DEFINE_HOOK(SR_ADDR_FLOAT_COPY_PS) {
  if(NativeBulkConstantsEnabled() && native::Enabled()) native::TryCopyConstantPrefix(ctx,base,true);
  SR_ORIGINAL(SR_ADDR_FLOAT_COPY_PS)(ctx,base);
}
#endif

#if SR_HOOK_ENABLED(IDLE_YIELD)
namespace {
bool IdleYieldBackoffEnabled() {
  static const bool enabled=[] {
    const char* value=std::getenv("SR_NATIVE_IDLE_YIELD");
    return !value || !*value || *value!='0';
  }();
  return enabled;
}
class IdleYieldWait {
public:
  IdleYieldWait():timer_(CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE)) {}
  ~IdleYieldWait() {if(timer_) CloseHandle(timer_);}
  void Wait() {
    LARGE_INTEGER due;due.QuadPart=-2500; // 250 us, relative 100 ns units.
    if(timer_ && SetWaitableTimerEx(timer_,&due,0,nullptr,nullptr,nullptr,0)) {
      WaitForSingleObject(timer_,1);
    } else Sleep(1);
  }
private:
  HANDLE timer_=nullptr;
};
}
SR_DEFINE_HOOK(SR_ADDR_IDLE_YIELD) {
  // Preserve the original yield's result and volatile/nonvolatile register
  // effects. The host wait only changes how an idle polling thread is parked.
  SR_ORIGINAL(SR_ADDR_IDLE_YIELD)(ctx,base);
  if(IdleYieldBackoffEnabled() && native::Enabled()) {
    thread_local native::IdleYieldBackoff backoff;
    const auto now=std::chrono::steady_clock::now().time_since_epoch();
    if(backoff.Observe(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()))) {
      thread_local IdleYieldWait wait;wait.Wait();
    }
  }
}
#endif

#if SR_HOOK_ENABLED(FRAME_HANDOFF)
// Observe arguments before the dispatch thunk and the actual returned result.
// No lock spans guest execution; the original is called in both modes.
SR_DEFINE_HOOK(SR_ADDR_FRAME_HANDOFF) {
  if(!ResourceUnlockAuditing()) { SR_ORIGINAL(SR_ADDR_FRAME_HANDOFF)(ctx,base); return; }
  if(handoff_depth++==0) handoff_nested=false; else handoff_nested=true;
  const auto observation=BeginHandoff(base,ctx.r3.u32,ctx.r4.u32,uint32_t(ctx.lr));
  SR_ORIGINAL(SR_ADDR_FRAME_HANDOFF)(ctx,base);
  EndHandoff(observation,ctx.r3.u32,handoff_nested);
  --handoff_depth;
}
#endif

// ---- Draws -------------------------------------------------------------------

#if SR_HOOK_ENABLED(DRAW_VERTICES)
// D3DDevice_DrawVertices(dev, PrimType, StartVertex, VertexCount)
SR_DEFINE_HOOK(SR_ADDR_DRAW_VERTICES) {
  const uint32_t probe_caller=uint32_t(ctx.lr),probe_stack=ctx.r1.u32;
  const uint32_t dev = ctx.r3.u32, prim = ctx.r4.u32, start = ctx.r5.u32, count = ctx.r6.u32;
  OnDeviceCall("DrawVertices", dev, prim);
  if(!TryNativeDrawSubmission(ctx,base,false))
    native::ProfileGuestGraphicsOriginal(SR_ADDRESS(SR_ADDR_DRAW_VERTICES),SR_ORIGINAL(SR_ADDR_DRAW_VERTICES),ctx,base);
  native::NoteInstanceProbeDraw(base,dev,"DrawVertices",count,probe_caller,probe_stack);
  if (native::Enabled()) native::Renderer::Get().DrawVertices(base, prim, start, count);
}
#endif

#if SR_HOOK_ENABLED(DRAW_INDEXED_VERTICES)
// D3DDevice_DrawIndexedVertices(dev, PrimType, BaseVertexIndex, StartIndex, IndexCount)
SR_DEFINE_HOOK(SR_ADDR_DRAW_INDEXED_VERTICES) {
  const uint32_t probe_caller=uint32_t(ctx.lr),probe_stack=ctx.r1.u32;
  const uint32_t dev = ctx.r3.u32, prim = ctx.r4.u32, base_vertex = ctx.r5.u32,
                 start = ctx.r6.u32, count = ctx.r7.u32;
  OnDeviceCall("DrawIndexedVertices", dev, prim);
  if(!TryNativeDrawSubmission(ctx,base,true))
    native::ProfileGuestGraphicsOriginal(SR_ADDRESS(SR_ADDR_DRAW_INDEXED_VERTICES),SR_ORIGINAL(SR_ADDR_DRAW_INDEXED_VERTICES),ctx,base);
  native::NoteInstanceProbeDraw(base,dev,"DrawIndexedVertices",count,probe_caller,probe_stack);
  if (native::Enabled()) {
    native::Renderer::Get().DrawIndexedVertices(base, prim, int32_t(base_vertex), start, count);
  }
}
#endif

#if SR_HOOK_ENABLED(DRAW_VERTICES_UP)
// D3DDevice_DrawVerticesUP(dev, PrimType, VertexCount, pVertexData, Stride) - Begin/End based.
SR_DEFINE_HOOK(SR_ADDR_DRAW_VERTICES_UP) {
  const uint32_t probe_caller=uint32_t(ctx.lr),probe_stack=ctx.r1.u32;
  const uint32_t dev = ctx.r3.u32, prim = ctx.r4.u32, count = ctx.r5.u32, data = ctx.r6.u32,
                 stride = ctx.r7.u32;
  OnDeviceCall("DrawVerticesUP", dev, prim);
  t_in_draw_up = true;
  native::ProfileGuestGraphicsOriginal(SR_ADDRESS(SR_ADDR_DRAW_VERTICES_UP),SR_ORIGINAL(SR_ADDR_DRAW_VERTICES_UP),ctx,base);
  t_in_draw_up = false;
  native::NoteInstanceProbeDraw(base,dev,"DrawVerticesUP",count,probe_caller,probe_stack);
  if (native::Enabled()) native::Renderer::Get().DrawInlineVertices(base, prim, data, count, stride);
}
#endif

#if SR_HOOK_ENABLED(BEGIN_VERTICES)
// D3DDevice_BeginVertices(dev, PrimType, VertexCount, Stride) -> r3 = where the
// game writes the inline vertices (inside the command segment).
SR_DEFINE_HOOK(SR_ADDR_BEGIN_VERTICES) {
  const uint32_t dev = ctx.r3.u32, prim = ctx.r4.u32, count = ctx.r5.u32, stride = ctx.r6.u32;
  if (!t_in_draw_up) OnDeviceCall("BeginVertices", dev, prim);
  native::ProfileGuestGraphicsOriginal(SR_ADDRESS(SR_ADDR_BEGIN_VERTICES),SR_ORIGINAL(SR_ADDR_BEGIN_VERTICES),ctx,base);
  if (native::Enabled() && !t_in_draw_up) {
    g_pending_inline = {true, prim, count, stride, ctx.r3.u32};
  }
}
#endif

#if SR_HOOK_ENABLED(END_VERTICES)
// D3DDevice_EndVertices(dev): the inline vertex data is complete here.
SR_DEFINE_HOOK(SR_ADDR_END_VERTICES) {
  native::NoteHookCall("EndVertices", ctx.r3.u32, 0);
  if (g_pending_inline.active) {
    g_pending_inline.active = false;
    native::NoteInstanceProbeDraw(base,ctx.r3.u32,"EndVertices",g_pending_inline.count,uint32_t(ctx.lr),ctx.r1.u32);
    native::Renderer::Get().DrawInlineVertices(base, g_pending_inline.prim, g_pending_inline.data,
                                               g_pending_inline.count, g_pending_inline.stride);
  }
  native::ProfileGuestGraphicsOriginal(SR_ADDRESS(SR_ADDR_END_VERTICES),SR_ORIGINAL(SR_ADDR_END_VERTICES),ctx,base);
}
#endif

// ---- Resolve, tiling, clear ----------------------------------------------------

#if SR_HOOK_ENABLED(RESOLVE)
// D3DDevice_Resolve(dev, Flags, pSrcRect, pDestTexture, pDestPoint, Level,
// Slice, pClearColor, ClearZ, ClearStencil, pParameters)
SR_DEFINE_HOOK(SR_ADDR_RESOLVE) {
  const uint32_t dev = ctx.r3.u32, flags = ctx.r4.u32, rect = ctx.r5.u32, dest = ctx.r6.u32,
                 point = ctx.r7.u32, clear_color = ctx.r10.u32;
  const float clear_z = float(ctx.f1.f64);
  const uint32_t level=ctx.r8.u32,slice=ctx.r9.u32;
  // sub_8210C5F8 reads ClearStencil at old SP+92 (new SP+460
  // after its 368-byte prologue). Capture before calling the original.
  const uint32_t clear_stencil=__builtin_bswap32(*reinterpret_cast<const uint32_t*>(base+ctx.r1.u32+92));
  OnDeviceCall("Resolve", dev, flags);
  // Original first: the resolve's RB_COPY_* registers land in the command
  // stream the PM4 mirror parses.
  native::ProfileGuestGraphicsOriginal(SR_ADDRESS(SR_ADDR_RESOLVE),SR_ORIGINAL(SR_ADDR_RESOLVE),ctx,base);
  if (native::Enabled()) {
    native::Renderer::Get().Resolve(base, flags, rect, dest, point, clear_color, clear_z, clear_stencil,level,slice);
  }
}
#endif

#if SR_HOOK_ENABLED(BEGIN_TILING)
// D3DDevice_BeginTiling(dev, Flags, Count, pTileRects, pClearColor, ClearZ, ClearStencil)
SR_DEFINE_HOOK(SR_ADDR_BEGIN_TILING) {
  OnDeviceCall("BeginTiling", ctx.r3.u32, ctx.r5.u32);
  if (native::Enabled()) {
    native::Renderer::Get().BeginTiling(base, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32,
                                        float(ctx.f1.f64), ctx.r8.u32);
  }
  native::ProfileGuestGraphicsOriginal(SR_ADDRESS(SR_ADDR_BEGIN_TILING),SR_ORIGINAL(SR_ADDR_BEGIN_TILING),ctx,base);
}
#endif

#if SR_HOOK_ENABLED(END_TILING)
// D3DDevice_EndTiling: its per-tile resolves go through the Resolve hook.
SR_DEFINE_HOOK(SR_ADDR_END_TILING) {
  OnDeviceCall("EndTiling", ctx.r3.u32, 0);
  native::ProfileGuestGraphicsOriginal(SR_ADDRESS(SR_ADDR_END_TILING),SR_ORIGINAL(SR_ADDR_END_TILING),ctx,base);
  if (native::Enabled()) native::Renderer::Get().EndTiling();
}
#endif

#if SR_HOOK_ENABLED(CLEAR)
// The shared float4 clear entry also catches direct engine calls that bypass
// the public D3DCOLOR wrapper. A null rectangle clears the whole surface.
SR_DEFINE_HOOK(SR_ADDR_CLEAR) {
  const uint32_t dev = ctx.r3.u32, flags = ctx.r4.u32, rect = ctx.r5.u32,
                 color_ptr = ctx.r6.u32, stencil = ctx.r8.u32;
  float color[4] = {};
  if (color_ptr && (flags & 0xF)) {
    for (uint32_t i = 0; i < 4; ++i) {
      uint32_t bits = GuestLoad32(base, color_ptr + 4 * i);
      std::memcpy(&color[i], &bits, 4);
    }
  }
  const float z = float(ctx.f1.f64);
  OnDeviceCall("Clear", dev, flags);
  native::ProfileGuestGraphicsOriginal(SR_ADDRESS(SR_ADDR_CLEAR),SR_ORIGINAL(SR_ADDR_CLEAR),ctx,base);
  if (native::Enabled()) {
    native::Renderer::Get().Clear(base, rect ? 1u : 0u, rect, flags, color, z, stencil);
  }
}
#endif

// ---- Command segment, constants, buffers ---------------------------------------

#if SR_HOOK_ENABLED(RING_MAKE_SPACE)
// XDK command segment switch (segment full / kickoff): the PM4 mirror parses
// the tail of the old segment and resynchronizes on the new one.
SR_DEFINE_HOOK(SR_ADDR_RING_MAKE_SPACE) {
  const uint32_t dev = ctx.r3.u32;
  OnDeviceCall("RingMakeSpace", dev, 0);
  if (native::Enabled()) native::Renderer::Get().SyncRing(base, dev);
  SR_ORIGINAL(SR_ADDR_RING_MAKE_SPACE)(ctx, base);
  if (native::Enabled()) native::Renderer::Get().ResyncRing(base, dev);
}
#endif

#if SR_HOOK_ENABLED(RING_ALLOC_LARGE)
// Large command segment allocation (after the segment switch could not satisfy it).
SR_DEFINE_HOOK(SR_ADDR_RING_ALLOC_LARGE) {
  const uint32_t dev = ctx.r3.u32;
  OnDeviceCall("RingAllocLarge", dev, 0);
  if (native::Enabled()) native::Renderer::Get().SyncRing(base, dev);
  SR_ORIGINAL(SR_ADDR_RING_ALLOC_LARGE)(ctx, base);
  if (native::Enabled()) native::Renderer::Get().ResyncRing(base, dev);
}
#endif

#if SR_HOOK_ENABLED(RESERVE_INLINE_CONSTANTS)
// Inline shader constant upload: (dev, r4, r5, vec4_count) reserves a PM4
// SET_CONSTANT packet in the ring and returns the data pointer the game
// fills. The ALU register is ((r5 - (r4 << 8)) * 4) & 0x7FC dwords.
SR_DEFINE_HOOK(SR_ADDR_RESERVE_INLINE_CONSTANTS) {
  const uint32_t r4 = ctx.r4.u32, r5 = ctx.r5.u32, count = ctx.r6.u32;
  OnDeviceCall("ReserveInlineConstants", ctx.r3.u32, count);
  SR_ORIGINAL(SR_ADDR_RESERVE_INLINE_CONSTANTS)(ctx, base);
  if (native::Enabled() && ctx.r3.u32) {
    const uint32_t unified = (r5 - (r4 << 8)) & 0x1FF;
    native::Renderer::Get().NoteRingConstants(unified >= 256, unified & 0xFF, count, ctx.r3.u32);
  }
}
#endif

#if SR_HOOK_ENABLED(LOAD_SHADER_LITERALS)
// Shader literal constants: at shader bind the XDK emits LOAD_ALU_CONSTANT
// packets from the shader object; (dev, table, data_base) walks
// {u16 vec4 register, u16 dword count, u32 offset} entries.
SR_DEFINE_HOOK(SR_ADDR_LOAD_SHADER_LITERALS) {
  OnDeviceCall("LoadShaderLiterals", ctx.r3.u32, ctx.r4.u32);
  if (native::Enabled()) {
    native::Renderer::Get().ApplyLoadAluConstants(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
  }
  SR_ORIGINAL(SR_ADDR_LOAD_SHADER_LITERALS)(ctx, base);
}
#endif

#if SR_HOOK_ENABLED(GPU_BEGIN_SHADER_CONSTANT_F4)
// D3DDevice_GpuBeginShaderConstantF4(dev, bPixelShader, StartRegister,
//   ppCachedConstants, ppWriteCombinedConstants, Vector4fCount): the game
// writes constants through the returned ring pointer only.
SR_DEFINE_HOOK(SR_ADDR_GPU_BEGIN_SHADER_CONSTANT_F4) {
  const bool pixel = ctx.r4.u32 != 0;
  const uint32_t start = ctx.r5.u32, ring_out = ctx.r7.u32, count = ctx.r8.u32;
  OnDeviceCall("GpuBeginShaderConstantF4", ctx.r3.u32, count);
  SR_ORIGINAL(SR_ADDR_GPU_BEGIN_SHADER_CONSTANT_F4)(ctx, base);
  if (native::Enabled() && ring_out) {
    native::Renderer::Get().NoteRingConstants(pixel, start, count, GuestLoad32(base, ring_out));
  }
}
#endif

// D3DVertexBuffer_Unlock / D3DIndexBuffer_Unlock (r3 = buffer object): the
// guest has rewritten the buffer contents.
[[maybe_unused]] static void InvalidateBufferObject(uint8_t* base, uint32_t object) {
  if (!object || !native::Enabled()) return;
  const uint32_t address = GuestLoad32(base, object + 0x18) & ~3u;
  const uint32_t size = GuestLoad32(base, object + 0x1C) & 0x00FFFFFFu;
  native::Renderer::Get().InvalidateGuestRange(address, size ? size : 0x10000);
}

#if SR_HOOK_ENABLED(VERTEX_BUFFER_UNLOCK)
SR_DEFINE_HOOK(SR_ADDR_VERTEX_BUFFER_UNLOCK) {
  const uint32_t object = ctx.r3.u32;
  native::NoteHookCall("VertexBufferUnlock", object, 0);
  SR_ORIGINAL(SR_ADDR_VERTEX_BUFFER_UNLOCK)(ctx, base);
  InvalidateBufferObject(base, object);
}
#endif

#if SR_HOOK_ENABLED(INDEX_BUFFER_UNLOCK)
SR_DEFINE_HOOK(SR_ADDR_INDEX_BUFFER_UNLOCK) {
  const uint32_t object = ctx.r3.u32;
  native::NoteHookCall("IndexBufferUnlock", object, 0);
  SR_ORIGINAL(SR_ADDR_INDEX_BUFFER_UNLOCK)(ctx, base);
  InvalidateBufferObject(base, object);
}
#endif

// ---- Frame boundary and GPU waits ----------------------------------------------

#if SR_HOOK_ENABLED(RESOURCE_UNLOCK)
SR_DEFINE_HOOK(SR_ADDR_RESOURCE_UNLOCK) {
  const uint32_t object=ctx.r3.u32, base_page=ctx.r4.u32, mip_page=ctx.r5.u32;
  const uint32_t caller=uint32_t(ctx.lr);
  SR_ORIGINAL(SR_ADDR_RESOURCE_UNLOCK)(ctx,base);
  if (ResourceUnlockAuditing()) ObserveResourceUnlock(base,object,base_page,mip_page,caller);
}
#endif

#if SR_HOOK_ENABLED(SWAP) && SR_HEX(SR_ADDR_SWAP) != SR_FRAME_STATS_SWAP_HOOK
// D3DDevice_Swap(dev, pFrontBuffer, pParameters) when it is not the function
// frame_stats.cpp already hooks. The original still runs so the guest's
// swap/vblank/fence semantics are untouched; the native frame is presented
// right after.
SR_DEFINE_HOOK(SR_ADDR_SWAP) {
  const uint32_t dev = ctx.r3.u32, front_buffer = ctx.r4.u32;
  static uint64_t swap_number = 0;
  ++swap_number;
  SR_ORIGINAL(SR_ADDR_SWAP)(ctx, base);
  native::NoteGuestSwap(base, dev, front_buffer);
  if (native::Enabled()) {
    native::HangWatchdogBeat();
    native::Renderer::Get().OnSwap(base, front_buffer, swap_number);
  }
}
#endif

// XDK GPU waits. BlockOnFence (r3 = device, r4 = fence) loops calling the poll
// (a few pause instructions, a read-pointer check and the XDK's hang
// detector; returns 1 while the caller should keep waiting) until the GPU's
// fence counter passes `fence`:
//   (dev[fence_current] - fence) >= (dev[fence_current] - *dev[fence_completed_ptr])
// On a PC that spin burns a core. With the native graphics system the poll
// sleeps until that exact condition holds (inside BlockOnFence) or any GPU
// progress (elsewhere), 1 ms cap either way so the hang detector still runs.
// Under Xenos (xenos / A/B mode) both waits are no-ops: the spin is unchanged.
#if SR_HOOK_ENABLED(BLOCK_ON_FENCE)
SR_DEFINE_HOOK(SR_ADDR_BLOCK_ON_FENCE) {
  const uint32_t saved_dev = t_fence_dev, saved_value = t_fence_value;
  t_fence_dev = ctx.r3.u32;
  t_fence_value = ctx.r4.u32;
  OnDeviceCall("BlockOnFence", ctx.r3.u32, ctx.r4.u32);
  SR_ORIGINAL(SR_ADDR_BLOCK_ON_FENCE)(ctx, base);
  t_fence_dev = saved_dev;
  t_fence_value = saved_value;
}
#endif

#if SR_HOOK_ENABLED(POLL_GPU_PROGRESS)
SR_DEFINE_HOOK(SR_ADDR_POLL_GPU_PROGRESS) {
  const uint64_t generation = native::GpuProgressGeneration();
  native::NoteHookCall("PollGpuProgress", ctx.r3.u32, 0);
  SR_ORIGINAL(SR_ADDR_POLL_GPU_PROGRESS)(ctx, base);
  if (ctx.r3.u32 != 1) return;
  if (t_fence_dev) {
    const uint32_t dev = t_fence_dev, fence = t_fence_value;
    native::WaitForGpuCondition(
        [base, dev, fence] {
          const auto& layout = native::profile::kDevice;
          const uint32_t current = GuestLoad32(base, dev + layout.fence_current);
          const uint32_t completed =
              GuestLoad32(base, GuestLoad32(base, dev + layout.fence_completed_ptr));
          return current - fence >= current - completed;
        },
        1000);
  } else {
    native::WaitForGpuProgress(generation, 1000);
  }
}
#endif
