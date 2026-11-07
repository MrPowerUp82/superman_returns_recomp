#include "coop_lua.h"
#include "coop_join_script.h"
#include "coop_session.h"
#include <array>

#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/runtime.h>

#include "generated/default/superman_returns_init.h"

#ifdef _WIN32
#include <windows.h>
#endif

REXCVAR_DEFINE_STRING(sr_coop_script, "", "Superman Returns",
                      "Development: run a local Lua file once per edit on the game thread; "
                      "empty disables the co-op script mailbox")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(sr_coop_autojoin, false, "Superman Returns",
                    "Experimental: enable F2 co-op lifecycle, initially disabled; requires co-op input")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DECLARE(bool, sr_coop_input);

namespace superman_returns::coop {
namespace {
constexpr uint32_t kScriptCapacity = 64 * 1024;
constexpr uint32_t kLuaManager = 0x8296D8CC;
constexpr uint32_t kContextStack = 0x8296E114;
constexpr uint32_t kInputManager = 0x8296E8C8;
std::atomic<uint32_t> script_buffer{0};
std::filesystem::path script_path;
std::mutex mailbox_mutex;
thread_local bool running_mailbox = false;
bool automatic_session = false;
std::atomic<bool> interactive_session{false};
std::atomic<uint64_t> runtime_state{1};
std::atomic<uint64_t> requested_session{0};
constexpr char kRemoveScript[]=R"(-- sr-coop-context: player1
local p1=GetIDFromBinIndex("SUPERMAN",0)
if not p1 or p1==0 or p1==255 then return "P1 not ready" end
if GetDASValue("currentControl",p1)~=2 then return "P1 not in gameplay control" end
local p2=SR_COOP_P2
if p2 then
  SetCameraHomeEntity(p1)
  DespawnEntity(p2)
end
SR_COOP_P2=nil
SR_COOP_LAST_POS=nil
return "disabled"
)";
enum class InternalScript {None,Spawn,Remove};

bool ReadWord(const uint8_t* base, uint32_t address, uint32_t& result) {
#ifdef _WIN32
  SIZE_T copied = 0;
  if (!address || !ReadProcessMemory(GetCurrentProcess(), base + address,
                                    &result, sizeof(result), &copied) || copied != sizeof(result))
    return false;
  result = __builtin_bswap32(result);
  return true;
#else
  return false;
#endif
}

std::string ReadString(const uint8_t* base, uint32_t address) {
  std::string result;
#ifdef _WIN32
  if (!address) return result;
  for (uint32_t i = 0; i < 1024 && uint64_t(address) + i < (uint64_t{1} << 32); ++i) {
    char value = 0;
    SIZE_T copied = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), base + address + i, &value, 1, &copied) ||
        copied != 1 || value == 0) break;
    result.push_back(value);
  }
#endif
  return result;
}

void ReturnLuaBool(PPCContext& ctx, uint8_t* base, bool value) {
  const PPCContext saved = ctx;
  ctx.r4.u64 = value;
  sub_8275CCF0(ctx, base);  // lua_pushboolean
  ctx = saved;
  ctx.r3.u64 = 1;
}

