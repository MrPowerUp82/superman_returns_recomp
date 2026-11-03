#include "coop_camera_preview.h"
#include "coop_camera_packet.h"
#include <array>
#include <cstring>
#include <rex/cvar.h>
#include <rex/logging.h>
#include "generated/default/superman_returns_init.h"
#include "title_benchmark.h"
#ifdef _WIN32
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(sr_coop_camera_preview,false,"Superman Returns",
                    "Development: draw the transported P2 camera in the existing world viewport")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DECLARE(bool,sr_coop_input);
REXCVAR_DECLARE(bool,sr_coop_camera_transport);
REXCVAR_DECLARE(bool,sr_coop_second_camera);

namespace superman_returns::coop {
namespace {
bool Enabled() {
  static const bool enabled=REXCVAR_GET(sr_coop_camera_preview) && REXCVAR_GET(sr_coop_input) &&
      REXCVAR_GET(sr_coop_camera_transport) && REXCVAR_GET(sr_coop_second_camera) && !benchmark::Active();
  return enabled;
}
bool Read(uint8_t* base,uint32_t address,void* out,size_t length) {
#ifdef _WIN32
  SIZE_T copied=0;
  return address && uint64_t(address)+length<=0x100000000ull &&
      ReadProcessMemory(GetCurrentProcess(),base+address,out,length,&copied) && copied==length;
#else
  return false;
#endif
}
uint32_t Word(uint8_t* base,uint32_t address) {
  std::array<uint8_t,4> bytes{};
  return Read(base,address,bytes.data(),4) ? packet::ReadWord(bytes,0) : 0;
}
struct Restore {
  PPCContext& ctx;
  PPCContext saved;
  ~Restore() {ctx=saved;}
};
struct PreviewFrame {
  uint32_t graphics=0,camera=0,hud=0,packet=0,generation=0;
  uint64_t sequence=0;
  bool sample=false;
  uint32_t source_view=1;
};
thread_local PreviewFrame frame;
CameraSelection CurrentSelection(uint8_t* base,uint32_t graphics) {
  const uint32_t list=Word(base,Word(base,graphics+96));
  return {list ? Word(base,list) : 0,list ? Word(base,list+4) : 0};
}
}

void ApplyP2CameraPreview(PPCContext& ctx,uint8_t* base,uint32_t graphics,
                          uint32_t view,uint32_t caller) {
  // This is the normal preparation caller. Never apply to the HUD camera or
  // mutate either consumed packet block to select a different source view.
  if(!Enabled() || view!=0 || caller!=0x82776000) return;
  frame={};
  const uint32_t consumer=Word(base,0x829A9398);
  const uint32_t address=consumer ? consumer+96 : 0;
  std::array<uint8_t,packet::kPacketSize> before{},after{};
  if(!Read(base,address,before.data(),before.size())) return;
  const auto selected=packet::DecodeOwnedSecondary(before);
  const auto selection=CurrentSelection(base,graphics);
  static bool active=false;
  if(!selected) {
    if(active && packet::ReadWord(before,200)==0)
      frame={graphics,selection.world,selection.hud,address,0,0,true,0};
    active=false;
    if(packet::ReadWord(before,200)) REXLOG_WARN("coop camera preview refused: invalid secondary view");
    return;
  }
  const uint32_t camera=selection.world,hud=selection.hud;
  std::array<uint8_t,64> hud_before{},hud_after{},world_after{};
  if(!camera || !hud || camera==hud || !Read(base,hud+112,hud_before.data(),64)) {
    REXLOG_WARN("coop camera preview refused: missing distinct world/HUD cameras"); return;
  }
  Restore restore{ctx,ctx};
  // Match sub_82775AA0's native setter order, targeting camera 0 throughout.
  auto scalar=[&](auto function,float value) {
    ctx.r3.u64=camera;ctx.f1.f64=value;function(ctx,base);
  };
  scalar(sub_827858F0,selected->projection[2]);
  scalar(sub_82785960,selected->projection[3]);
  scalar(sub_82785758,selected->projection[0]);
  scalar(sub_82785768,selected->projection[1]);
  ctx.r3.u64=camera;ctx.r4.u64=0;ctx.r5.u64=0;ctx.r6.u64=1280;ctx.r7.u64=720;
  sub_82785778(ctx,base);
  ctx.r3.u64=camera;ctx.r4.u64=address+packet::kSecondaryOffset;
  sub_82786208(ctx,base);
  ctx.r3.u64=camera;
  sub_827863D0(ctx,base);
  const bool packet_unchanged=Read(base,address,after.data(),after.size()) && before==after;
  const bool hud_unchanged=Read(base,hud+112,hud_after.data(),64) && hud_before==hud_after;
  const bool matrix_matches=Read(base,camera+112,world_after.data(),64) &&
      PreviewMatrixMatches(selection,CurrentSelection(base,graphics),
          std::span(before).subspan(packet::kSecondaryOffset,64),world_after);
  if(!packet_unchanged || !hud_unchanged || !matrix_matches) {
    REXLOG_ERROR("coop camera preview failed: packet_unchanged={} hud_unchanged={} matrix_matches={}",
                 packet_unchanged,hud_unchanged,matrix_matches); return;
  }
  static uint64_t sequence=0;
  static uint32_t generation=0;
  ++sequence;
  const bool sample=!active || sequence<=3 || sequence%120==0 || generation!=selected->generation;
  if(sample)
    REXLOG_INFO("coop camera preview: seq={} generation={} graphics={:08X} camera={:08X} hud={:08X} packet={:08X} packet_unchanged={} hud_unchanged={} matrix_matches={} selection_matches=true position={},{},{}",
        sequence,selected->generation,graphics,camera,hud,address,packet_unchanged,hud_unchanged,matrix_matches,
        selected->matrix[12],selected->matrix[13],selected->matrix[14]);
  active=true;
  generation=selected->generation;
  frame={graphics,camera,hud,address,generation,sequence,sample};
}

void ObserveP2CameraDraw(uint8_t* base,uint32_t graphics) {
  if(!Enabled() || !frame.camera || graphics!=frame.graphics) return;
  const auto current=CurrentSelection(base,graphics);
  std::array<uint8_t,packet::kPacketSize> data{};
  std::array<uint8_t,64> matrix{};
  const bool readable=Read(base,frame.packet,data.data(),data.size()) &&
      Read(base,current.world+112,matrix.data(),matrix.size());
  const uint32_t offset=frame.source_view ? packet::kSecondaryOffset : 16;
  const bool matches=readable && packet::ReadWord(data,200)==frame.generation &&
      PreviewMatrixMatches({frame.camera,frame.hud},current,
          std::span(data).subspan(offset,64),matrix);
  if(!matches) {
    REXLOG_ERROR("coop camera preview failed: world preparation changed the selected camera");return;
  }
  if(frame.sample) {
    auto value=[&](size_t offset) {return std::bit_cast<float>(packet::ReadWord(matrix,offset));};
    if(frame.source_view)
      REXLOG_INFO("coop camera preview draw: seq={} generation={} camera={:08X} hud={:08X} matrix_matches={} selection_matches=true position={},{},{}",
          frame.sequence,frame.generation,current.world,current.hud,matches,value(48),value(52),value(56));
    else
      REXLOG_INFO("coop camera preview restored: generation=0 camera={:08X} hud={:08X} matrix_matches={} selection_matches=true position={},{},{}",
          current.world,current.hud,matches,value(48),value(52),value(56));
  }
}
}
