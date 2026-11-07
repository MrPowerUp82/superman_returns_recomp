#include "coop_apt_probe.h"
#include "coop_camera_packet.h"
#include "coop_apt_ancestry.h"
#include "coop_apt_stamina.h"
#include "coop_guest_string.h"
#include "coop_status.h"
#include "coop_split_render.h"
#include "title_benchmark.h"
#include <array>
#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <bit>
#include <mutex>
#include <rex/runtime.h>
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
REXCVAR_DEFINE_BOOL(sr_coop_stamina_preview,false,"Superman Returns",
                    "Development: private P2 stamina fill/notch transforms; equal 10000-point capacities only")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
namespace {
using namespace superman_returns::coop;
constexpr uint32_t kLimit=256;
struct MovieInfo {
  apt::Movie kind=apt::Movie::Unknown;
  std::vector<uint32_t> characters,exported;
};
struct Geometry {
  std::optional<float> start,notch,mask_fill,mask_right;
  bool cap_supported=false;
};
struct Pass {
  std::optional<SplitHudContext> view;
  bool sampled=false;
  uint32_t adapters=0,nodes=0,carriers=0,logged_adapters=0,logged_nodes=0,carrier=0;
  uint32_t pushes=0,pops=0,selected=0;
  uint32_t preview_applied=0,preview_skipped=0;
  std::array<uint32_t,6> roles{};
  apt::Ancestry ancestry;
  std::unordered_map<uint32_t,MovieInfo> movies;
  std::unordered_map<uint32_t,apt::Node> sources;
  std::unordered_map<uint32_t,Geometry> geometry;
};
thread_local Pass pass;
thread_local std::vector<Pass> parents;
std::atomic<uint32_t> preview_scratch{0};
std::mutex preview_mutex;
thread_local bool inside_preview=false;
bool Enabled() {
  static const bool enabled=REXCVAR_GET(sr_coop_apt_probe)&&REXCVAR_GET(sr_coop_input)&&
      REXCVAR_GET(sr_coop_split_preview)&&!superman_returns::benchmark::Active();
  return enabled;
}
bool PreviewEnabled(){static const bool enabled=REXCVAR_GET(sr_coop_stamina_preview)&&Enabled();return enabled;}
bool Read(uint8_t* base,uint64_t address,void* out,size_t size) {
#ifdef _WIN32
  SIZE_T copied=0;
  return address && address+size<=0x100000000ull &&
      ReadProcessMemory(GetCurrentProcess(),base+address,out,size,&copied)&&copied==size;
#else
  return false;
#endif
}
bool Write(uint8_t* base,uint32_t address,const void* input,size_t size) {
#ifdef _WIN32
  SIZE_T copied=0;
  return address && uint64_t(address)+size<=0x100000000ull &&
      WriteProcessMemory(GetCurrentProcess(),base+address,input,size,&copied)&&copied==size;
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
std::string String(uint8_t* base,uint32_t address) {
  return ReadGuestSymbol(address,[&](uint64_t at,void* out,size_t size) {
    return Read(base,at,out,size);
  });
}
const MovieInfo& Movie(uint8_t* base,uint32_t root) {
  auto [entry,inserted]=pass.movies.try_emplace(root);
  if(!inserted)return entry->second;
  auto& info=entry->second;
  std::array<uint8_t,64> header{};
  const bool readable=Read(base,root,header.data(),header.size());
  const auto count=packet::ReadWord(header,28),exports=packet::ReadWord(header,56);
  const auto characters=packet::ReadWord(header,32),table=packet::ReadWord(header,60);
  if(readable && packet::ReadWord(header,0)==9 && count && count<=4096 && exports<=128) {
    std::vector<uint8_t> chars(count*4),names(exports*8);
    if(Read(base,characters,chars.data(),chars.size()) && packet::ReadWord(chars,0)==root &&
       exports && Read(base,table,names.data(),names.size())) {
      for(uint32_t i=0;i<count;++i)info.characters.push_back(packet::ReadWord(chars,i*4));
      for(uint32_t i=0;i<exports;++i) {
        const auto name=String(base,packet::ReadWord(names,i*8));
        const auto index=packet::ReadWord(names,i*8+4);
        apt::Movie kind=apt::Movie::Unknown;
        if(name=="Main_Hud")kind=apt::Movie::MainHud;
        if(name=="StaminaMeter")kind=apt::Movie::StaminaMeter;
        if(name=="GenericMeter")kind=apt::Movie::GenericMeter;
        if(kind!=apt::Movie::Unknown && index<count) {
          info.kind=kind;info.exported.push_back(info.characters[index]);
        }
      }
    }
  }
  const auto view=*pass.view;
  if(pass.sampled)REXLOG_INFO("coop apt movie: frame={} generation={} view={} root={:08X} readable={} movie={} characters={} exports={} header={} override=false",
      view.frame,view.generation,view.view,root,readable,uint32_t(info.kind),info.characters.size(),exports,Hex(header));
  return info;
}
apt::Node ResolveNode(uint8_t* base,uint32_t address,const std::array<uint8_t,52>& data) {
  const auto source=packet::ReadWord(data,4);
  auto [entry,inserted]=pass.sources.try_emplace(source);
  if(inserted) {
    auto& value=entry->second;value.source=source;value.root=Word(base,uint64_t(source)+4);
    value.character=0xFFFFFFFFu;
    const auto& info=Movie(base,value.root);
    for(uint32_t i=0;i<info.characters.size();++i)
      if(info.characters[i]==source){value.character=i;break;}
    value.movie=info.kind;
    value.exported=std::find(info.exported.begin(),info.exported.end(),source)!=info.exported.end();
  }
  auto value=entry->second;
  value.address=address;value.depth=(uint32_t(data[20])<<8)|data[21];
  const auto pointer=packet::ReadWord(data,8);
  std::array<uint8_t,24> affine{};
  value.affine_valid=!pointer || Read(base,pointer,affine.data(),affine.size());
  if(pointer && value.affine_valid)
    for(size_t i=0;i<6;++i)value.affine[i]=std::bit_cast<float>(packet::ReadWord(affine,i*4));
  for(float v:value.affine)if(!std::isfinite(v))value.affine_valid=false;
  return value;
}
Geometry& MeterGeometry(uint8_t* base,const apt::Node& meter) {
  auto [entry,inserted]=pass.geometry.try_emplace(meter.address);
  if(inserted && Word(base,meter.source)==5 && Word(base,uint64_t(meter.source)+16)==1) {
    const auto& movie=Movie(base,meter.root);
    if(movie.characters.size()>23) {
      const auto cap=movie.characters[23];
      const float left=std::bit_cast<float>(Word(base,uint64_t(cap)+16));
      const float right=std::bit_cast<float>(Word(base,uint64_t(cap)+24));
      entry->second.cap_supported=Word(base,cap)==1 && std::isfinite(left) && std::isfinite(right) && right-left==8;
    }
    const auto frames=Word(base,uint64_t(meter.source)+20);
    const auto count=Word(base,frames),controls=Word(base,uint64_t(frames)+4);
    if(count<=512 && controls)for(uint32_t i=0;i<count;++i) {
      const auto control=Word(base,uint64_t(controls)+i*4),kind=Word(base,control);
      if((kind!=3 && kind!=9) || !(Word(base,uint64_t(control)+4)&0x20))continue;
      const auto name=String(base,Word(base,uint64_t(control)+52));
      const float x=std::bit_cast<float>(Word(base,uint64_t(control)+32));
      if(!std::isfinite(x))continue;
      if(name=="mFillLeft")entry->second.start=x;
      if(name=="mNotch")entry->second.notch=x;
    }
  }
  return entry->second;
}
void RecordGeometry(uint8_t* base) {
  const auto selected=pass.ancestry.Select();if(!selected)return;
  const auto path=pass.ancestry.Path();const auto& meter=path[selected->meter];
  auto& geometry=MeterGeometry(base,meter);
  if(selected->role==apt::Role::Mask && path.size()>selected->child+1) {
    const auto& child=path[selected->child+1];
    if(child.affine_valid) {
      if(child.character==11 && child.depth==3)geometry.mask_fill=child.affine[4];
      if(child.character==12 && child.depth==5)geometry.mask_right=child.affine[4];
    }
  }
}
void CaptureWorld(uint8_t* base,uint32_t node) {
  if(!pass.view)return;
  auto* top=pass.ancestry.Top();if(!top || top->address!=node)return;
  const auto pool=Word(base,0x829A158C);
  std::array<uint8_t,2> index{};std::array<uint8_t,64> matrix{};
  if(!pool || !Read(base,0x829A1598,index.data(),index.size()))return;
  const uint32_t slot=(uint32_t(index[0])<<8)|index[1];
  if(!Read(base,uint64_t(pool)+uint64_t(slot)*80,matrix.data(),matrix.size()))return;
  top->world_valid=true;
  for(size_t i=0;i<16;++i) {
    top->world[i]=std::bit_cast<float>(packet::ReadWord(matrix,i*4));
    if(!std::isfinite(top->world[i]))top->world_valid=false;
  }
}
std::string Path() {
  std::string text;
  for(const auto& node:pass.ancestry.Path()) {
    if(!text.empty())text+=';';
    text+=std::to_string(node.address)+":"+std::to_string(node.source)+":"+
        std::to_string(node.root)+":"+std::to_string(node.character)+":"+
        std::to_string(node.depth)+":"+std::to_string(uint32_t(node.movie))+":"+
        std::to_string(node.exported?1:0);
  }
  return text.empty()?"empty":text;
}
void ObserveNode(uint8_t* base,uint32_t node) {
  if(!pass.view)return;
  const auto ordinal=++pass.nodes;
  std::array<uint8_t,52> data{};
  const bool readable=Read(base,node,data.data(),data.size());
  auto value=ResolveNode(base,node,data);
  if(!readable)value={node};
  pass.ancestry.Push(value);++pass.pushes;
  if(PreviewEnabled())RecordGeometry(base);
  if(!pass.sampled || ordinal>kLimit)return;
  ++pass.logged_nodes;
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
  if(view) {
    pass.sampled=view->frame<=3 || view->frame%120==0;
    if(pass.sampled || PreviewEnabled())pass.view=view;
  }
}
void superman_returns::coop::EndAptHudProbe() {
  if(!Enabled() || parents.empty())return;
  if(pass.view && pass.sampled) {
    const auto view=*pass.view;
    REXLOG_INFO("coop apt pass: frame={} generation={} view={} adapters={} captured={} nodes={} node_captured={} carriers={} limit={} override=false",
        view.frame,view.generation,view.view,pass.adapters,pass.logged_adapters,pass.nodes,pass.logged_nodes,pass.carriers,kLimit);
    REXLOG_INFO("coop apt scopepass: frame={} generation={} view={} pushes={} pops={} valid={} balanced={} selected={} mask={} right={} body={} left={} notch={} override=false",
        view.frame,view.generation,view.view,pass.pushes,pass.pops,pass.ancestry.Valid(),
        pass.ancestry.Valid()&&pass.ancestry.Path().empty(),pass.selected,
        pass.roles[1],pass.roles[2],pass.roles[3],pass.roles[4],pass.roles[5]);
    if(PreviewEnabled())REXLOG_INFO("coop apt previewpass: frame={} generation={} view={} applied={} skipped={} override={}",
        view.frame,view.generation,view.view,pass.preview_applied,pass.preview_skipped,pass.preview_applied!=0);
  }
  pass=parents.back();parents.pop_back();
}

REX_HOOK_RAW(sub_8268C568) {
  const auto node=ctx.r4.u32;
  if(Enabled())ObserveNode(base,ctx.r4.u32);
  __imp__sub_8268C568(ctx,base);
  if(PreviewEnabled())CaptureWorld(base,node);
}
REX_HOOK_RAW(sub_8268C670) {
  const auto node=ctx.r4.u32;
  if(Enabled())ObserveNode(base,ctx.r4.u32);
  __imp__sub_8268C670(ctx,base);
  if(PreviewEnabled())CaptureWorld(base,node);
}
REX_HOOK_RAW(sub_82685290) {
  const auto node=ctx.r4.u32;
  __imp__sub_82685290(ctx,base);
  if(Enabled() && pass.view){pass.ancestry.Pop(node);++pass.pops;}
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
  const auto role=pass.ancestry.SelectedRole();
  if(role!=apt::Role::None){++pass.selected;++pass.roles[uint32_t(role)];}
  if(!pass.sampled || ordinal>kLimit){__imp__sub_826D1648(ctx,base);return;}
  ++pass.logged_adapters;
  const auto view=*pass.view;
  const auto carrier_address=pass.carrier;
  const auto path=Path();
  const auto previous_applied=pass.preview_applied;
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
  // The adapter is unchanged. The optional consumer hook below uses private
  // storage; the original packet/slot are still read back here.
  __imp__sub_826D1648(ctx,base);
  std::array<uint8_t,16> data{};
  const bool packet_readable=manager && Read(base,uint64_t(manager)+84,data.data(),data.size());
  const bool packet_matches=packet_readable && transform<=0xFFFFFFFFull &&
      packet::ReadWord(data,4)==uint32_t(transform) && packet::ReadWord(data,8)==arg4 && packet::ReadWord(data,12)==arg5;
  const bool unchanged=readable && Read(base,transform,after.data(),after.size()) && before==after;
  REXLOG_INFO("coop apt packet: frame={} generation={} view={} ordinal={} object={:08X} method={:08X} caller={:08X} carrier={:08X} carrier_readable={} carrier_data={} source_matches={} manager={:08X} packet={} packet_readable={} packet_matches={} slot={} transform={:08X} readable={} transform_unchanged={} matrix={} role={} scope_valid={} path={} override={}",
      view.frame,view.generation,view.view,ordinal,object,method,caller,carrier_address,carrier_readable,Hex(carrier),
      carrier_readable && packet::ReadWord(carrier,32)==object,manager,Hex(data),packet_readable,packet_matches,
      index,transform,readable,unchanged,Hex(before),uint32_t(role),pass.ancestry.Valid(),path,pass.preview_applied!=previous_applied);
}

void superman_returns::coop::InstallAptPreview(rex::Runtime& runtime) {
  if(PreviewEnabled())preview_scratch.store(runtime.memory()->SystemHeapAlloc(96),std::memory_order_release);
}
REX_HOOK_RAW(sub_826E08F8) {
  const auto selected=pass.ancestry.Select();
  if(!PreviewEnabled() || !pass.view || pass.view->view!=1 || !selected ||
     selected->role==apt::Role::Mask || inside_preview) {__imp__sub_826E08F8(ctx,base);return;}
  bool applied=false;
  struct CountSkip {bool& applied;~CountSkip(){if(!applied)++pass.preview_skipped;}} count_skip{applied};
  const auto value=StaminaForHud(pass.view->generation,1),primary=StaminaForHud(pass.view->generation,0);
  const auto scratch=preview_scratch.load(std::memory_order_acquire);
  const auto path=pass.ancestry.Path();const auto& meter=path[selected->meter];const auto& role=path[selected->child];
  const auto geometry=MeterGeometry(base,meter);
  if(!scratch || !value || !primary || value->maximum!=10000 || primary->maximum!=10000 ||
     !geometry.cap_supported || !geometry.start || !geometry.notch || !geometry.mask_fill || !geometry.mask_right ||
     !meter.world_valid || !role.affine_valid || role.affine[0]<=0 || role.affine[1]!=0 || role.affine[2]!=0 ||
     (selected->role!=apt::Role::FillBody && role.affine[0]!=1) ||
     Word(base,uint64_t(pass.carrier)+32)!=ctx.r3.u32 || Word(base,uint64_t(pass.carrier)+4)!=meter.root) {
    __imp__sub_826E08F8(ctx,base);return;
  }
  const float width=*geometry.mask_right-*geometry.mask_fill+16;
  if(selected->role==apt::Role::FillBody && std::abs(2*role.affine[0]-(width-16))>0.001f) {
    __imp__sub_826E08F8(ctx,base);return;
  }
  const auto desired=apt::Position(selected->role,value->scaled,width,*geometry.start,*geometry.notch);
  const auto original_address=ctx.r4.u32;
  std::array<uint8_t,16> original{},packet_after{};
  std::array<uint8_t,80> matrix{},matrix_after{};
  if(!desired || !Read(base,original_address,original.data(),original.size()) ||
     !Read(base,packet::ReadWord(original,4),matrix.data(),matrix.size())) {__imp__sub_826E08F8(ctx,base);return;}
  const auto data=apt::PrivatePacket(original,matrix,scratch,*desired,role.affine[4],meter.world);
  if(!data){__imp__sub_826E08F8(ctx,base);return;}
  std::lock_guard lock(preview_mutex);
  if(!Write(base,scratch,data->data(),data->size())){__imp__sub_826E08F8(ctx,base);return;}
  const auto original_arg=ctx.r4;
  inside_preview=true;
  struct Restore {PPCContext& ctx;decltype(ctx.r4) arg;~Restore(){ctx.r4=arg;inside_preview=false;}} restore{ctx,original_arg};
  ctx.r4.u64=scratch;
  __imp__sub_826E08F8(ctx,base);
  applied=true;++pass.preview_applied;
  std::array<uint8_t,96> private_after{};
  const bool unchanged=Read(base,original_address,packet_after.data(),packet_after.size()) && original==packet_after &&
      Read(base,packet::ReadWord(original,4),matrix_after.data(),matrix_after.size()) && matrix==matrix_after &&
      Read(base,scratch,private_after.data(),private_after.size()) && private_after==*data;
  if(!unchanged)REXLOG_ERROR("coop apt preview failed: original/private packet readback");
  if(pass.sampled) {
    std::array<uint8_t,64> basis{};
    for(size_t i=0;i<16;++i)packet::WriteWord(basis,i*4,std::bit_cast<uint32_t>(meter.world[i]));
    REXLOG_INFO("coop apt preview: frame={} generation={} view=1 ordinal={} role={} target={} current={} maximum={} scaled={} width={} start={} notch={} desired={} local={} basis={} original_packet={} original_matrix={} scratch={:08X} private={} unchanged={} override=true",
        pass.view->frame,pass.view->generation,pass.adapters,uint32_t(selected->role),
        value->target,value->current,value->maximum,value->scaled,width,*geometry.start,*geometry.notch,*desired,role.affine[4],
        Hex(basis),Hex(original),Hex(matrix),scratch,Hex(*data),unchanged);
  }
}
