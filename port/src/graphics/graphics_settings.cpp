#include "graphics_settings.h"
#include "graphics_preferences.h"
#include "graphics_options_runtime.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <windows.h>
#include <fstream>
#include <iterator>
#include <bcrypt.h>
#include <algorithm>
#include <cwctype>
#include <mutex>

REXCVAR_DEFINE_BOOL(sr_rt_shadows,false,"Superman Returns","Request RT shadows; unavailable backends remain Off");
REXCVAR_DEFINE_INT32(sr_shadow_softness,1,"Superman Returns","RT shadow softness preference: 1 Low, 2 High").range(1,2);
REXCVAR_DEFINE_BOOL(sr_character_shadows,false,"Superman Returns","Request character participation as RT caster");
REXCVAR_DEFINE_BOOL(sr_rt_reflections,false,"Superman Returns","Request RT reflections; unavailable backends remain Off");
REXCVAR_DEFINE_STRING(sr_dlss,"off","Superman Returns","DLSS Super Resolution preference").allowed({"off","quality","balanced","performance","dlaa"});

namespace superman_returns::graphics {
namespace {
constexpr const char* names[]{"sr_rt_shadows","sr_shadow_softness","sr_character_shadows","sr_rt_reflections","sr_dlss",
  "sr_native_api","window_width","sr_native_fps_limit","sr_native_fxaa","sr_native_msaa_samples","sr_native_render_scale"};
std::mutex settings_mutex;
GraphicsPreferencesBridge bridge;
std::filesystem::path settings_path;
GraphicsValues last_saved{0,1,0,0,0,0,1,0,0,0,2};
bool initialized=false, needs_fill=false, vulkan_backend=false;
constexpr const char* dlss_modes[]{"off","quality","balanced","performance","dlaa"};
int ChoiceIndex(int value,const std::initializer_list<int>& choices,int fallback) {
  int index=0;for(int choice:choices) {if(value==choice) return index;++index;}return fallback;
}
GraphicsValues ReadCvars() {
  GraphicsValues values{0,1,0,0,0,0,1,0,0,0,2};
  values[0]=REXCVAR_GET(sr_rt_shadows);values[1]=REXCVAR_GET(sr_shadow_softness);
  values[2]=REXCVAR_GET(sr_character_shadows);values[3]=REXCVAR_GET(sr_rt_reflections);
  const auto dlss=REXCVAR_GET(sr_dlss);
  for(int i=0;i<5;++i) if(dlss==dlss_modes[i]) values[4]=i;
  values[5]=rex::cvar::Query<std::string>("sr_native_api")=="vulkan";
  const auto width=rex::cvar::Query<int32_t>("window_width"),height=rex::cvar::Query<int32_t>("window_height");
  constexpr int widths[]{960,1280,1600,1920,2560,3840},heights[]{540,720,900,1080,1440,2160};
  for(int i=0;i<6;++i) if(width==widths[i] && height==heights[i]) values[6]=i;
  values[7]=ChoiceIndex(rex::cvar::Query<int32_t>("sr_native_fps_limit"),{0,30,60,120},0);
  values[8]=rex::cvar::Query<bool>("sr_native_fxaa");
  values[9]=ChoiceIndex(rex::cvar::Query<int32_t>("sr_native_msaa_samples"),{1,4,8},0);
  values[10]=ChoiceIndex(int(rex::cvar::Query<double>("sr_native_render_scale")*100+.5),{50,75,100,125,150,200,300,400},2);
  if(initialized && vulkan_backend) {
    const auto intent=SharedGraphicsOptions()->Snapshot().requested;
    for(size_t i=8;i<values.size();++i) values[i]=intent[i];
  }
  return values;
}
void WriteCvars(const GraphicsValues& values) {
  const auto external=ReadCvars();
  const bool vulkan=vulkan_backend;
  for(size_t i=0;i<values.size();++i) if(external[i]!=values[i]) {
    if(vulkan && i>=8) continue; // Retain intent without feeding unsupported cvars to validation/replay.
    std::string text;
    if(i==4) text=dlss_modes[values[i]];
    else if(i==5) text=values[i] ? "vulkan" : "d3d12";
    else if(i==6) {
      constexpr int widths[]{960,1280,1600,1920,2560,3840}, heights[]{540,720,900,1080,1440,2160};
      rex::cvar::SetFlagByName("window_width",std::to_string(widths[values[i]]));
      rex::cvar::SetFlagByName("window_height",std::to_string(heights[values[i]]));continue;
    } else if(i==7) {constexpr int fps[]{0,30,60,120};text=std::to_string(fps[values[i]]);}
    else if(i==9) {constexpr int samples[]{1,4,8};text=std::to_string(samples[values[i]]);}
    else if(i==10) {constexpr const char* scales[]{"0.5","0.75","1","1.25","1.5","2","3","4"};text=scales[values[i]];}
    else text=i==1 ? std::to_string(values[i]) : values[i] ? "true" : "false";
    rex::cvar::SetFlagByName(names[i],text);
  }
}
std::string ReadText(const std::filesystem::path& path) {
  std::error_code error;
  if(!std::filesystem::is_regular_file(path,error)) return {};
  const auto size=std::filesystem::file_size(path,error);
  if(error || size>1024*1024) return {};
  std::ifstream in(path,std::ios::binary);
  return in ? std::string(std::istreambuf_iterator<char>(in),{}) : std::string{};
}
// Exactly matches the launcher's lower-case directory + trailing slash SHA256 ID.
std::filesystem::path SharedPath() {
  wchar_t explicit_path[32768]{};
  if(GetEnvironmentVariableW(L"SR_LAUNCHER_SETTINGS",explicit_path,32768)>0) return explicit_path;
  wchar_t executable[32768]{};
  if(!GetModuleFileNameW(nullptr,executable,32768)) return {};
  std::wstring directory=std::filesystem::path(executable).parent_path().wstring()+L"\\";
  const int lower_count=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,directory.data(),int(directory.size()),nullptr,0,nullptr,nullptr,0);
  if(!lower_count) return {};
  std::wstring lowered(lower_count,L'\0');
  if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,directory.data(),int(directory.size()),lowered.data(),lower_count,nullptr,nullptr,0)) return {};
  directory=std::move(lowered);
  const int count=WideCharToMultiByte(CP_UTF8,0,directory.data(),int(directory.size()),nullptr,0,nullptr,nullptr);
  std::string utf8(count,'\0');WideCharToMultiByte(CP_UTF8,0,directory.data(),int(directory.size()),utf8.data(),count,nullptr,nullptr);
  auto module=LoadLibraryW(L"bcrypt.dll");if(!module) return {};
  auto open=reinterpret_cast<decltype(&BCryptOpenAlgorithmProvider)>(GetProcAddress(module,"BCryptOpenAlgorithmProvider"));
  auto hash=reinterpret_cast<decltype(&BCryptHash)>(GetProcAddress(module,"BCryptHash"));
  auto close=reinterpret_cast<decltype(&BCryptCloseAlgorithmProvider)>(GetProcAddress(module,"BCryptCloseAlgorithmProvider"));
  BCRYPT_ALG_HANDLE algorithm=nullptr;unsigned char digest[32]{};bool ok=false;
  if(open && hash && close && open(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0) {
    ok=hash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(utf8.data()),ULONG(utf8.size()),digest,32)>=0;
    close(algorithm,0);
  }
  FreeLibrary(module);if(!ok) return {};
  constexpr wchar_t hex[]=L"0123456789ABCDEF";std::wstring id;
  for(int i=0;i<6;++i) {id+=hex[digest[i]>>4];id+=hex[digest[i]&15];}
  wchar_t local[32768]{};if(!GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768)) return {};
  return std::filesystem::path(local)/L"SupermanReturns"/L"launcher"/(id+L".ini");
}
bool Save(const GraphicsValues& values) {
  if(settings_path.empty()) return false;

  auto temporary=settings_path;
  temporary+=L".tmp."+std::to_wstring(GetCurrentProcessId());
  std::error_code error;
  if(!settings_path.parent_path().empty()) std::filesystem::create_directories(settings_path.parent_path(),error);
  if(error) return false;
  auto lock_path=settings_path;lock_path+=L".lock";
  const HANDLE file_lock=CreateFileW(lock_path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file_lock==INVALID_HANDLE_VALUE) return false; // Retry next UI frame, never block the guest.
  struct Unlock {HANDLE value;~Unlock(){CloseHandle(value);}} unlock{file_lock};
  std::string latest;
  const bool exists=std::filesystem::exists(settings_path,error);
  if(error) return false;
  if(exists) {
    const auto size=std::filesystem::file_size(settings_path,error);
    if(error || size>1024*1024) return false;
    std::ifstream input(settings_path,std::ios::binary);
    if(!input) return false;
    latest.assign(std::istreambuf_iterator<char>(input),{});
    if(input.bad()) return false;
  }
  const auto text=MergeLauncherGraphicsPreferences(latest,last_saved,values,needs_fill);
  {
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    out.write(text.data(),std::streamsize(text.size()));
    out.close();
    if(!out) {std::filesystem::remove(temporary,error); return false;}
  }
  // Same-directory atomic replacement. Never truncate the previous preferences.
  if(!MoveFileExW(temporary.c_str(),settings_path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(temporary,error); return false;
  }
  return true;
}
}
void InitializeGraphicsSettings(const std::filesystem::path& path) {
  std::lock_guard lock(settings_mutex);
  if(initialized) return;
  settings_path=SharedPath();
  vulkan_backend=rex::cvar::Query<std::string>("sr_native_api")=="vulkan";
  auto values=ReadCvars();
  auto saved=values;
  const auto present=ReadLauncherGraphicsPreferences(ReadText(settings_path),saved);
  GraphicsValues legacy{};
  if(ParseGraphicsPreferences(ReadText(path),legacy))
    for(size_t i=0;i<5;++i) if(!present[i]) saved[i]=legacy[i];
  for(size_t i=0;i<values.size();++i) {
    if(rex::cvar::GetFlagSource(names[i])==rex::cvar::Source::kDefault || (vulkan_backend && i>=8)) values[i]=saved[i];
    needs_fill|=!present[i];
  }
  initialized=bridge.Initialize(*SharedGraphicsOptions(),values);
  WriteCvars(values); last_saved=values;
  REXLOG_INFO("native graphics preferences restored: RT={} softness={} character={} reflections={} DLSS={} (requests only; applied Off until backend execution)",
              values[0],values[1],values[2],values[3],values[4]);
}
void SynchronizeGraphicsSettings(bool flush) {
  std::lock_guard lock(settings_mutex);
  if(!initialized) return;
  const auto values=bridge.Synchronize(*SharedGraphicsOptions(),ReadCvars());
  WriteCvars(values);
  if(values!=last_saved || needs_fill || flush) {
    if(Save(values)) {last_saved=values;needs_fill=false;}
    else if(flush) REXLOG_WARN("native graphics preferences could not be saved: {}",settings_path.string());
  }
}
}