void MaybeRunScript(PPCContext& ctx, uint8_t* base, uint32_t lua) {
  const uint32_t buffer = script_buffer.load(std::memory_order_acquire);
  if (!buffer || running_mailbox) return;

  // SpawnEntity depends on the current script entity's flow. Do not execute
  // during VM initialization, or against another Lua state/coroutine.
  uint32_t manager = 0, main_lua = 0, begin = 0, end = 0, entity = 0;
  if (!ReadWord(base, kLuaManager, manager) || !manager ||
      !ReadWord(base, manager + 12, main_lua) || main_lua != lua ||
      !ReadWord(base, kContextStack, begin) || !ReadWord(base, kContextStack + 4, end) ||
      !begin || end <= begin || ((end - begin) & 3) ||
      !ReadWord(base, end - 4, entity) || !entity) return;

  std::unique_lock lock(mailbox_mutex, std::try_to_lock);
  if (!lock) return;
  using Clock = std::chrono::steady_clock;
  static auto next_poll = Clock::time_point{};
  static bool has_stamp = false;
  static std::filesystem::file_time_type last_stamp;
  static std::string pending_script;
  static InternalScript operation=InternalScript::None;
  static uint64_t pending_request=0;
  static auto next_join = Clock::time_point{};
  static SessionLifecycle lifecycle;
  const auto now = Clock::now();
  const auto request=requested_session.load(std::memory_order_acquire);
  if(operation!=InternalScript::None && pending_request!=request) {
    pending_script.clear();operation=InternalScript::None;next_join={};
  }
  if (!script_path.empty() && now >= next_poll) {
    next_poll = now + std::chrono::milliseconds(250);
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(script_path, error);
    if (error) {
      if (operation==InternalScript::None) pending_script.clear();
      has_stamp = false;
    } else if (!has_stamp || stamp != last_stamp) {
      std::ifstream input(script_path, std::ios::binary);
      if (!input) return;  // retry transient open failures without consuming the edit
      std::string incoming(kScriptCapacity, '\0');
      input.read(incoming.data(), incoming.size());
      const auto count = input.gcount();
      last_stamp = stamp;
      has_stamp = true;
      pending_script.clear();
      operation=InternalScript::None;
      if (input.bad() || count == 0 || count >= kScriptCapacity) {
        REXLOG_WARN("coop Lua: script must contain 1..65535 bytes");
        return;
      }
      incoming.resize(size_t(count));
      // PowerShell 5 writes a UTF-8 BOM; Lua 5.0's loader doesn't skip it.
      if (incoming.starts_with("\xEF\xBB\xBF")) incoming.erase(0, 3);
      pending_script = std::move(incoming);
      REXLOG_INFO("coop Lua: queued {} bytes", pending_script.size());
    }
  }
  if (pending_script.empty() && automatic_session && now >= next_join) {
    const auto action=lifecycle.Next(request);
    if(action!=SessionAction::None) {
      pending_script=action==SessionAction::Spawn?kJoinScript:kRemoveScript;
      operation=action==SessionAction::Spawn?InternalScript::Spawn:InternalScript::Remove;
      pending_request=request;
    }
  }
  if (pending_script.empty()) return;

  // Script contexts are ScriptEntity interfaces, not actor objects. Spawn's
  // world-flow lookup requires the real actor script context. Check every valid
  // callback for a pending chunk; limiting this check to the disk poll interval
  // can repeatedly miss an actor whose callbacks occupy another frame phase.
  const int context_port = pending_script.starts_with("-- sr-coop-context: player1") ? 0 :
      pending_script.starts_with("-- sr-coop-context: player2") ? 1 : -1;
  if (context_port >= 0) {
    const PPCContext saved = ctx;
    ctx.r3.u64 = lua;
    ctx.r4.u64 = 100001;  // missing argument -> resolve the current script actor
    sub_82262B38(ctx, base);
    const uint32_t current_actor = ctx.r3.u32;
    ctx = saved;
    uint32_t manager = 0, slots = 0, target_id = 0, count_word = 0;
    if (!current_actor || !ReadWord(base, kInputManager, manager) || !manager ||
        !ReadWord(base, manager + 8, count_word) ||
        (count_word >> 24) <= static_cast<uint32_t>(context_port) ||
        !ReadWord(base, manager + 4, slots) || !slots ||
        !ReadWord(base, slots + context_port * 4, target_id) || target_id == 255) return;
    ctx.r3.u64 = target_id;
    sub_821C23E0(ctx, base);
    const uint32_t target_actor = ctx.r3.u32;
    ctx = saved;
    if (!target_actor || target_actor != current_actor) return;

  }
  std::string script;
  script.swap(pending_script);
  const auto executed=operation;
  operation=InternalScript::None;
  const bool automatic=executed==InternalScript::Spawn;
  const auto action=automatic?SessionAction::Spawn:
      executed==InternalScript::Remove?SessionAction::Remove:SessionAction::None;
  // Pessimistically consume operations before guest calls. Only exact read-only
  // waiting replies or verified success may advance the lifecycle.
  lifecycle.Begin(action);

  const PPCContext saved = ctx;
  struct RestoreContext {
    PPCContext& target;
    const PPCContext& saved;
    ~RestoreContext() { target = saved; running_mailbox = false; }
  } restore{ctx, saved};
  running_mailbox = true;
  if (executed==InternalScript::None) REXLOG_INFO("coop Lua: executing {} bytes context={:08X}", script.size(), entity);

  ctx.r3.u64 = lua;
  sub_8275BDA0(ctx, base);  // lua_gettop
  const int32_t top = ctx.r3.s32;
  if (top < 0 || top > 100000) {
    REXLOG_WARN("coop Lua: unexpected stack height {}; script skipped", top);
    return;
  }
  std::memcpy(base + buffer, script.data(), script.size());
  const uint32_t name = buffer + kScriptCapacity;
  constexpr char chunk_name[] = "@sr-coop-mailbox";
  std::memcpy(base + name, chunk_name, sizeof(chunk_name));
  ctx.r3.u64 = lua;
  ctx.r4.u64 = buffer;
  ctx.r5.u64 = script.size();
  ctx.r6.u64 = name;
  sub_82761C48(ctx, base);  // luaL_loadbuffer
  uint32_t status = ctx.r3.u32;
  if (status == 0) {
    ctx.r3.u64 = lua;
    ctx.r4.u64 = 0;
    ctx.r5.u64 = 1;
    ctx.r6.u64 = 0;
    __imp__sub_8275D340(ctx, base);  // lua_pcall; bypass this observer
    status = ctx.r3.u32;
  }
  ctx.r3.u64 = lua;
  ctx.r4.s64 = -1;
  sub_8275C638(ctx, base);  // lua_tostring
  const std::string message = ReadString(base, ctx.r3.u32);
  ctx.r3.u64 = lua;
  ctx.r4.s64 = top;
  sub_8275BDB8(ctx, base);  // lua_settop
  if (executed==InternalScript::Spawn || executed==InternalScript::Remove) {
    const bool waiting = status == 0 && (message == "world not ready" ||
        message == "P1 not ready" || message == "P1 not in gameplay control");
    uint32_t p1=0,p2=0;std::array<double,3> spawn{};int consumed=0;
    const bool joined=automatic && status==0 && std::sscanf(message.c_str(),"P1=%u P2=%u spawn=%lf,%lf,%lf%n",
        &p1,&p2,&spawn[0],&spawn[1],&spawn[2],&consumed)==5 && size_t(consumed)==message.size() &&
        p1 && p2 && p1!=p2 && p1!=255 && p2!=255 &&
        std::isfinite(spawn[0]) && std::isfinite(spawn[1]) && std::isfinite(spawn[2]);
    const bool removed=!automatic && status==0 && message=="disabled";
    lifecycle.Complete(action,waiting?SessionResult::Waiting:
        (joined || removed)?SessionResult::Success:SessionResult::Failed);
    const auto epoch=(runtime_state.load(std::memory_order_relaxed)&~uint64_t{1})+((joined || removed)?2:0);
    runtime_state.store(epoch|uint64_t(lifecycle.Active()),std::memory_order_release);
    if (waiting) {
      next_join = Clock::now() + std::chrono::seconds(1);
    } else {
      next_join={};
      if(automatic)REXLOG_INFO("coop session spawn: status={} context={:08X} result={} active={}",status,entity,message,lifecycle.Active());
      else REXLOG_INFO("coop session: status={} result={} active={}",status,message,lifecycle.Active());
    }

  } else {
    REXLOG_INFO("coop Lua: status={} context={:08X} result={}", status, entity, message);
  }
}
}  // namespace

