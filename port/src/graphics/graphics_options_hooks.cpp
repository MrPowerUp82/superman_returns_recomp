#include "graphics_options_runtime.h"
#include <rex/hook.h>
#include <rex/input/input.h>
#include <windows.h>
#include <intrin.h>
#include <array>
#include <cstring>
REX_EXTERN(__imp__sub_8214F348);
namespace {
using namespace superman_returns::graphics;
void ObservePause(uint8_t* base) {
  const auto before=CurrentGraphicsGuestPause();
  const auto sample=ReadGraphicsGuestPause([&](uint32_t address)->std::optional<uint32_t> {
    uint32_t value=0; SIZE_T copied=0;
    if(!ReadProcessMemory(GetCurrentProcess(),base+address,&value,4,&copied) || copied!=4) return {};
    return _byteswap_ulong(value);
  });
  ObserveGraphicsGuestPause(sample);
  const auto after=CurrentGraphicsGuestPause();
  if(before.guest_paused!=after.guest_paused || before.session!=after.session)
    REXLOG_INFO("native graphics guest pause: paused={} session={} valid={} context={:08X} state={}",
                after.guest_paused,after.session,sample.valid,sample.context,sample.state);
}
}
REX_HOOK_RAW(sub_8214F348) {
  __imp__sub_8214F348(ctx,base);
  ObservePause(base);
}
