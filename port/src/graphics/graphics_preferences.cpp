#include "graphics_preferences.h"
#include <charconv>

namespace superman_returns::graphics {
namespace {
constexpr std::string_view keys[]{"rt_shadows","shadow_softness","character_shadows","rt_reflections","dlss",
    "graphics_api","resolution","fps_limit","antialiasing","msaa","render_scale"};
std::string_view Trim(std::string_view s) {
  const auto first=s.find_first_not_of(" \t\r");
  if(first==s.npos) return {};
  return s.substr(first,s.find_last_not_of(" \t\r")-first+1);
}
bool Valid(size_t i,int value) {return value>=(i==1 ? 1 : 0) && value<=GraphicsOptionMaximum(static_cast<GraphicsOption>(i));}
}
bool ParseGraphicsPreferences(std::string_view text,GraphicsValues& values) {
  if(text.size()>16384) return false;
  GraphicsValues parsed{0,1,0,0,0,0,1,0,0,0,2};
  unsigned seen=0;
  int version=0;
  while(!text.empty()) {
    const auto end=text.find('\n');
    auto line=Trim(text.substr(0,end));
    text=end==text.npos ? std::string_view{} : text.substr(end+1);
    line=Trim(line.substr(0,line.find('#')));
    if(line.empty()) continue;
    const auto equal=line.find('=');
    if(equal==line.npos) return false;
    const auto name=Trim(line.substr(0,equal)), raw=Trim(line.substr(equal+1));
    if(raw.empty()) return false;
    int value=0;
    const auto result=std::from_chars(raw.data(),raw.data()+raw.size(),value);
    if(result.ec!=std::errc{} || result.ptr!=raw.data()+raw.size()) return false;
    if(name=="version") {
      if(version || (value!=1 && value!=2)) return false;
      version=value; continue;
    }
    size_t i=0;
    while(i<kGraphicsOptionCount && keys[i]!=name) ++i;
    if(i==kGraphicsOptionCount) continue; // Future numeric keys do not replace known preferences.
    if((seen&(1u<<i)) || !Valid(i,value)) return false;
    seen|=1u<<i; parsed[i]=value;
  }
  if(!version || (version==1 ? seen!=31 : seen!=(1u<<kGraphicsOptionCount)-1)) return false;
  values=parsed; return true;
}
std::string SerializeGraphicsPreferences(const GraphicsValues& values) {
  std::string result="version=2\n";
  for(size_t i=0;i<values.size();++i) {
    if(!Valid(i,values[i])) return {};
    result+=std::string(keys[i])+"="+std::to_string(values[i])+"\n";
  }
  return result;
}
bool GraphicsPreferencesBridge::Initialize(GraphicsOptions& options,const GraphicsValues& values) {
  if(!options.RestorePreferences(values)) return false;
  previous_=values; return true;
}
GraphicsValues GraphicsPreferencesBridge::Synchronize(GraphicsOptions& options,const GraphicsValues& external) {
  for(size_t i=0;i<external.size();++i)
    if(external[i]!=previous_[i]) options.Request(static_cast<GraphicsOption>(i),external[i]);
  previous_=options.Snapshot().requested;
  return previous_;
}
}

namespace superman_returns::graphics {
namespace {
std::string IniValue(size_t i,int value) {
  if(!Valid(i,value)) return {};
  if(i==5) return value ? "vulkan" : "d3d12";
  if(i==6) return std::string(GraphicsValueLabel(GraphicsOption::kResolution,value));
  if(i==7) { constexpr int fps[]{0,30,60,120}; return std::to_string(fps[value]); }
  if(i==8) return value ? "FXAA" : "Off";
  if(i==9) { constexpr int samples[]{1,4,8}; return std::to_string(samples[value]); }
  if(i==10) { constexpr int scales[]{50,75,100,125,150,200,300,400}; return std::to_string(scales[value]); }
  return std::to_string(value);
}
bool ParseIniValue(size_t i,std::string_view raw,int& value) {
  for(int v=GraphicsOptionMinimum(static_cast<GraphicsOption>(i));v<=GraphicsOptionMaximum(static_cast<GraphicsOption>(i));++v)
    if(raw==IniValue(i,v)) {value=v;return true;}
  if(i==0 || i==2 || i==3) {
    if(raw=="true") {value=1;return true;}
    if(raw=="false") {value=0;return true;}
  }
  return false;
}
}
std::string_view LauncherGraphicsKey(GraphicsOption option) {
  const auto i=static_cast<size_t>(option);
  return i<kGraphicsOptionCount ? keys[i] : std::string_view{};
}
std::array<bool,kGraphicsOptionCount> ReadLauncherGraphicsPreferences(std::string_view text,GraphicsValues& values) {
  std::array<bool,kGraphicsOptionCount> present{};
  if(text.size()>1024*1024) return present;
  while(!text.empty()) {
    const auto end=text.find('\n');
    auto line=Trim(text.substr(0,end));
    text=end==text.npos ? std::string_view{} : text.substr(end+1);
    if(line.empty() || line.front()=='#' || line.front()==';') continue;
    const auto equal=line.find('=');
    if(equal==line.npos) continue;
    const auto key=Trim(line.substr(0,equal)), raw=Trim(line.substr(equal+1));
    for(size_t i=0;i<kGraphicsOptionCount;++i) if(key==keys[i]) {
      int value=0;
      if(ParseIniValue(i,raw,value)) {values[i]=value;present[i]=true;}
    }
  }
  return present;
}
std::string MergeLauncherGraphicsPreferences(std::string_view text,const GraphicsValues& previous,
    const GraphicsValues& requested,bool fill_missing) {
  GraphicsValues disk{};
  const auto present=ReadLauncherGraphicsPreferences(text,disk);
  std::array<bool,kGraphicsOptionCount> dirty{},written{};
  for(size_t i=0;i<kGraphicsOptionCount;++i)
    dirty[i]=Valid(i,requested[i]) && (requested[i]!=previous[i] || (fill_missing && !present[i]));
  std::string result;
  while(!text.empty()) {
    const auto end=text.find('\n');
    const auto raw=text.substr(0,end);
    auto line=Trim(raw);
    text=end==text.npos ? std::string_view{} : text.substr(end+1);
    const auto equal=line.find('=');
    const auto key=equal==line.npos ? std::string_view{} : Trim(line.substr(0,equal));
    bool replaced=false;
    for(size_t i=0;i<kGraphicsOptionCount;++i) if(dirty[i] && key==keys[i]) {
      if(!written[i]) result+=std::string(keys[i])+"="+IniValue(i,requested[i])+"\n";
      written[i]=true;replaced=true;break;
    }
    if(!replaced) {result+=raw;result+='\n';}
  }
  for(size_t i=0;i<kGraphicsOptionCount;++i) if(dirty[i] && !written[i])
    result+=std::string(keys[i])+"="+IniValue(i,requested[i])+"\n";
  return result;
}
}
