#include "graphics_settings.h"
#include "graphics_preferences.h"
#include "graphics_options_runtime.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <windows.h>
#include <fstream>
#include <iterator>

REXCVAR_DEFINE_BOOL(sr_rt_shadows,false,"Superman Returns","Request RT shadows; unavailable backends remain Off");
REXCVAR_DEFINE_INT32(sr_shadow_softness,1,"Superman Returns","RT shadow softness preference: 1 Low, 2 High").range(1,2);
REXCVAR_DEFINE_BOOL(sr_character_shadows,false,"Superman Returns","Request character participation as RT caster");
REXCVAR_DEFINE_BOOL(sr_rt_reflections,false,"Superman Returns","Request RT reflections; unavailable backends remain Off");
REXCVAR_DEFINE_STRING(sr_dlss,"off","Superman Returns","DLSS Super Resolution preference").allowed({"off","quality"});

namespace superman_returns::graphics {
namespace {
constexpr const char* names[]{"sr_rt_shadows","sr_shadow_softness","sr_character_shadows","sr_rt_reflections","sr_dlss"};
GraphicsPreferencesBridge bridge;
std::filesystem::path settings_path;
GraphicsValues last_attempt{0,1,0,0,0};
bool initialized=false;
GraphicsValues ReadCvars() {
  return {REXCVAR_GET(sr_rt_shadows),REXCVAR_GET(sr_shadow_softness),REXCVAR_GET(sr_character_shadows),
          REXCVAR_GET(sr_rt_reflections),REXCVAR_GET(sr_dlss)=="quality"};
}
void WriteCvars(const GraphicsValues& values) {
  const auto external=ReadCvars();
  for(size_t i=0;i<values.size();++i) if(external[i]!=values[i]) {
    const std::string text=i==1 ? std::to_string(values[i]) :
        i==4 ? (values[i] ? "quality" : "off") : (values[i] ? "true" : "false");
    rex::cvar::SetFlagByName(names[i],text);
  }
}
bool Save(const GraphicsValues& values) {
  if(settings_path.empty()) return true;
  const auto text=SerializeGraphicsPreferences(values);
  if(text.empty()) return false;
  auto temporary=settings_path;
  temporary+=L".tmp."+std::to_wstring(GetCurrentProcessId());
  std::error_code error;
  if(!settings_path.parent_path().empty()) std::filesystem::create_directories(settings_path.parent_path(),error);
  if(error) return false;
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
  if(initialized) return;
  settings_path=path;
  auto values=ReadCvars();
  std::error_code error;
  if(!path.empty() && std::filesystem::is_regular_file(path,error)) {
    GraphicsValues saved{};
    const auto size=std::filesystem::file_size(path,error);
    std::ifstream in(path,std::ios::binary);
    std::string text;
    if(!error && size<=16384 && in) text.assign(std::istreambuf_iterator<char>(in),{});
    if(ParseGraphicsPreferences(text,saved)) {
      for(size_t i=0;i<values.size();++i)
        if(rex::cvar::GetFlagSource(names[i])==rex::cvar::Source::kDefault) values[i]=saved[i];
    } else REXLOG_WARN("native graphics preferences ignored: invalid or unsupported file {}",path.string());
  }
  initialized=bridge.Initialize(*SharedGraphicsOptions(),values);
  WriteCvars(values); last_attempt=values;
  REXLOG_INFO("native graphics preferences restored: RT={} softness={} character={} reflections={} DLSS={} (requests only; applied Off until backend execution)",
              values[0],values[1],values[2],values[3],values[4]);
}
void SynchronizeGraphicsSettings(bool flush) {
  if(!initialized) return;
  const auto values=bridge.Synchronize(*SharedGraphicsOptions(),ReadCvars());
  WriteCvars(values);
  if(values!=last_attempt || flush) {
    if(!Save(values)) REXLOG_WARN("native graphics preferences could not be saved: {}",settings_path.string());
    last_attempt=values;
  }
}
}
