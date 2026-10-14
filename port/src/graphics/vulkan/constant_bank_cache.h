#pragma once
#include "../guest/render_packet.h"
#include <array>
#include <cstring>
namespace superman_returns::graphics::vulkan {
// Comparisons use bounded host storage, never mapped write-combined memory.
class ConstantBankCache {
public:
  struct Reuse {bool vs=false,ps=false;uint32_t vs_offset=0,ps_offset=0;};
  void Reset() {chunk_=nullptr;serial_=0;}
  Reuse Find(uint64_t serial,const void* chunk,const guest::ConstantSnapshot& value) const {
    if(!chunk || chunk!=chunk_ || !serial || serial!=serial_) return {};
    return {std::memcmp(vs_.data(),value.vs.data(),sizeof(vs_))==0,
            std::memcmp(ps_.data(),value.ps.data(),sizeof(ps_))==0,vs_offset_,ps_offset_};
  }
  void Commit(uint64_t serial,const void* chunk,uint32_t offset,const guest::ConstantSnapshot& value,Reuse reuse) {
    if(chunk!=chunk_ || serial!=serial_) reuse={};
    if(!reuse.vs) {std::memcpy(vs_.data(),value.vs.data(),sizeof(vs_));vs_offset_=offset;}
    if(!reuse.ps) {std::memcpy(ps_.data(),value.ps.data(),sizeof(ps_));ps_offset_=offset;}
    serial_=serial;chunk_=chunk;
  }
private:
  std::array<uint32_t,1024> vs_{},ps_{};
  uint64_t serial_=0;const void* chunk_=nullptr;uint32_t vs_offset_=0,ps_offset_=0;
};
}
