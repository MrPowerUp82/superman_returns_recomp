#include "coop_camera_probe.h"
#include "coop_second_camera.h"
#include "coop_camera_preview.h"
#include "coop_split_render.h"

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/default/superman_returns_init.h"
#include "title_benchmark.h"

#ifdef _WIN32
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(sr_coop_camera_probe, false, "Superman Returns",
                    "Development: observe gameplay camera and view-packet handoff")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DECLARE(bool, sr_coop_input);

namespace superman_returns::coop {
namespace {
std::atomic<uint64_t> presents{0}, updates{0}, submissions{0}, consumes{0}, applies{0}, fallbacks{0};
std::atomic<uint64_t> render_frames{0}, effect_frames{0};
thread_local bool sample_render = false;
thread_local uint32_t sampled_groups = 0, sampled_techniques = 0;

bool Enabled() {
  static const bool enabled = REXCVAR_GET(sr_coop_camera_probe) &&
      REXCVAR_GET(sr_coop_input) && !benchmark::Active();
  return enabled;
}

template <size_t N>
bool ReadWords(uint8_t* base, uint32_t address, std::array<uint32_t, N>& words) {
#ifdef _WIN32
  if (!address || uint64_t(address) + sizeof(words) > (uint64_t{1} << 32)) return false;
  SIZE_T copied = 0;
  if (!ReadProcessMemory(GetCurrentProcess(), base + address, words.data(),
                         sizeof(words), &copied) || copied != sizeof(words)) return false;
  for (auto& word : words) word = __builtin_bswap32(word);
  return true;
#else
  return false;
#endif
}

uint32_t Word(uint8_t* base, uint32_t address) {
  std::array<uint32_t, 1> words{};
  return ReadWords(base, address, words) ? words[0] : 0;
}
float Float(uint32_t word) { return std::bit_cast<float>(word); }

void ObserveCamera(uint8_t* base, uint32_t manager, uint32_t caller, uint64_t sequence) {
  std::array<uint32_t, 12> pose{};
  std::array<uint32_t, 5> mode{};
  const bool readable = ReadWords(base, manager + 448, pose) &&
                        ReadWords(base, manager + 4880, mode);
  REXLOG_INFO("coop camera update: seq={} present={} submissions={} manager={:08X} caller={:08X} readable={} target={} active={:08X} mode={} pose={:08X},{:08X},{:08X},{:08X},{:08X},{:08X},{:08X},{:08X} position={},{},{}",
      sequence, presents.load(), submissions.load(), manager, caller, readable,
      Word(base, manager + 432), mode[0], mode[4], pose[0], pose[1], pose[2],
      pose[3], pose[4], pose[5], pose[6], pose[7], Float(pose[8]), Float(pose[9]), Float(pose[10]));
}

void ObservePacket(uint8_t* base, uint32_t owner, uint32_t packet, uint32_t caller,
                   uint64_t sequence, const char* stage) {
  std::array<uint32_t, 70> data{};
  const bool readable = ReadWords(base, packet, data);
  const uint32_t target = readable ? Word(base, data[0] + 12) : 0;
  bool finite = readable;
  for (size_t i = 4; i < 24; ++i) finite = finite && std::isfinite(Float(data[i]));
  REXLOG_INFO("coop camera packet: seq={} present={} updates={} owner={:08X} packet={:08X} caller={:08X} readable={} dispatch={:08X} finite={} transform={},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{} projection={},{},{},{} viewport={:08X},{:08X} flags={:08X} stage={} secondary_position={},{},{} secondary_projection={},{},{},{} secondary_viewport={:08X},{:08X} secondary_flags={:08X}",
      sequence, presents.load(), updates.load(), owner, packet, caller, readable,
      target, finite, Float(data[4]), Float(data[5]), Float(data[6]), Float(data[7]),
      Float(data[8]), Float(data[9]), Float(data[10]), Float(data[11]),
      Float(data[12]), Float(data[13]), Float(data[14]), Float(data[15]),
      Float(data[16]), Float(data[17]), Float(data[18]), Float(data[19]),
      Float(data[20]), Float(data[21]), Float(data[22]), Float(data[23]),
      data[24], data[25], data[26], stage, Float(data[40]), Float(data[41]), Float(data[42]),
      Float(data[44]), Float(data[45]), Float(data[46]), Float(data[47]),
      data[48], data[49], data[50]);
}
}  // namespace

void NoteCameraProbePresent() {
  if (Enabled()) presents.fetch_add(1, std::memory_order_relaxed);
}
void NoteCameraProbeGroup(uint32_t group, uint32_t subgroup, uint32_t caller) {
  if (!sample_render || !Enabled()) return;
  ++sampled_groups;
  if (sampled_groups <= 96)
    REXLOG_INFO("coop camera render group: present={} ordinal={} group={} subgroup={} caller={:08X}",
        presents.load(), sampled_groups, group, subgroup, caller);
}
}  // namespace superman_returns::coop

REX_HOOK_RAW(sub_821808F8) {
  using namespace superman_returns::coop;
  const uint32_t manager = ctx.r3.u32, caller = uint32_t(ctx.lr);
  const double dt = ctx.f1.f64;
  __imp__sub_821808F8(ctx, base);
  UpdateSecondCamera(ctx, base, manager, dt);
  if (!Enabled()) return;
  const auto sequence = updates.fetch_add(1) + 1;
  if (sequence <= 3 || sequence % 120 == 0) ObserveCamera(base, manager, caller, sequence);
}

