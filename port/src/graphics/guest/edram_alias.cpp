#include "edram_alias.h"
#include <algorithm>
namespace superman_returns::graphics::guest {
bool EdramOverlaps(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
  if(!an || !bn) return false;
  if(an>=2048 || bn>=2048) return true;
  a%=2048;b%=2048;
  for(uint32_t shift:{0u,2048u}) if((a+shift<b+bn && b<a+shift+an) || (b+shift<a+an && a<b+shift+bn)) return true;
  return false;
}
AliasPlan PlanEdramAlias(const AliasSurface& target,std::span<const AliasSurface> surfaces,std::span<const uint64_t> bound) {
  AliasPlan out;
  for(const auto& other:surfaces) {
    if(other.id==target.id || other.last_write<=target.last_write || std::find(bound.begin(),bound.end(),other.id)!=bound.end()) continue;
    if(other.geometry.edram_base==target.geometry.edram_base && other.depth==target.depth) {
      if(!target.depth && other.geometry.width==target.geometry.width && other.geometry.height==target.geometry.height && other.scale==target.scale && other.last_write>out.newest_same) {
        out.source=other.id;out.newest_same=other.last_write;
      }
      continue;
    }
    if(EdramOverlaps(target.geometry.edram_base,target.geometry.edram_tiles,other.geometry.edram_base,other.geometry.edram_tiles)) out.newest_other=std::max(out.newest_other,other.last_write);
  }
  if(out.newest_same>out.newest_other) out.action=AliasAction::kReinterpret;
  else if(out.newest_other) out.action=AliasAction::kClear;
  return out;
}
}
