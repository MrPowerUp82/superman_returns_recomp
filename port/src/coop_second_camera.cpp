#include "coop_second_camera.h"
#include "coop_lua.h"
#include "coop_camera_input_policy.h"

#include <array>
#include <cstring>
#include <atomic>
#include <bit>
#include <cmath>
#include <mutex>
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
std::mutex snapshot_mutex;
SecondCameraSnapshot snapshot;
uint32_t view_generation=0;
uint32_t last_locomotion=0;  // diagnostics: P2 locomotion type last seen
uint32_t aim_generation=0;  // view_generation whose aim point (+528) is valid
uint64_t session_generation=0;
void InvalidateSnapshot() {
  std::lock_guard lock(snapshot_mutex);
  snapshot={};
}

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
CameraInputBinding InputBinding(uint8_t* base) {
  if (!owner || !target_id || primary_owner != Word(base, kCameraGlobal) ||
      Word(base, owner + 432) != target_id) return {};
  const auto input = Word(base, kInputGlobal);
  const auto slots = input && (Word(base, input + 8) >> 24) > 1 ? Word(base, input + 4) : 0;
  if (!slots || Word(base, slots + 4) != target_id) return {};
  return {primary_owner, owner, Word(base, slots), target_id, session_generation};
}
void RouteCamera(uint8_t* base, PPCRegister& camera, uint32_t port, uint32_t actor) {
  const auto routed = CameraForInput(InputBinding(base), RuntimeCoopState(), camera.u32, port, actor);
  if (routed != camera.u32) {
    camera.u64 = routed;
    static thread_local uint32_t routed_samples = 0;
    if (++routed_samples <= 3)
      REXLOG_INFO("coop camera input routed: port={} actor={} manager={:08X}", port, actor, routed);
  }
}
void RouteLocomotionCamera(PPCRegister& camera, const PPCRegister& component) {
  if (!storage.load(std::memory_order_acquire) || !(RuntimeCoopState() & 1)) return;
  auto* base = rex::system::kernel_state()->memory()->virtual_membase();
  const auto interface = Word(base, component.u32 + 100);
  const auto table = interface ? Word(base, interface) : 0;
  // 821AA4F8 resolves the port using InputManager and interface +4 (actor).
  if (!table || Word(base, table + 108) != 0x821AA4F8) return;
  const auto actor = Word(base, interface + 4);
  if (actor) RouteCamera(base, camera, 1, Word(base, actor + 88));
}
void SelectLocomotionCamera(PPCContext& ctx, uint8_t* base, uint32_t actor) {
  const auto query = Word(base, Word(base, actor) + 36);
  if (!query) return;
  ctx.r3.u64 = actor;
  ctx.r4.u64 = 0x41435452;  // ACTR
  REX_CALL_INDIRECT_FUNC(query);
  const auto brain = ctx.r3.u32 ? Word(base, ctx.r3.u32 + 96) : 0;
  const auto locomotion = brain ? Word(base, brain + 4) : 0;
  const auto get_type = locomotion ? Word(base, Word(base, locomotion) + 4) : 0;
  if (!get_type) return;
  ctx.r3.u64 = locomotion;
  REX_CALL_INDIRECT_FUNC(get_type);
  last_locomotion = ctx.r3.u32;
  // P2's own FSM now drives its manager natively (actor camera scope), including
  // transition modes such as 8. Only correct a plain FLY/LOCO mismatch: with a
  // transition active (+16 != 0) sub_82174758 does not switch but clears the
  // transition counter (+4904), so calling it every frame restarts the blend
  // forever and the camera keeps turning while P2 walks.
  const auto desired = CameraModeForLocomotion(ctx.r3.u32);
  const auto current = Word(base, owner + 4896);
  if (!ShouldForceLocomotionCamera(desired, current, Word(base, owner + 16))) return;
  const auto previous = current;
  ctx.r3.u64 = owner;
  sub_82174758(ctx, base);
  REXLOG_INFO("coop second camera locomotion: target={} previous={} mode={}",
              target_id, previous, Word(base, owner + 4896));
}
void Write(uint8_t* base, uint32_t address, const void* input, size_t count) {
#ifdef _WIN32
  SIZE_T copied = 0;
  if (address && uint64_t(address) + count <= (uint64_t{1} << 32))
    WriteProcessMemory(GetCurrentProcess(), base + address, input, count, &copied);
#endif
}
void WriteWordBE(uint8_t* base, uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  Write(base, address, &value, 4);
}
// The manager update (sub_821808F8) is not run for P2's graph. Reproduce the
// two pieces P2's own gameplay reads through the singleton: the camera
// position copy (+480) and the aim point (+528) from sub_821759B8, the
// camera-ray collision used by powers/targeting/aim animation when no target
// is locked. sub_821759B8's only write outside the manager is the view
// direction at *(0x82970174)+3104; keep P1's value there.
void UpdateSecondaryAim(PPCContext& ctx, uint8_t* base) {
  constexpr uint32_t kViewGlobal = 0x82970174, kViewOffset = 3104;
  const uint32_t view_object = Word(base, kViewGlobal);
  std::array<uint8_t, 16> saved{};
  const bool keep = view_object && Read(base, view_object + kViewOffset, saved.data(), saved.size());
  ctx.r3.u64 = owner;
  ctx.r4.u64 = owner + 480;
  sub_821624D8(ctx, base);
  ctx.r3.u64 = owner;
  sub_821759B8(ctx, base);
  if (keep) Write(base, view_object + kViewOffset, saved.data(), saved.size());
  std::array<uint32_t, 3> aim{};
  bool finite = Read(base, owner + 528, aim.data(), sizeof(aim));
  for (auto word : aim) finite = finite && std::isfinite(std::bit_cast<float>(__builtin_bswap32(word)));
  if (finite && aim_generation != view_generation) {
    aim_generation = view_generation;
    REXLOG_INFO("coop second camera aim ready: target={} aim={},{},{} primary_aim={},{},{}", target_id,
                std::bit_cast<float>(__builtin_bswap32(aim[0])), std::bit_cast<float>(__builtin_bswap32(aim[1])),
                std::bit_cast<float>(__builtin_bswap32(aim[2])), Float(base, primary_owner + 528),
                Float(base, primary_owner + 532), Float(base, primary_owner + 536));
  }
  if (!finite) aim_generation = 0;
}
}  // namespace

