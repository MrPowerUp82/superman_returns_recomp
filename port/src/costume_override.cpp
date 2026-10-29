// --sr_costume: wear a costume without unlocking it.
//
// The costume menu's Apt native SetActiveCostume (handler sub_822B29E0,
// registered by sub_822B9A78) does:
//   world = *(0x8296DF74)                          (sub_821F6588)
//   flow  = FindFlow(world, "SupermenActorWorldFlow")  (sub_821F6850, string 0x8201C0A4)
//   var   = FindVariable(*(*(flow + 104) + 4), "SetCostumeNum")  (sub_826CD770, string 0x8201C0BC)
//   *(var + 4) = costume + 1                       (sub_821E6F90)
// and then sub_822B18E0, which only shows the "now wearing" message box. The
// script consumes the variable within a few frames. Only the variable is written
// here: nothing goes to the save, so the game puts the film suit back whenever it
// recreates Superman. When to write is decided by StepReapply from the suit
// textures the renderer decodes (texture_mods_runtime.h).
#include <rex/cvar.h>
#include <rex/logging.h>

#include <string>

#include "costume_override_policy.h"
#include "generated/default/superman_returns_init.h"
#include "graphics/texture_mods/texture_mods_runtime.h"

REXCVAR_DECLARE(std::string, sr_costume);

namespace superman_returns::costume {
namespace {
constexpr uint32_t kWorldPointer = 0x8296DF74;
constexpr uint32_t kFlowName = 0x8201C0A4;      // "SupermenActorWorldFlow"
constexpr uint32_t kVariableName = 0x8201C0BC;  // "SetCostumeNum"
constexpr int kGoldenAge = 2;

uint32_t Load32(const uint8_t* base, uint32_t address) {
  return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + address));
}
void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  *reinterpret_cast<uint32_t*>(base + address) = __builtin_bswap32(value);
}

bool WriteCostume(PPCContext& ctx, uint8_t* base, int costume) {
  const uint32_t world = Load32(base, kWorldPointer);
  if (!world) return false;
  ctx.r3.u64 = world;
  ctx.r4.u64 = kFlowName;
  sub_821F6850(ctx, base);
  const uint32_t flow = ctx.r3.u32;
  const uint32_t script = flow ? Load32(base, flow + 104) : 0;
  const uint32_t variables = script ? Load32(base, script + 4) : 0;
  if (!variables) return false;
  ctx.r3.u64 = variables;
  ctx.r4.u64 = kVariableName;
  sub_826CD770(ctx, base);
  const uint32_t variable = ctx.r3.u32;
  if (!variable) return false;
  Store32(base, variable + 4, uint32_t(costume + 1));
  return true;
}
}  // namespace

void ApplyForcedCostume(PPCContext& ctx, uint8_t* base) {
  static const auto wanted = ParseCostume(REXCVAR_GET(sr_costume));
  // The film suit is costume 0: nothing to bring back.
  if (!wanted || *wanted == 0) return;
  static ReapplyState state;
  const bool track_worn = *wanted == kGoldenAge;
  if (!StepReapply(state, graphics::texture_mods::FilmSuitDecodes(),
                   track_worn ? graphics::texture_mods::GoldenAgeDecodes() : 0, track_worn))
    return;
  // Guest calls clobber registers the caller of the present hook may still use.
  const PPCContext saved = ctx;
  const bool written = WriteCostume(ctx, base, *wanted);
  ctx = saved;
  REXLOG_INFO("costume: {} costume {} (try {})", written ? "wearing" : "no actor flow for", *wanted,
              state.tries);
}

}  // namespace superman_returns::costume
