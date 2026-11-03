#include "coop_lua.h"

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
  const auto now = Clock::now();
  if (now < next_poll) return;
  next_poll = now + std::chrono::milliseconds(250);
  std::error_code error;
  const auto stamp = std::filesystem::last_write_time(script_path, error);
  if (error) return;
  static bool has_stamp = false;
  static std::filesystem::file_time_type last_stamp;
  if (has_stamp && stamp == last_stamp) return;
  std::ifstream input(script_path, std::ios::binary);
  std::string script(kScriptCapacity, '\0');
  input.read(script.data(), script.size());
  const auto count = input.gcount();
  if (!input || count == 0 || count >= kScriptCapacity) {
    // EOF is expected for a short file; fail only on bad I/O or invalid size.
    if (input.bad() || count == 0 || count >= kScriptCapacity) {
      REXLOG_WARN("coop Lua: script must contain 1..65535 bytes");
      return;
    }
  }
  script.resize(size_t(count));
  // PowerShell 5 writes a UTF-8 BOM; Lua 5.0's buffer loader doesn't skip it.
  if (script.starts_with("\xEF\xBB\xBF")) script.erase(0, 3);
  if (script.empty()) return;

  // Script contexts are ScriptEntity interfaces, not actor objects. Spawn's
  // world-flow lookup requires the real actor script context. Wait for it
  // instead of putting an Entity pointer into the ScriptEntity context stack.
  if (script.starts_with("-- sr-coop-context: player1")) {
    const PPCContext saved = ctx;
    ctx.r3.u64 = lua;
    ctx.r4.u64 = 100001;  // missing argument -> resolve the current script actor
    sub_82262B38(ctx, base);
    const uint32_t current_actor = ctx.r3.u32;
    ctx = saved;
    uint32_t manager = 0, slots = 0, p1_id = 0;
    if (!current_actor || !ReadWord(base, kInputManager, manager) || !manager ||
        !ReadWord(base, manager + 4, slots) || !slots ||
        !ReadWord(base, slots, p1_id) || p1_id == 255) return;
    ctx.r3.u64 = p1_id;
    sub_821C23E0(ctx, base);
    const uint32_t p1 = ctx.r3.u32;
    ctx = saved;
    if (!p1 || p1 != current_actor) return;
  }
  last_stamp = stamp;
  has_stamp = true;

  const PPCContext saved = ctx;
  struct RestoreContext {
    PPCContext& target;
    const PPCContext& saved;
    ~RestoreContext() { target = saved; running_mailbox = false; }
  } restore{ctx, saved};
  running_mailbox = true;
  REXLOG_INFO("coop Lua: executing {} bytes context={:08X}", script.size(), entity);

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
  REXLOG_INFO("coop Lua: status={} context={:08X} result={}", status, entity, message);
}
}  // namespace

void InstallLuaBridge(rex::Runtime& runtime) {
  const std::string path = REXCVAR_GET(sr_coop_script);
  if (path.empty()) return;
#ifdef _WIN32
  script_path = path;
  const uint32_t buffer = runtime.memory()->SystemHeapAlloc(kScriptCapacity + 64);
  if (!buffer) {
    REXLOG_ERROR("coop Lua: could not allocate the script mailbox");
    return;
  }
  script_buffer.store(buffer, std::memory_order_release);
  REXLOG_INFO("coop Lua: local development mailbox enabled");
#else
  REXLOG_WARN("coop Lua: mailbox currently requires Windows checked guest reads");
#endif
}

}  // namespace superman_returns::coop

// Development-only extension: SetActorToDefaultController(actor, port).
// Calls made by the guest itself, or outside the injected mailbox, keep the
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
      !ReadWord(base, slots + port * 4, occupant) ||
      (occupant != 255 && occupant != actor_id)) {
    REXLOG_WARN("coop Lua: refused actor binding to occupied/invalid port {}", port);
    ReturnLuaBool(ctx, base, false);
    return;
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