// Called around sub_821BE6C8 (one actor's own update, from the entity loop
// sub_821C2468). Returns the manager to expose for that actor, or 0.
uint32_t BeginActorCameraScope(uint8_t* base, uint32_t actor) {
  if (!storage.load(std::memory_order_relaxed) || !owner || !target_id || !actor) return 0;
  // Cheap reject first: this runs for every actor, every entity-loop pass, so
  // read the ID the way the guest itself does (no syscall). `actor` is the
  // pointer the guest is about to dereference in the same call.
  if (actor < 0x10000 || actor > 0xFFFFFFFFu - 92) return 0;
  uint32_t raw = 0;
  std::memcpy(&raw, base + actor + 88, 4);
  const auto id = __builtin_bswap32(raw);
  if (id != target_id) return 0;
  const auto singleton = Word(base, kCameraGlobal);
  const auto routed = CameraForActorUpdate(InputBinding(base), RuntimeCoopState(), singleton, id,
                                           aim_generation && aim_generation == view_generation);
  if (!routed) return 0;
  WriteWordBE(base, kCameraGlobal, routed);
  static uint32_t scopes = 0;
  if (++scopes <= 3 || scopes % 3600 == 0)
    REXLOG_INFO("coop actor camera scope: actor={} manager={:08X} primary={:08X} count={}",
                target_id, routed, singleton, scopes);
  return singleton;
}
void EndActorCameraScope(uint8_t* base, uint32_t primary, uint32_t routed) {
  if (Word(base, kCameraGlobal) == routed) WriteWordBE(base, kCameraGlobal, primary);
  else REXLOG_WARN("coop actor camera scope: singleton changed during P2 update ({:08X}); left as is",
                   Word(base, kCameraGlobal));
}
uint32_t SecondCameraOwner() { return owner; }
void ReadOwner(uint8_t* base, uint32_t address, uint32_t& value) {
  value = storage.load(std::memory_order_relaxed) && target_id ? Word(base, address) : 0;
}

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
  InvalidateSnapshot();
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
  InvalidateSnapshot();
  Restore restore(ctx);
  const auto session=RuntimeCoopState();
  if(session_generation!=(session>>1)) {target_id=0;session_generation=session>>1;}
  if(!(session&1)) {target_id=0;return;}
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
    // SetCameraHomeEntity eagerly resets both flight and on-foot controllers.
    // On-foot reset (8217CEC8 -> 82160D70) copies 288 bytes from its parameter
    // array without a null check. Construct alongside the primary manager's
    // startup parameter loading, and still gate binding on actual availability.
    const uint32_t flight = Word(base, owner + 544 + 1344);
    const uint32_t foot = Word(base, owner + 2032 + 240);
    if (!foot || !flight) {
      if (!waiting_logged) REXLOG_INFO("coop second camera waiting for parameters: foot={:08X} flight={:08X}", foot, flight);
      waiting_logged = true;
      return;
    }
    ctx.r3.u64 = owner;
    ctx.r4.u64 = actor;
    sub_82181590(ctx, base);  // bind native target and initialize controller/view
    target_id = p2_id;
    view_generation=(view_generation+1)&0x3FFFFFFF;
    if(!view_generation) ++view_generation;
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
  SelectLocomotionCamera(ctx, base, actor);
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
  UpdateSecondaryAim(ctx, base);
  std::array<uint32_t, 16> matrix{};
  bool finite = Read(base, block + kManagerSize, matrix.data(), sizeof(matrix));
  for (auto& word : matrix) {
    word = __builtin_bswap32(word);
    finite = finite && std::isfinite(std::bit_cast<float>(word));
  }
  ++sequence;
  std::array<float,16> values{};
  for(size_t i=0;i<values.size();++i) values[i]=std::bit_cast<float>(matrix[i]);
  std::optional<packet::ViewBytes> encoded;
  if(finite && (Word(base,view+20)>>24)==0)
    encoded=packet::EncodeView(values,{Float(base,view+12),Float(base,view+16),
                                     Float(base,view+4),Float(base,view+8)},0xC0000000u|view_generation);
  {
    std::lock_guard lock(snapshot_mutex);
    snapshot={encoded,target_id,primary,sequence,session_generation};
  }
  if (sequence <= 3 || sequence % 120 == 0)
    REXLOG_INFO("coop second camera: seq={} owner={:08X} primary={:08X} target={} primary_target={} singleton_unchanged={} finite={} position={},{},{} primary_position={},{},{} mode={} primary_mode={} p2_locomotion={:08X} forward={},{},{}",
        sequence, owner, primary, target_id, Word(base, primary + 432),
        Word(base, kCameraGlobal) == primary, finite,
        std::bit_cast<float>(matrix[12]), std::bit_cast<float>(matrix[13]), std::bit_cast<float>(matrix[14]),
        Float(base, primary + 480), Float(base, primary + 484), Float(base, primary + 488),
        Word(base, owner + 4896), Word(base, primary + 4896), last_locomotion, values[8], values[9], values[10]);
}
SecondCameraSnapshot SampleSecondCamera() {
  std::lock_guard lock(snapshot_mutex);
  const auto session=RuntimeCoopState();
  if(!(session&1) || snapshot.session_generation!=(session>>1))return {};
  return snapshot;
}
}  // namespace superman_returns::coop

