// Internal render resolution (sr_render_scale, percent of 1280x720).
//
// Device setup (sub_822EB148) fills its D3D present parameters with
// sub_82611A20(params, 4); mode 4 is the 1280x720 preset, written to the
// back buffer (+0 width, +4 height) and front buffer (+12, +16) fields. The
// Shrinking these fields alone is NOT sufficient: the 2026-09-30 title
// smoke test at 75% still resolves 1280 pixels from a pitch of 960 and shows
// black regions and stale pixels. Keep this experimental and out of presets
// until intermediate textures, composition and HUD have been validated.

#include <algorithm>
#include <array>
#include <mutex>

#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/default/superman_returns_init.h"
#include "sr_settings.h"

namespace {

uint32_t Load32(const uint8_t* base, uint32_t addr) {
  return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + addr));
}

void Store32(uint8_t* base, uint32_t addr, uint32_t value) {
  *reinterpret_cast<uint32_t*>(base + addr) = __builtin_bswap32(value);
}

// Keep sizes multiples of 8: EDRAM tiles and resolves work in 8-pixel units.
uint32_t Scale(uint32_t size, int32_t percent) {
  return std::max<uint32_t>(64, (size * percent / 100 + 7) & ~7u);
}

}  // namespace

// Mid-asm hook at 0x822EB1C8, right after device setup (sub_822EB148) stores
// the engine's own render size (this+60 = 1280, this+64 = 720; this+68/72
// hold the real video mode). The engine sizes its EDRAM render targets from
// these fields, not from the back buffer.
void SrScaleEngineRenderSize(PPCRegister& r31) {
  const int32_t percent = std::clamp(REXCVAR_GET(sr_render_scale), 25, 100);
  if (percent == 100) return;
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const uint32_t device = r31.u32;
  // +60/+64: render size; +68/+72: display mode, which sizes the final
  // resolve. Both must shrink or the resolve reads past the smaller surface.
  for (uint32_t offset : {60u, 68u}) {
    const uint32_t width = Scale(Load32(base, device + offset), percent);
    const uint32_t height = Scale(Load32(base, device + offset + 4), percent);
    Store32(base, device + offset, width);
    Store32(base, device + offset + 4, height);
    REXLOG_INFO("render scale {}%: device +{} size {}x{}", percent, offset, width, height);
  }
}

REX_HOOK_RAW(sub_82611A20) {
  const uint32_t params = ctx.r3.u32;
  const uint32_t mode = ctx.r4.u32;
  __imp__sub_82611A20(ctx, base);

  const int32_t percent = std::clamp(REXCVAR_GET(sr_render_scale), 25, 100);
  if (percent == 100 || mode != 4) return;
  const uint32_t width = Scale(Load32(base, params + 0), percent);
  const uint32_t height = Scale(Load32(base, params + 4), percent);
  Store32(base, params + 0, width);
  Store32(base, params + 4, height);
  Store32(base, params + 12, width);
  Store32(base, params + 16, height);
  REXLOG_INFO("render scale {}%: back buffer {}x{}", percent, width, height);
}

// Bounded diagnostic at the XDK resolve entry. r5 is the optional source
// rectangle; r4's low three bits select the source render target (4 = depth).
// These arguments are read by sub_8210C5F8 before it emits the copy commands.
// Recording the caller and extent exposes remaining unscaled engine passes.
REX_HOOK_RAW(sub_8210C5F8) {
  // RequiresRestart: read once, so normal runs don't query a cvar per copy.
  static const bool diagnose = REXCVAR_GET(sr_render_scale) != 100;
  if (diagnose && ctx.r6.u32) {
    // The resolve itself decodes its destination extent from texture +36:
    // low 13 bits are width-1; the following 13 bits are height-1 (2D).
    // Record the raw word as well, rather than assuming every texture is 2D.
    const uint32_t size_word = Load32(base, ctx.r6.u32 + 36);
    const uint64_t key = (uint64_t(uint32_t(ctx.lr)) << 32) | size_word;
    static std::mutex mutex;
    static std::array<uint64_t, 24> seen{};
    static size_t used = 0;
    std::lock_guard lock(mutex);
    if (used < seen.size() && std::find(seen.begin(), seen.begin() + used, key) == seen.begin() + used) {
      seen[used++] = key;
      const uint32_t rect = ctx.r5.u32;
      REXLOG_INFO("render scale resolve: caller {:08X}, flags {:08X}, destination {:08X}, "
                  "size_word {:08X}, rect {} {} {} {}, explicit={}", uint32_t(ctx.lr),
                  ctx.r4.u32, ctx.r6.u32, size_word,
                  rect ? Load32(base, rect) : 0, rect ? Load32(base, rect + 4) : 0,
                  rect ? Load32(base, rect + 8) : 0,
                  rect ? Load32(base, rect + 12) : 0, rect != 0);
    }
  }
  __imp__sub_8210C5F8(ctx, base);
}
