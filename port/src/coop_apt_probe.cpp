#include "coop_apt_probe.h"
#include "coop_camera_packet.h"
#include "coop_split_render.h"
#include "title_benchmark.h"
#include <array>
#include <string>
#include <vector>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include "generated/default/superman_returns_init.h"
#ifdef _WIN32
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(sr_coop_apt_probe,false,"Superman Returns",
                    "Development: bounded read-only APT packet observations in split HUD passes")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DECLARE(bool,sr_coop_input);
REXCVAR_DECLARE(bool,sr_coop_split_preview);
namespace {
using namespace superman_returns::coop;
constexpr uint32_t kLimit=256;
struct Pass {
  std::optional<SplitHudContext> view;
  uint32_t adapters=0,nodes=0,carriers=0,logged_adapters=0,logged_nodes=0,carrier=0;
};
thread_local Pass pass;
thread_local std::vector<Pass> parents;
bool Enabled() {
  static const bool enabled=REXCVAR_GET(sr_coop_apt_probe)&&REXCVAR_GET(sr_coop_input)&&
      REXCVAR_GET(sr_coop_split_preview)&&!superman_returns::benchmark::Active();
  return enabled;
}
bool Read(uint8_t* base,uint64_t address,void* out,size_t size) {
#ifdef _WIN32
  SIZE_T copied=0;
  return address && address+size<=0x100000000ull &&
      ReadProcessMemory(GetCurrentProcess(),base+address,out,size,&copied)&&copied==size;
#else
  return false;
#endif
}
uint32_t Word(uint8_t* base,uint64_t address) {
  std::array<uint8_t,4> bytes{};
  return Read(base,address,bytes.data(),4)?packet::ReadWord(bytes,0):0;
}
template<size_t N> std::string Hex(const std::array<uint8_t,N>& bytes) {
  static constexpr char digits[]="0123456789abcdef";
  std::string text(N*2,'0');
  for(size_t i=0;i<N;++i){text[i*2]=digits[bytes[i]>>4];text[i*2+1]=digits[bytes[i]&15];}
  return text;
}
void ObserveNode(uint8_t* base,uint32_t node) {
  if(!pass.view)return;
  const auto ordinal=++pass.nodes;
  if(ordinal>kLimit)return;
  ++pass.logged_nodes;
  std::array<uint8_t,52> data{};
  const bool readable=Read(base,node,data.data(),data.size());
  const auto affine=readable?packet::ReadWord(data,8):0;
  std::array<uint8_t,24> matrix{};
  const bool affine_readable=affine && Read(base,affine,matrix.data(),matrix.size());
  const auto& view=*pass.view;
  REXLOG_INFO("coop apt node: frame={} generation={} view={} ordinal={} node={:08X} readable={} data={} affine={:08X} affine_readable={} matrix={} override=false",
      view.frame,view.generation,view.view,ordinal,node,readable,Hex(data),affine,affine_readable,Hex(matrix));
}
} // namespace

void superman_returns::coop::BeginAptHudProbe() {
  if(!Enabled())return;
  parents.push_back(pass);
  pass={};
  const auto view=CurrentSplitHud();
  if(view && (view->frame<=3 || view->frame%120==0))pass.view=view;
}
void superman_returns::coop::EndAptHudProbe() {
  if(!Enabled() || parents.empty())return;
  if(pass.view) {
    const auto view=*pass.view;
    REXLOG_INFO("coop apt pass: frame={} generation={} view={} adapters={} captured={} nodes={} node_captured={} carriers={} limit={} override=false",
        view.frame,view.generation,view.view,pass.adapters,pass.logged_adapters,pass.nodes,pass.logged_nodes,pass.carriers,kLimit);
  }
  pass=parents.back();parents.pop_back();
}

REX_HOOK_RAW(sub_8268C568) {
  if(Enabled())ObserveNode(base,ctx.r4.u32);
  __imp__sub_8268C568(ctx,base);
}
REX_HOOK_RAW(sub_826ACD18) {
  if(!Enabled() || !pass.view){__imp__sub_826ACD18(ctx,base);return;}
  const auto previous=pass.carrier;
  pass.carrier=ctx.r3.u32;++pass.carriers;
  struct Restore {uint32_t previous;~Restore(){pass.carrier=previous;}} restore{previous};
  __imp__sub_826ACD18(ctx,base);
}
REX_HOOK_RAW(sub_826D1648) {
  if(!Enabled() || !pass.view){__imp__sub_826D1648(ctx,base);return;}
  const auto ordinal=++pass.adapters;
  if(ordinal>kLimit){__imp__sub_826D1648(ctx,base);return;}
  ++pass.logged_adapters;
  const auto view=*pass.view;
  const auto carrier_address=pass.carrier;
  const auto object=ctx.r3.u32,arg4=ctx.r4.u32,arg5=ctx.r5.u32,caller=uint32_t(ctx.lr);
  const auto manager=Word(base,0x829A23E0),pool=Word(base,0x829A158C);
  std::array<uint8_t,2> index_bytes{};
  const bool index_readable=Read(base,0x829A1598,index_bytes.data(),index_bytes.size());
  const uint32_t index=(uint32_t(index_bytes[0])<<8)|index_bytes[1];
  const uint64_t transform=uint64_t(pool)+uint64_t(index)*80;
  std::array<uint8_t,80> before{},after{};
  std::array<uint8_t,64> carrier{};
  const bool readable=pool && index_readable && Read(base,transform,before.data(),before.size());
  const bool carrier_readable=Read(base,carrier_address,carrier.data(),carrier.size());
  const auto method=Word(base,uint64_t(Word(base,object))+32);
  // Observe the original adapter/consumer; never replace its packet or registers.
  __imp__sub_826D1648(ctx,base);
  std::array<uint8_t,16> data{};
  const bool packet_readable=manager && Read(base,uint64_t(manager)+84,data.data(),data.size());
  const bool packet_matches=packet_readable && transform<=0xFFFFFFFFull &&
      packet::ReadWord(data,4)==uint32_t(transform) && packet::ReadWord(data,8)==arg4 && packet::ReadWord(data,12)==arg5;
  const bool unchanged=readable && Read(base,transform,after.data(),after.size()) && before==after;
  REXLOG_INFO("coop apt packet: frame={} generation={} view={} ordinal={} object={:08X} method={:08X} caller={:08X} carrier={:08X} carrier_readable={} carrier_data={} source_matches={} manager={:08X} packet={} packet_readable={} packet_matches={} slot={} transform={:08X} readable={} transform_unchanged={} matrix={} override=false",
      view.frame,view.generation,view.view,ordinal,object,method,caller,carrier_address,carrier_readable,Hex(carrier),
      carrier_readable && packet::ReadWord(carrier,32)==object,manager,Hex(data),packet_readable,packet_matches,
      index,transform,readable,unchanged,Hex(before));
}