// Contracts/PCs: docs/coop/GROUND-CAMERA-2026-11-09.md. These callbacks change
// only the loaded camera register; the global singleton always remains P1's.
void SrCoopLookCamera(PPCRegister& camera, PPCRegister& port) {
  using namespace superman_returns::coop;
  if (!storage.load(std::memory_order_acquire) || port.u32 != 1) return;
  auto* base = rex::system::kernel_state()->memory()->virtual_membase();
  RouteCamera(base, camera, port.u32, InputBinding(base).secondary_actor);
}
void SrCoopLocomotionCamera(PPCRegister& camera, PPCRegister& component) {
  superman_returns::coop::RouteLocomotionCamera(camera, component);
}
void SrCoopMovementCamera(PPCRegister& camera, PPCRegister& actor, PPCRegister& port) {
  using namespace superman_returns::coop;
  if (!storage.load(std::memory_order_acquire) || port.u32 != 1) return;
  auto* base = rex::system::kernel_state()->memory()->virtual_membase();
  RouteCamera(base, camera, port.u32, Word(base, actor.u32 + 88));
}

// One actor's own update: FSM/Lua (+132), brain/locomotion (+96), powers,
// targeting and aim animation all read the camera through the singleton.
// While P2 updates, the singleton names P2's manager; P1 and every other
// actor keep P1's. Same (simulation) thread as every traced singleton read.
REX_HOOK_RAW(sub_821BE6C8) {
  using namespace superman_returns::coop;
  struct Scope {
    uint8_t* base; uint32_t primary, routed;
    ~Scope() { if (primary) EndActorCameraScope(base, primary, routed); }
  } scope{base, BeginActorCameraScope(base, ctx.r3.u32), SecondCameraOwner()};
  __imp__sub_821BE6C8(ctx, base);
}

