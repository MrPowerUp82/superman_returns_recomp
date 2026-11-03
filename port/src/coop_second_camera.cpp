#include "coop_second_camera.h"

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include "generated/default/superman_returns_init.h"
#include "title_benchmark.h"
#ifdef _WIN32
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(sr_coop_second_camera, false, "Superman Returns",
                    "Development: update a separate native P2 camera controller; no second view")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DECLARE(bool, sr_coop_input);

namespace superman_returns::coop {
namespace {
constexpr uint32_t kCameraGlobal = 0x8296D86C, kInputGlobal = 0x8296E8C8;
constexpr uint32_t kManagerSize = 5024;
std::atomic<uint32_t> storage{0};
uint32_t owner = 0, primary_owner = 0, target_id = 0;
uint64_t sequence = 0;
bool waiting_logged = false;

struct Restore {
  PPCContext& ctx;
  PPCContext saved;
  explicit Restore(PPCContext& context) : ctx(context), saved(context) {}
  ~Restore() { ctx = saved; }
};
bool Read(uint8_t* base, uint32_t address, void* output, size_t count) {
#ifdef _WIN32
  SIZE_T copied = 0;
  return address && uint64_t(address) + count <= (uint64_t{1} << 32) &&
      ReadProcessMemory(GetCurrentProcess(), base + address, output, count, &copied) && copied == count;
#else
  return false;
#endif
}
uint32_t Word(uint8_t* base, uint32_t address) {
  uint32_t result = 0;
  return Read(base, address, &result, 4) ? __builtin_bswap32(result) : 0;
}
float Float(uint8_t* base, uint32_t address) { return std::bit_cast<float>(Word(base, address)); }
uint32_t Actor(PPCContext& ctx, uint8_t* base, uint32_t id) {
  ctx.r3.u64 = id;
  sub_821C23E0(ctx, base);
  return ctx.r3.u32;
}
}  // namespace

void InstallSecondCamera(rex::Runtime& runtime) {
  if (!REXCVAR_GET(sr_coop_second_camera) || !REXCVAR_GET(sr_coop_input) || benchmark::Active()) return;
#ifdef _WIN32
  // Reserve outer storage once. The native destructor frees its own nested
  // allocations; this SystemHeap block is reused after explicit destruction.
  const auto address = runtime.memory()->SystemHeapAlloc(kManagerSize + 64);
  if (!address) { REXLOG_ERROR("coop second camera: outer allocation failed"); return; }
  storage.store(address, std::memory_order_release);
  REXLOG_INFO("coop second camera enabled: storage={:08X}; controller-only development mode", address);
#endif
}

void DestroySecondCamera(PPCContext& ctx, uint8_t* base) {
  if (!owner) return;
  Restore restore(ctx);
  const uint32_t previous = owner, previous_target = target_id;
  owner = primary_owner = target_id = 0;
  waiting_logged = false;
  ctx.r3.u64 = previous;
  ctx.r4.u64 = 0;  // destroy nested allocations/listener, retain SystemHeap outer storage
  sub_82197E08(ctx, base);
  REXLOG_INFO("coop second camera destroyed: owner={:08X} target={}", previous, previous_target);
}

void InitializeSecondCamera(PPCContext& ctx, uint8_t* base) {
  const uint32_t block = storage.load(std::memory_order_acquire);
  const uint32_t primary = Word(base, kCameraGlobal);
  if (!block || !primary || owner) return;
  Restore restore(ctx);
  ctx.r3.u64 = block;
  sub_8218C4C0(ctx, base);
  owner = block;
  primary_owner = primary;
  REXLOG_INFO("coop second camera constructed at camera initialization: owner={:08X} primary={:08X}", owner, primary);
}

void UpdateSecondCamera(PPCContext& ctx, uint8_t* base, uint32_t primary, double dt) {
  const uint32_t block = storage.load(std::memory_order_acquire);
  if (!block) return;
  Restore restore(ctx);
  const uint32_t input = Word(base, kInputGlobal);
  const uint32_t slots = input ? Word(base, input + 4) : 0;
  const uint32_t count = input ? Word(base, input + 8) >> 24 : 0;
  const uint32_t p2_id = count > 1 && slots ? Word(base, slots + 4) : 0;
  const uint32_t p1_id = slots && count ? Word(base, slots) : 0;
  const uint32_t actor = p2_id && p2_id != 255 && p2_id != p1_id ? Actor(ctx, base, p2_id) : 0;
  if (!primary || Word(base, kCameraGlobal) != primary || (owner && primary_owner != primary)) {
    DestroySecondCamera(ctx, base);
    return;
  }
  if (!owner) return;
  if (!actor) {
    if (target_id) REXLOG_INFO("coop second camera suspended: owner={:08X} removed_target={}", owner, target_id);
    // The controller graph owns its startup-loaded parameters. Retain it but
    // never update a removed actor. SetHome below replaces every target on join.
    // The native manager's event handler is disabled (constructor flag +517=0).
    target_id = 0;
    return;
  }
  if (target_id != p2_id) {
    // SetCameraHomeEntity eagerly resets both on-foot and flight controllers.
    // Flight reset (8217CEC8 -> 82160D70) copies 288 bytes from its parameter
    // array without a null check. Construct alongside the primary manager's
    // startup parameter loading, and still gate binding on actual availability.
    const uint32_t foot = Word(base, owner + 544 + 1344);
    const uint32_t flight = Word(base, owner + 2032 + 240);
    if (!foot || !flight) {
      if (!waiting_logged) REXLOG_INFO("coop second camera waiting for parameters: foot={:08X} flight={:08X}", foot, flight);
      waiting_logged = true;
      return;
    }
    ctx.r3.u64 = owner;
    ctx.r4.u64 = actor;
    sub_82181590(ctx, base);  // bind native target and initialize controller/view
    target_id = p2_id;
    // Joining can leave P2 selected as the primary camera home. Give that home
    // back to port 0 at join, using the
    // native setter; leave other scripted/cutscene targets alone.
    if (Word(base, primary + 432) == p2_id && p1_id && p1_id != 255) {
      const uint32_t p1 = Actor(ctx, base, p1_id);
      if (p1) {
        ctx.r3.u64 = primary;
        ctx.r4.u64 = p1;
        sub_82181590(ctx, base);
        REXLOG_INFO("coop second camera restored primary home: target={}", p1_id);
      }
    }
    REXLOG_INFO("coop second camera created: owner={:08X} primary={:08X} target={}", owner, primary, target_id);
  }
  if (!std::isfinite(dt) || dt <= 0 || dt > 0.25) return;
  const uint32_t controller = Word(base, owner + 4880);
  const uint32_t update = controller ? Word(base, Word(base, controller) + 16) : 0;
  const uint32_t view = Word(base, owner + 40);
  const uint32_t transform = view ? Word(base, Word(base, view) + 44) : 0;
  if (!update || !transform) return;
  ctx.r3.u64 = controller;
  ctx.f1.f64 = dt;
  REX_CALL_INDIRECT_FUNC(update);
  ctx.r3.u64 = view;
  ctx.r4.u64 = block + kManagerSize;
  REX_CALL_INDIRECT_FUNC(transform);
  std::array<uint32_t, 16> matrix{};
  bool finite = Read(base, block + kManagerSize, matrix.data(), sizeof(matrix));
  for (auto& word : matrix) {
    word = __builtin_bswap32(word);
    finite = finite && std::isfinite(std::bit_cast<float>(word));
  }
  ++sequence;
  if (sequence <= 3 || sequence % 120 == 0)
    REXLOG_INFO("coop second camera: seq={} owner={:08X} primary={:08X} target={} primary_target={} singleton_unchanged={} finite={} position={},{},{} primary_position={},{},{}",
        sequence, owner, primary, target_id, Word(base, primary + 432),
        Word(base, kCameraGlobal) == primary, finite,
        std::bit_cast<float>(matrix[12]), std::bit_cast<float>(matrix[13]), std::bit_cast<float>(matrix[14]),
        Float(base, primary + 480), Float(base, primary + 484), Float(base, primary + 488));
}
}  // namespace superman_returns::coop

REX_HOOK_RAW(sub_821620E8) {
  superman_returns::coop::DestroySecondCamera(ctx, base);
  __imp__sub_821620E8(ctx, base);
}

REX_HOOK_RAW(sub_8218CCD0) {
  __imp__sub_8218CCD0(ctx, base);
  superman_returns::coop::InitializeSecondCamera(ctx, base);
}
