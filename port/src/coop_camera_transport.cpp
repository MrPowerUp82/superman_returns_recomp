#include "coop_second_camera.h"
#include "coop_hud.h"
#include <algorithm>
#include <array>
#include <bit>
#include <mutex>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include "generated/default/superman_returns_init.h"
#include "title_benchmark.h"
#ifdef _WIN32
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(sr_coop_camera_transport, false, "Superman Returns",
                    "Development: serialize the owned P2 camera in the unused second view; no extra rendering")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DECLARE(bool,sr_coop_input);
REXCVAR_DECLARE(bool,sr_coop_second_camera);

namespace {
using namespace superman_returns::coop;
bool Enabled() {
  static const bool enabled=REXCVAR_GET(sr_coop_camera_transport) && REXCVAR_GET(sr_coop_input) &&
      REXCVAR_GET(sr_coop_second_camera) && !superman_returns::benchmark::Active();
  return enabled;
}
bool Read(uint8_t* base,uint32_t address,void* output,size_t length) {
#ifdef _WIN32
  SIZE_T copied=0;
  return address && uint64_t(address)+length<=0x100000000ull &&
      ReadProcessMemory(GetCurrentProcess(),base+address,output,length,&copied) && copied==length;
#else
  return false;
#endif
}
uint32_t Word(uint8_t* base,uint32_t address) {
  std::array<uint8_t,4> bytes{};
  return Read(base,address,bytes.data(),4) ? packet::ReadWord(bytes,0) : 0;
}
void Publish(uint8_t* base,uint32_t address) {
  // This virtual method serves many state packets. Admit only the exact camera
  // producer owned by 82176698; never touch the consumer, HUD or other packets.
  const uint32_t producer=Word(base,0x8296D8C0);
  if(!producer || Word(base,producer+16)!=address) return;
  static std::mutex publish_mutex;
  std::lock_guard lock(publish_mutex);
  static packet::PacketOwner ownership;
  static uint64_t sequence=0;
  static uint32_t logged_generation=0;
  const auto snapshot=SampleSecondCamera();
  if(snapshot.view && snapshot.primary_owner!=Word(base,0x8296D86C)) return;
  std::array<uint8_t,packet::kPacketSize> before{},after{},verified{};
  if(!Read(base,address,before.data(),before.size())) {
    REXLOG_ERROR("coop camera transport failed: unreadable producer packet"); return;
  }
  after=before;
  auto candidate=ownership;
  const auto result=candidate.Publish(after,address,snapshot.view);
  if(result==packet::Result::Inactive) return;
  if(result==packet::Result::Occupied || result==packet::Result::Invalid) {
    REXLOG_WARN("coop camera transport refused: packet={:08X} result={}",address,int(result)); return;
  }
#ifdef _WIN32
  SIZE_T written=0;
  const bool wrote=WriteProcessMemory(GetCurrentProcess(),base+address+packet::kSecondaryOffset,
      after.data()+packet::kSecondaryOffset,packet::kViewSize,&written) && written==packet::kViewSize;
#else
  const bool wrote=false;
#endif
  const bool read_back=wrote && Read(base,address,verified.data(),verified.size());
  const bool primary_unchanged=read_back && std::equal(before.begin(),before.begin()+112,verified.begin());
  const bool secondary_matches=read_back && std::equal(after.begin()+112,after.end(),verified.begin()+112);
  if(!primary_unchanged || !secondary_matches) {
    REXLOG_ERROR("coop camera transport failed: packet={:08X} wrote={} primary_unchanged={} secondary_matches={}",
        address,wrote,primary_unchanged,secondary_matches); return;
  }
  ownership=candidate;
  ++sequence;
  const uint32_t generation=packet::ReadWord(verified,200);
  if(sequence<=3 || sequence%120==0 || generation!=logged_generation || result==packet::Result::Cleared) {
    logged_generation=generation;
    auto value=[&](size_t offset) { return std::bit_cast<float>(packet::ReadWord(verified,offset)); };
    REXLOG_INFO("coop camera transport: seq={} camera_seq={} target={} generation={} active={} primary_unchanged={} secondary_matches={} position={},{},{} projection={},{},{},{}",
        sequence,snapshot.sequence,snapshot.target,generation,
        result==packet::Result::Published,primary_unchanged,secondary_matches,
        value(160),value(164),value(168),value(176),value(180),value(184),value(188));
  }
}
}
REX_HOOK_RAW(sub_82782200) {
  if(CaptureCoopHud(ctx,base))return;
  if(Enabled()) Publish(base,ctx.r3.u32);
  __imp__sub_82782200(ctx,base);
}
