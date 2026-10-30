#pragma once
#include "shader_constant_usage.h"
#include <bit>
#include <cstdlib>
#include <cstring>

namespace superman_returns::graphics::guest {
inline bool ShaderConstantMaskEnabled() {
  // Experimental: fewer writes did not improve the game's measured frame rate.
  static const bool enabled=[] {const char* v=std::getenv("SR_NATIVE_USED_CONSTANT_MASK");return v && v[0] && v[0]!='0';}();
  return enabled;
}
// Compare sanitized CPU snapshots, never upload-heap / write-combined memory.
inline bool EqualShaderConstants(std::span<const uint32_t,1024> before,
                                 std::span<const uint32_t,1024> after,
                                 const ShaderConstantUsage& usage) {
  if(!usage.known || usage.dynamic) return std::memcmp(before.data(),after.data(),4096)==0;
  for(uint32_t block=0;block<4;++block) {
    uint64_t bits=usage.mask[block];
    while(bits) {
      const uint32_t first=std::countr_zero(bits),count=std::countr_one(bits>>first);
      const uint32_t reg=block*64+first;
      if(std::memcmp(before.data()+reg*4,after.data()+reg*4,count*16)) return false;
      bits&=count==64?0:~(((uint64_t(1)<<count)-1)<<first);
    }
  }
  return true;
}

// A retained D3D12 upload contains every register at uploaded_version_. Masked
// reuse NEVER changes that snapshot/version. Memoize only the proof for the
// checked version and mask, so expanding shader usage rechecks stale lanes.
class VersionedShaderConstantCache {
public:
  void Reset() {uploaded_version_=checked_version_=0;}
  void Commit(uint64_t version,std::span<const uint32_t,1024> words) {
    std::memcpy(uploaded_.data(),words.data(),4096);uploaded_version_=version;checked_version_=0;
  }
  template<class Reader> bool CanReuse(uint64_t version,const ShaderConstantUsage& usage,Reader&& words) {
    if(!version || !uploaded_version_ || version<uploaded_version_) return false;
    if(version==uploaded_version_) return true;
    if(!usage.known) return false;
    if(version==checked_version_ && usage==checked_usage_) return true;
    if(!EqualShaderConstants(uploaded_,words(),usage)) return false;
    checked_version_=version;checked_usage_=usage;return true;
  }
private:
  std::array<uint32_t,1024> uploaded_{};
  uint64_t uploaded_version_=0,checked_version_=0;
  ShaderConstantUsage checked_usage_;
};
}
