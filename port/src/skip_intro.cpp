// Skip the boot logo/legal videos (EA_HD_logo, EA_ID_OLD, DC_logo, WBIE_logo,
// DC_WB_legal) when sr_skip_intro is set.
//
// sub_822A8BC0 starts the sequence: unless a global "sequence disabled" flag
// is set, it prepares the player (sub_822B72F0), begins the sequence
// (sub_82470628) and plays table[this+560] via sub_822B7F38. sub_822A8C68
// advances on each video end and, after the last one, marks the sequence done
// (this+560 = -1, this+564 = 1) and ends it with sub_82470700. With the option
// on we run the same setup and go straight to that "done" state.

#include <rex/hook.h>

#include "generated/default/superman_returns_init.h"
#include "sr_settings.h"

namespace {

uint32_t Load32(const uint8_t* base, uint32_t addr) {
  return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + addr));
}

void Store32(uint8_t* base, uint32_t addr, uint32_t value) {
  *reinterpret_cast<uint32_t*>(base + addr) = __builtin_bswap32(value);
}

}  // namespace

REX_HOOK_RAW(sub_822A8BC0) {
  // Mirrors the original early-out: [[0x829761E0] + 4] != 0 disables the sequence.
  const bool sequence_disabled = Load32(base, Load32(base, 0x829761E0) + 4) != 0;
  if (!REXCVAR_GET(sr_skip_intro) || sequence_disabled) {
    __imp__sub_822A8BC0(ctx, base);
    return;
  }

  const uint32_t self = ctx.r3.u32;
  sub_822B72F0(ctx, base);  // this = r3
  base[self + 553] = 0;
  sub_82470628(ctx, base);  // begin sequence
  Store32(base, self + 560, 0xFFFFFFFF);
  base[self + 564] = 1;
  sub_82470700(ctx, base);  // end sequence
}