void InstallLuaBridge(rex::Runtime& runtime) {
  const std::string path = REXCVAR_GET(sr_coop_script);
  const bool automatic = REXCVAR_GET(sr_coop_autojoin) && REXCVAR_GET(sr_coop_input);
  if (path.empty() && !automatic) return;
#ifdef _WIN32
  script_path = path;
  automatic_session = automatic;
  const uint32_t buffer = runtime.memory()->SystemHeapAlloc(kScriptCapacity + 64);
  if (!buffer) {
    REXLOG_ERROR("coop Lua: could not allocate the script mailbox");
    return;
  }
  script_buffer.store(buffer, std::memory_order_release);
  runtime_state.store(automatic?0:1,std::memory_order_release);
  requested_session.store(0,std::memory_order_release);
  interactive_session.store(automatic,std::memory_order_release);
  if (!path.empty()) REXLOG_INFO("coop Lua: local development mailbox enabled");
  if (automatic) REXLOG_INFO("coop session ready: initially disabled; press F2 during gameplay to spawn P2 beside P1");
#else
  REXLOG_WARN("coop Lua: mailbox currently requires Windows checked guest reads");
#endif
}

bool CanToggleCoop() {return interactive_session.load(std::memory_order_acquire);}
void RequestCoopToggle() {
  if(!CanToggleCoop())return;
  const auto request=requested_session.fetch_add(1,std::memory_order_acq_rel)+1;
  REXLOG_INFO("coop session request: revision={} enabled={}",request,bool(request&1));
}
bool RuntimeCoopActive() {
  return bool(RuntimeCoopState()&1);
}
uint64_t RuntimeCoopState() {
  return interactive_session.load(std::memory_order_acquire)?runtime_state.load(std::memory_order_acquire):1;
}

}  // namespace superman_returns::coop

