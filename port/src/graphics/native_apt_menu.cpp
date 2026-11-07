#include "native_apt_menu.h"
#include "graphics_options_runtime.h"
#include "graphics_settings.h"
#include "native_graphics_apt_recipe.h"
#include <rex/ppc/context.h>
REX_EXTERN(sub_826C3328);
REX_EXTERN(sub_826815A0);
REX_EXTERN(sub_8269C538);
REX_EXTERN(sub_82698550);
#include <rex/runtime.h>
#include <rex/system/thread_state.h>
#include <rex/system/kernel_state.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/filesystem.h>
#include <rex/hash.h>
#include <rex/logging.h>
#include <array>
#include <vector>
#include <cstring>
#include <filesystem>
#include <atomic>
#include <fstream>
#include <mutex>
#include <windows.h>
namespace {
std::atomic<bool> ready{false};
uint32_t text_scratch=0;
std::recursive_mutex callbacks;
using rex::filesystem::Entry;
// VFS OpenFile resolves the parent directory before GetChild. Forward the
// original DATA tree and substitute exactly one immutable child in that tree.
class AptForwardEntry final : public rex::filesystem::Entry {
  rex::filesystem::Entry* original_;
 public:
  AptForwardEntry(rex::filesystem::Device* device,Entry* parent,Entry* original)
      : Entry(device,parent,parent ? parent->path()+"\\"+original->name() : ""),original_(original) {
    attributes_=original->attributes();size_=original->size();allocation_size_=original->allocation_size();
    create_timestamp_=original->create_timestamp();access_timestamp_=original->access_timestamp();write_timestamp_=original->write_timestamp();
  }
  void Add(std::unique_ptr<Entry> entry) {children_.push_back(std::move(entry));}
  rex::X_STATUS Open(uint32_t access,rex::filesystem::File** file) override {return original_->Open(access,file);}
  bool can_map() const override {return original_->can_map();}
  std::unique_ptr<rex::memory::MappedMemory> OpenMapped(rex::memory::MappedMemory::Mode mode,size_t offset,size_t length) override {
    return original_->OpenMapped(mode,offset,length);
  }
};
class AptDataDevice final : public rex::filesystem::HostPathDevice {
  rex::filesystem::Entry* original_;
  std::filesystem::path ui_;
  std::unique_ptr<AptForwardEntry> root_;
  std::unique_ptr<AptForwardEntry> Clone(Entry* source,Entry* parent) {
    auto entry=std::make_unique<AptForwardEntry>(this,parent,source);
    for(const auto& child:source->children()) {
      if(source==original_ && _stricmp(child->name().c_str(),"ui.AST")==0) {
        rex::filesystem::FileInfo info;
        if(!rex::filesystem::GetInfo(ui_,&info))return {};
        auto* replacement=rex::filesystem::HostPathEntry::Create(this,entry.get(),ui_,info);
        if(!replacement)return {};
        entry->Add(std::unique_ptr<Entry>(replacement));
      } else {
        auto forwarded=Clone(child.get(),entry.get());if(!forwarded)return {};
        entry->Add(std::move(forwarded));
      }
    }
    return entry;
  }
 public:
  AptDataDevice(Entry* original,const std::filesystem::path& ui)
      :HostPathDevice("\\Device\\SrNativeGraphicsUi",ui.parent_path(),true),original_(original),ui_(ui) {}
  bool Initialize() override {root_=Clone(original_,nullptr);return bool(root_);}
  Entry* ResolvePath(std::string_view path) override {return root_?root_->ResolvePath(path):nullptr;}
  void Dump(rex::string::StringBuffer* out) override {if(root_)root_->Dump(out,0);}
};
}
bool superman_returns::graphics::NativeAptMenuReady(){return ready.load(std::memory_order_acquire);}
void superman_returns::graphics::InstallNativeAptMenu(rex::Runtime& runtime) {
  using namespace apt_recipe;
  auto* fs=runtime.file_system();if(!fs)return;
  auto* entry=dynamic_cast<rex::filesystem::HostPathEntry*>(fs->ResolvePath("d:\\DATA\\ui.AST"));
  if(!entry || rex::hash_file(entry->host_path())!=kSourceHash){REXLOG_WARN("native APT menu: unknown UI archive; original preserved");return;}
  wchar_t local[32768]{};
  if(!GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768))return;
  const auto folder=std::filesystem::path(local)/"SupermanReturns"/"native_apt"/kOutputHash;
  std::error_code error;std::filesystem::create_directories(folder,error);if(error)return;
  const auto output=folder/"ui.AST";
  if(rex::hash_file(output)!=kOutputHash) {
    std::ifstream file(entry->host_path(),std::ios::binary);
    std::vector<uint8_t> original(kSourceSize);if(!file.read(reinterpret_cast<char*>(original.data()),original.size()))return;
    auto pending=output;pending+=L".pending";std::ofstream write(pending,std::ios::binary|std::ios::trunc);
    uint64_t written=0;
    for(const auto& op:kOps) {
      const bool literal=op.source==UINT32_MAX;
      if(uint64_t(literal?op.literal:op.source)+op.length>(literal?sizeof(kLiterals):original.size()))return;
      write.write(reinterpret_cast<const char*>(literal?kLiterals+op.literal:original.data()+op.source),op.length);written+=op.length;
    }
    write.close();if(!write || written!=kOutputSize || rex::hash_file(pending)!=kOutputHash)return;
    if(!MoveFileExW(pending.c_str(),output.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return;
  }
  auto* original_data=fs->ResolvePath("d:\\DATA");if(!original_data)return;
  auto device=std::make_unique<AptDataDevice>(original_data,output);
  if(!device->Initialize() || !fs->RegisterDevice(std::move(device)))return;
  // Resolve d:/game: first, then DATA. OpenFile's parent lookup now uses our tree.
  text_scratch=runtime.memory()->SystemHeapAlloc(4096);if(!text_scratch)return;
  constexpr char mount[]="\\Device\\Harddisk0\\Partition1\\DATA";
  if(!fs->RegisterSymbolicLink(mount,"\\Device\\SrNativeGraphicsUi"))return;
  auto* actual=dynamic_cast<rex::filesystem::HostPathEntry*>(fs->ResolvePath("d:\\DATA\\ui.AST"));
  if(!actual || actual->host_path()!=output) {fs->UnregisterSymbolicLink(mount);return;}
  ready.store(true,std::memory_order_release);
  REXLOG_INFO("native APT menu: installed original Pause/Toggle assets, ui_override={} source_unchanged=true overlay=false",output.string());
}

// Manifest midasm, at PauseGame entry before its prologue. Exactly three tagged
// arguments use the port bridge; every normal PauseGame invocation stays original.
bool SrNativeGraphicsAptCall(PPCRegister& result,PPCRegister& count,PPCRegister& stack) {
  if(!superman_returns::graphics::NativeAptMenuReady() || count.u32!=3)return false;
  std::lock_guard lock(callbacks);
  auto* base=rex::system::kernel_state()->memory()->virtual_membase();
  PPCContext call=*rex::runtime::current_ppc_context();call.r1=stack;
  const auto arg=[&](uint32_t index) {
    call.r3.u64=index;sub_826C3328(call,base);
    if(!call.r3.u32)return int32_t(-1);
    sub_826815A0(call,base);return call.r3.s32;
  };
  const int32_t operation=arg(0);if(operation!=21330 && operation!=21331)return false;
  const int32_t row=arg(1),field=arg(2);
  using namespace superman_returns::graphics;
  const auto snapshot=SharedGraphicsOptions()->Snapshot();
  int answer=-1;std::string text;bool string=false;
  if(operation==21330 && row==-1 && (field==0 || field==1)) {
    REXLOG_INFO("native APT graphics screen: open={} overlay=false",field==0);
    answer=1;
  }
  if(row>=0 && row<int(kGraphicsOptionCount)) {
    const auto option=static_cast<GraphicsOption>(row);
    if(operation==21331) {
      const auto pause=ReadGraphicsGuestPause([&](uint32_t address)->std::optional<uint32_t>{
        uint32_t value=0;SIZE_T n=0;if(!ReadProcessMemory(GetCurrentProcess(),base+address,&value,4,&n)||n!=4)return {};
        return _byteswap_ulong(value);
      });
      answer=pause.paused && SharedGraphicsOptions()->Request(option,field);
      REXLOG_INFO("native APT graphics request: row={} value={} accepted={} guest_paused={} overlay=false",row,field,answer,pause.paused);
      if(answer)SynchronizeGraphicsSettings();
    } else switch(field) {
      case 0:answer=snapshot.requested[row];break;
      case 1:answer=snapshot.applied[row];break;
      case 2:answer=snapshot.features[row].implemented && snapshot.features[row].capable;break;
      case 3:string=true;text=GraphicsOptionLabel(option);if(GraphicsOptionRequiresRestart(option))text+=" [RESTART]";break;
      case 4:
        string=true;
        for(int value=0;value<=GraphicsOptionMaximum(option);++value){if(value)text+='|';
          text+=(!snapshot.features[row].implemented || !snapshot.features[row].capable) ?
              std::string_view("N/A") : GraphicsValueLabel(option,value);}break;
      case 5:string=true;text=snapshot.features[row].reason;break;
    }
  }
  if(string) {
    if(text.size()>4095)text.resize(4095);
    std::memcpy(base+text_scratch,text.c_str(),text.size()+1);call.r3.u64=text_scratch;sub_8269C538(call,base);
  } else {call.r3.s64=answer;sub_82698550(call,base);}
  result=call.r3;return true;
}
