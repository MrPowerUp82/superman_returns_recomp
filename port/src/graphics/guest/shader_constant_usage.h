#pragma once
#include "shader_texture_usage.h"
#include <array>
#include <cstdint>
#include <span>

namespace superman_returns::graphics::guest {
struct ShaderConstantUsage {
  std::array<uint64_t,4> mask{};
  bool known=false;
  bool dynamic=false;
  bool operator==(const ShaderConstantUsage&) const = default;
};

// Uses the bounded XDK/CF validation in shader_texture_usage.h. Xenos ALU bit
// layouts: Xenia/ReXGlue ucode.h (same BSD-3-Clause attribution as that file).
// Overapproximate all three source fields, even for unused ALU operands.
inline ShaderConstantUsage AnalyzeShaderConstantUsage(std::span<const uint8_t> container) {
  if(!AnalyzeShaderTextureUsage(container).known) return {};
  const auto be32=[](std::span<const uint8_t> bytes,size_t offset) {
    return (uint32_t(bytes[offset])<<24)|(uint32_t(bytes[offset+1])<<16)|
        (uint32_t(bytes[offset+2])<<8)|uint32_t(bytes[offset+3]);
  };
  const auto physical=container.subspan(be32(container,4),be32(container,8));
  const auto code=physical.subspan(be32(container,be32(container,24)),
                                  be32(container,be32(container,24)+4));
  const auto cf_at=[&](size_t index) {
    const size_t offset=index/2*12;const uint32_t middle=be32(code,offset+4);
    return !(index&1) ? uint64_t(be32(code,offset))|(uint64_t(middle&0xffff)<<32) :
        uint64_t(middle>>16)|(uint64_t(be32(code,offset+8))<<16);
  };
  const auto is_exec=[](uint32_t op) {return (op>=1 && op<=6)||op==13||op==14;};
  size_t pairs=code.size()/12;
  ShaderConstantUsage usage;usage.known=true;
  for(size_t pair=0;pair<pairs;++pair) for(size_t half=0;half<2;++half) {
    const uint64_t cf=cf_at(pair*2+half);if(!is_exec(uint32_t(cf>>44))) continue;
    const size_t address=cf&0xfff,count=(cf>>12)&7;pairs=std::min(pairs,address);
    uint32_t sequence=uint32_t(cf>>16)&0xfff;
    for(size_t i=0;i<count;++i,sequence>>=2) {
      if(sequence&1) continue; // Fetch instructions address a separate bank.
      const size_t offset=(address+i)*12;
      const uint32_t word0=be32(code,offset),word1=be32(code,offset+4),word2=be32(code,offset+8);
      const uint32_t scalar=word0>>26,vector=(word2>>24)&31;
      if(vector>29 || scalar>50 || scalar==41) return {};
      const bool scalar_const=scalar>=42 && scalar<=47;
      bool has_constant=false;
      for(uint32_t source=0;source<3;++source) {
        const bool temporary=(word2>>(31-source))&1;
        // mulsc/addsc/subsc always have a constant in src3: its selector bit
        // encodes the other temporary operand instead of the storage class.
        if(temporary && !(source==2 && scalar_const)) continue;
        const uint32_t reg=(word2>>((2-source)*8))&255;
        usage.mask[reg/64]|=uint64_t(1)<<(reg%64);has_constant=true;
      }
      // Either relative-constant flag may select a0 or aL. GPR-relative
      // addressing alone is irrelevant. Never infer a bounded constant array.
      if(has_constant && (word1&0xc0000000u)) usage.dynamic=true;
    }
  }
  if(usage.dynamic) usage.mask.fill(UINT64_MAX);
  return usage;
}
}
