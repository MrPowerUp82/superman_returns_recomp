#include "coop_lua.h"
#include "coop_join_script.h"

#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
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
                    "Experimental: create P2 once after P1 enters gameplay; requires co-op input")
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
bool autojoin_enabled = false;

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
  static bool pending_autojoin = false;
  static auto next_join = Clock::time_point{};
  const auto now = Clock::now();
  if (!script_path.empty() && now >= next_poll) {
    next_poll = now + std::chrono::milliseconds(250);
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(script_path, error);
    if (error) {
      if (!pending_autojoin) pending_script.clear();
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
      pending_autojoin = false;
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
  if (pending_script.empty() && autojoin_enabled && now >= next_join) {
    pending_script = kJoinScript;
    pending_autojoin = true;
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
  const bool automatic = pending_autojoin;
  pending_autojoin = false;
  // Stop by default before invoking any guest code. Only a read-only readiness
  // result may re-arm the join; errors and partial spawns never create duplicates.
  if (automatic) autojoin_enabled = false;

  const PPCContext saved = ctx;
  struct RestoreContext {
    PPCContext& target;
    const PPCContext& saved;
    ~RestoreContext() { target = saved; running_mailbox = false; }
  } restore{ctx, saved};
  running_mailbox = true;
  if (!automatic) REXLOG_INFO("coop Lua: executing {} bytes context={:08X}", script.size(), entity);

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
  if (automatic) {
    const bool waiting = status == 0 && (message == "world not ready" ||
        message == "P1 not ready" || message == "P1 not in gameplay control");
    if (waiting) {
      autojoin_enabled = true;
      next_join = Clock::now() + std::chrono::seconds(1);
    } else {
      REXLOG_INFO("coop autojoin: status={} context={:08X} result={}; automatic join finished",
                  status, entity, message);
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
  autojoin_enabled = automatic;
  const uint32_t buffer = runtime.memory()->SystemHeapAlloc(kScriptCapacity + 64);
  if (!buffer) {
    REXLOG_ERROR("coop Lua: could not allocate the script mailbox");
    return;
  }
  script_buffer.store(buffer, std::memory_order_release);
  if (!path.empty()) REXLOG_INFO("coop Lua: local development mailbox enabled");
  if (automatic) REXLOG_INFO("coop autojoin: waiting for P1 gameplay; built-in join script, once per session");
#else
  REXLOG_WARN("coop Lua: mailbox currently requires Windows checked guest reads");
#endif
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