// Bridge-only extension: SetActorToDefaultController(actor, port).
// Calls made by the guest itself, or outside the built-in join/mailbox, keep the
// original single-argument behavior. Ports are zero-based, as in XamInputGetState.
REX_HOOK_RAW(sub_8229F778) {
  using namespace superman_returns::coop;
  if (!running_mailbox) {
    __imp__sub_8229F778(ctx, base);
    return;
  }
  const PPCContext saved = ctx;
  sub_8275BDA0(ctx, base);
  const int32_t top = ctx.r3.s32;
  ctx = saved;
  if (top != 2) {
    __imp__sub_8229F778(ctx, base);
    return;
  }

  // Match Lua's numeric conversion, then validate before narrowing to an index.
  ctx.r4.s64 = 2;
  sub_8275C110(ctx, base);  // lua_isnumber
  const bool number = ctx.r3.u32 != 0;
  ctx = saved;
  ctx.r4.s64 = 2;
  sub_8275C518(ctx, base);  // lua_tonumber
  const double requested_port = ctx.f1.f64;
  ctx = saved;
  if (!number || !std::isfinite(requested_port) || requested_port < 0 ||
      requested_port > 3 || std::floor(requested_port) != requested_port) {
    REXLOG_WARN("coop Lua: controller port must be an integer in 0..3");
    ReturnLuaBool(ctx, base, false);
    return;
  }
  const uint32_t port = static_cast<uint32_t>(requested_port);
  ctx.r4.u64 = 1;
  sub_82262B38(ctx, base);  // resolve the actor and its ACTR interface
  const uint32_t actor = ctx.r3.u32;
  ctx = saved;
  uint32_t manager = 0, slots = 0, actor_id = 0, occupant = 0, count_word = 0;
  if (!actor || !ReadWord(base, actor + 88, actor_id) ||
      !ReadWord(base, kInputManager, manager) || !manager ||
      !ReadWord(base, manager + 8, count_word) || (count_word >> 24) <= port ||
      !ReadWord(base, manager + 4, slots) || !slots ||
      !ReadWord(base, slots + port * 4, occupant)) {
    REXLOG_WARN("coop Lua: refused actor binding to invalid port {}", port);
    ReturnLuaBool(ctx, base, false);
    return;
  }
  if (occupant != 255 && occupant != actor_id) {
    ctx.r3.u64 = occupant;
    sub_821C23E0(ctx, base);
    const bool occupied_by_live_entity = ctx.r3.u32 != 0;
    ctx = saved;
    if (occupied_by_live_entity) {
      REXLOG_WARN("coop Lua: refused actor binding to occupied port {} (actor={})", port, occupant);
      ReturnLuaBool(ctx, base, false);
      return;
    }
    // DespawnEntity does not clear this table. The native binding routine
    // already handles an old slot whose entity lookup returns null.
    REXLOG_INFO("coop Lua: reclaiming stale actor={} on guest port={}", occupant, port);
  }
  ctx.r3.u64 = manager;
  ctx.r4.u64 = actor;
  ctx.r5.u64 = port;
  sub_8229F380(ctx, base);  // the guest's existing actor/port binding routine
  ctx = saved;
  ReturnLuaBool(ctx, base, true);  // only the two-argument extension returns a bool
  REXLOG_INFO("coop Lua: actor={} bound to guest port={}", actor_id, port);
}

REX_HOOK_RAW(sub_8275D340) {
  const uint32_t lua = ctx.r3.u32;
  __imp__sub_8275D340(ctx, base);
  if (ctx.r3.u32 == 0) superman_returns::coop::MaybeRunScript(ctx, base, lua);
}
