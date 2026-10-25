#include "graphics_preferences.h"
#include <charconv>

namespace superman_returns::graphics {
namespace {
constexpr std::string_view keys[]{"rt_shadows","shadow_softness","character_shadows","rt_reflections","dlss"};
std::string_view Trim(std::string_view s) {
  const auto first=s.find_first_not_of(" \t\r");
  if(first==s.npos) return {};
  return s.substr(first,s.find_last_not_of(" \t\r")-first+1);
}
bool Valid(size_t i,int value) {return value>=(i==1 ? 1 : 0) && value<=(i==1 ? 2 : 1);}
}
bool ParseGraphicsPreferences(std::string_view text,GraphicsValues& values) {
  if(text.size()>16384) return false;
  GraphicsValues parsed{};
  unsigned seen=0;
  bool version=false;
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
      if(version || value!=1) return false;
      version=true; continue;
    }
    size_t i=0;
    while(i<kGraphicsOptionCount && keys[i]!=name) ++i;
    if(i==kGraphicsOptionCount) continue; // Future numeric keys do not replace known preferences.
    if((seen&(1u<<i)) || !Valid(i,value)) return false;
    seen|=1u<<i; parsed[i]=value;
  }
  if(!version || seen!=(1u<<kGraphicsOptionCount)-1) return false;
  values=parsed; return true;
}
std::string SerializeGraphicsPreferences(const GraphicsValues& values) {
  std::string result="version=1\n";
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
