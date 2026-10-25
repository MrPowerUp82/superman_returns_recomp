#include "graphics_options_runtime.h"
#include <rex/hook.h>
#include <rex/input/input.h>
#include <windows.h>
#include <intrin.h>
#include <array>
#include <cstring>
REX_EXTERN(__imp__sub_8214F348);
REX_EXTERN(__imp__sub_828A7A38);
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
struct PadPackets {
  std::mutex mutex;
  struct User {bool initialized=false,filtered=false;uint32_t original=0,serial=0;};
  std::array<User,4> users{};
  uint32_t Next(uint32_t user,uint32_t raw,bool filtered) {
    std::lock_guard lock(mutex);
    if(user>=users.size()) return raw;
    auto& s=users[user];
    if(!s.initialized) {s.initialized=true;s.original=s.serial=raw;}
    if(raw!=s.original || filtered!=s.filtered) ++s.serial;
    s.original=raw;s.filtered=filtered;
    return s.serial;
  }
} packets;
}
REX_HOOK_RAW(sub_8214F348) {
  __imp__sub_8214F348(ctx,base);
  ObservePause(base);
}
REX_HOOK_RAW(sub_828A7A38) {
  const uint32_t user=ctx.r3.u32, output=ctx.r4.u32;
  __imp__sub_828A7A38(ctx,base);
  if(!superman_returns::graphics::CurrentGraphicsGuestPause().guest_paused &&
     !superman_returns::graphics::SharedGraphicsOptionsMenu()->CaptureGuestInput()) return;
  ObservePause(base);
  auto menu=superman_returns::graphics::SharedGraphicsOptionsMenu();
  rex::input::X_INPUT_STATE state{};
  SIZE_T copied=0;
  const bool valid=ctx.r3.u32==0 && output &&
      ReadProcessMemory(GetCurrentProcess(),base+output,&state,sizeof(state),&copied) && copied==sizeof(state);
  if(user==0) {
    superman_returns::graphics::GraphicsGamepadState pad{};
    if(valid) pad={state.gamepad.buttons,state.gamepad.thumb_lx,state.gamepad.thumb_ly,
                  state.gamepad.thumb_rx,state.gamepad.thumb_ry,state.gamepad.left_trigger,state.gamepad.right_trigger};
    const bool was_open=menu->Snapshot().open;
    superman_returns::graphics::ObserveGraphicsGamepad(pad);
    if(!was_open && menu->Snapshot().open)
      REXLOG_INFO("native graphics entry: source=gamepad buttons={:04X}",pad.buttons);
  }
  if(valid) {
    const bool capture=menu->CaptureGuestInput();
    const uint32_t original_packet=state.packet_number;
    state.packet_number=packets.Next(user,state.packet_number,capture);
    if(capture) state.gamepad={};
    SIZE_T written=0;
    if(capture || uint32_t(state.packet_number)!=original_packet)
      WriteProcessMemory(GetCurrentProcess(),base+output,&state,sizeof(state),&written);
  }
}
