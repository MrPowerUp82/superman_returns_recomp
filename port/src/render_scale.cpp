// Internal render resolution (sr_render_scale, percent of 1280x720).
//
// Device setup (sub_822EB148) fills its D3D present parameters with
// sub_82611A20(params, 4); mode 4 is the 1280x720 preset, written to the
// back buffer (+0 width, +4 height) and front buffer (+12, +16) fields. The
// engine later queries the back buffer size from the device instead of
// hardcoding 720p (sub_8214E2C8 only falls back to 1280x720), so shrinking
// these fields makes the whole frame render at the lower resolution and the
// presenter scales the smaller front buffer up to the window.

#include <algorithm>

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