REX_HOOK_RAW(sub_82176698) {
  using namespace superman_returns::coop;
  if (!Enabled()) { __imp__sub_82176698(ctx, base); return; }
  const uint32_t owner = ctx.r3.u32, caller = uint32_t(ctx.lr);
  const uint32_t packet = Word(base, owner + 16);
  __imp__sub_82176698(ctx, base);
  const auto sequence = submissions.fetch_add(1) + 1;
  if (sequence <= 3 || sequence % 120 == 0) ObservePacket(base, owner, packet, caller, sequence, "submit");
}

REX_HOOK_RAW(sub_82777150) {
  using namespace superman_returns::coop;
  if (!Enabled()) { __imp__sub_82777150(ctx, base); return; }
  const uint32_t owner = ctx.r3.u32, caller = uint32_t(ctx.lr);
  __imp__sub_82777150(ctx, base);
  const auto sequence = consumes.fetch_add(1) + 1;
  if (sequence <= 3 || sequence % 120 == 0)
    ObservePacket(base, owner, owner + 96, caller, sequence, "consume");
}

REX_HOOK_RAW(sub_82775AA0) {
  using namespace superman_returns::coop;
  const uint32_t graphics = ctx.r3.u32, index = ctx.r4.u32, caller = uint32_t(ctx.lr);
  __imp__sub_82775AA0(ctx, base);
  const auto sequence = Enabled() ? applies.fetch_add(1) + 1 : 0;
  if (sequence && (sequence <= 3 || sequence % 120 == 0)) {
    const uint32_t list = Word(base, Word(base, graphics + 96));
    const uint32_t camera = index < 2 && list ? Word(base, list + index * 4) : 0;
    std::array<uint32_t, 16> transform{};
    const bool readable = camera && ReadWords(base, camera + 112, transform);
    REXLOG_INFO("coop camera apply: seq={} present={} updates={} submissions={} consumes={} fallbacks={} graphics={:08X} view={} camera={:08X} caller={:08X} readable={} position={},{},{}",
        sequence, presents.load(), updates.load(), submissions.load(), consumes.load(),
        fallbacks.load(), graphics, index, camera, caller, readable,
        Float(transform[12]), Float(transform[13]), Float(transform[14]));
  }
  if(!PrepareSplitCamera(ctx,base,graphics,index,caller))
    ApplyP2CameraPreview(ctx,base,graphics,index,caller);
}

REX_HOOK_RAW(sub_827758F8) {
  using namespace superman_returns::coop;
  if (!Enabled()) { __imp__sub_827758F8(ctx, base); return; }
  const auto sequence = fallbacks.fetch_add(1) + 1;
  if (sequence <= 3 || sequence % 120 == 0)
    REXLOG_INFO("coop camera fallback groups: seq={} present={} updates={} submissions={} consumes={} applies={} caller={:08X}",
        sequence, presents.load(), updates.load(), submissions.load(), consumes.load(),
        applies.load(), uint32_t(ctx.lr));
  __imp__sub_827758F8(ctx, base);
}

REX_HOOK_RAW(sub_82776038) {
  using namespace superman_returns::coop;
  const uint32_t split_graphics=ctx.r3.u32;
  const bool split=BeginSplitWorld(base,split_graphics);
  ObserveP2CameraDraw(base,ctx.r3.u32);
  if (!Enabled()) {
    __imp__sub_82776038(ctx, base);
    if(split)EndSplitWorld(ctx,base,split_graphics);
    return;
  }
  const uint32_t graphics = ctx.r3.u32;
  const auto sequence = render_frames.fetch_add(1) + 1;
  sample_render = sequence <= 3 || sequence % 120 == 0;
  sampled_groups = sampled_techniques = 0;
  const bool sample = sample_render;
  __imp__sub_82776038(ctx, base);
  if(split)EndSplitWorld(ctx,base,split_graphics);
  sample_render = false;
  if (sample) {
    std::array<uint32_t, 2> state{};
    const bool readable = ReadWords(base, graphics + 100, state);
    REXLOG_INFO("coop camera render frame: seq={} present={} updates={} submissions={} consumes={} applies={} effect_frames={} fallbacks={} graphics={:08X} readable={} route={:08X} groups={} techniques={}",
        sequence, presents.load(), updates.load(), submissions.load(), consumes.load(),
        applies.load(), effect_frames.load(), fallbacks.load(), graphics, readable,
        state[0], sampled_groups, sampled_techniques);
  }
}

REX_HOOK_RAW(sub_82776EB0) {
  using namespace superman_returns::coop;
  if (!Enabled()) { __imp__sub_82776EB0(ctx, base); return; }
  const uint32_t owner = ctx.r3.u32;
  effect_frames.fetch_add(1);
  if (sample_render) {
    std::array<uint32_t, 6> assets{};
    const bool readable = ReadWords(base, owner + 80, assets);
    REXLOG_INFO("coop camera effects: present={} owner={:08X} readable={} assets={:08X},{:08X},{:08X} count={} order={},{},{} mode={}",
        presents.load(), owner, readable, assets[0], assets[1], assets[2],
        Word(base, owner + 316), Word(base, owner + 304), Word(base, owner + 308),
        Word(base, owner + 312), Word(base, owner + 336));
  }
  __imp__sub_82776EB0(ctx, base);
}

REX_HOOK_RAW(sub_8277B998) {
  using namespace superman_returns::coop;
  if (!Enabled() || !sample_render) { __imp__sub_8277B998(ctx, base); return; }
  const uint32_t effect = ctx.r3.u32, technique = ctx.r4.u32, caller = uint32_t(ctx.lr);
  ++sampled_techniques;
  if (sampled_techniques <= 16)
    REXLOG_INFO("coop camera technique: present={} ordinal={} effect={:08X} technique={} caller={:08X}",
        presents.load(), sampled_techniques, effect, technique, caller);
  __imp__sub_8277B998(ctx, base);
}