// A power component's fire/aim update (heat vision, breaths). It is driven by
// the power-system entity in a later entity-loop phase, outside its owner's
// sub_821BE6C8, and aims at the singleton's aim point (+528) when nothing is
// locked (sub_822172B8, target -1). Its owner actor is at component +32.
REX_HOOK_RAW(sub_8221E748) {
  using namespace superman_returns::coop;
  uint32_t owner_actor = 0;
  superman_returns::coop::ReadOwner(base, ctx.r3.u32 + 32, owner_actor);
  struct Scope {
    uint8_t* base; uint32_t primary, routed;
    ~Scope() { if (primary) EndActorCameraScope(base, primary, routed); }
  } scope{base, BeginActorCameraScope(base, owner_actor), SecondCameraOwner()};
  __imp__sub_8221E748(ctx, base);
}

// Cloth render object update: r3 = object, f1 = render time (0 in the split's
// P2 preparation pass). Cloth position at +28176, grace counter at +28256.
REX_HOOK_RAW(sub_82301510) {
  using namespace superman_returns::coop;
  if (storage.load(std::memory_order_relaxed) && ctx.f1.f64 > 0) {
    const auto snapshot = SampleSecondCamera();
    if (snapshot.view) {
      std::array<uint32_t, 4> raw{};
      if (Read(base, ctx.r3.u32 + 28176, raw.data(), 16)) {
        float cloth[3];
        for (int i = 0; i < 3; ++i) cloth[i] = std::bit_cast<float>(__builtin_bswap32(raw[i]));
        auto camera = [&](int i) { return std::bit_cast<float>(packet::ReadWord(*snapshot.view, 48 + i * 4)); };
        const auto grace = int32_t(Word(base, ctx.r3.u32 + 28256));
        if (ClothNeedsSecondaryGrace(float(ctx.f1.f64), grace, cloth[0] - camera(0),
                                     cloth[1] - camera(1), cloth[2] - camera(2))) {
          WriteWordBE(base, ctx.r3.u32 + 28256, 1);
          static std::atomic<uint32_t> granted{0};
          const auto n = ++granted;
          if (n <= 3 || n % 3600 == 0)
            REXLOG_INFO("coop cloth: P2-camera grace object={:08X} count={}", ctx.r3.u32, n);
        }
      }
    }
  }
  __imp__sub_82301510(ctx, base);
}

REX_HOOK_RAW(sub_821620E8) {
  superman_returns::coop::DestroySecondCamera(ctx, base);
  __imp__sub_821620E8(ctx, base);
}

REX_HOOK_RAW(sub_8218CCD0) {
  __imp__sub_8218CCD0(ctx, base);
  superman_returns::coop::InitializeSecondCamera(ctx, base);
}
