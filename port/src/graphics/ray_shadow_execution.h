#pragma once
#include "graphics_options.h"
namespace superman_returns::graphics {
class RayShadowExecution {
 public:
  struct Ticket {
    uint64_t fence=0,frame=0,revision=0;
    GraphicsValues requested{};
    bool counter_recorded=false,character_built=false;
  };
  struct Result {uint64_t frame=0,revision=0,pixels=0; GraphicsValues applied{};};
  bool Submit(uint32_t slot,uint64_t fence,uint64_t frame,uint64_t revision,
              GraphicsValues requested,bool counter_recorded,bool character_built=false) {
    if(slot>=slots_.size() || !fence || !frame || slots_[slot].fence) return false;
    slots_[slot]={fence,frame,revision,requested,counter_recorded,character_built}; return true;
  }
  Ticket Pending(uint32_t slot) const {return slot<slots_.size() ? slots_[slot] : Ticket{};}
  bool Complete(uint32_t slot,uint64_t completed,uint64_t shader_pixels) {
    if(slot>=slots_.size()) return false;
    const auto pending=slots_[slot];
    if(!pending.fence || completed<pending.fence || completed==UINT64_MAX) return false;
    slots_[slot]={};
    if(pending.frame>latest_.frame) {
      latest_={pending.frame,pending.revision,shader_pixels,{}};
      if(pending.counter_recorded && shader_pixels && pending.requested[0]==1) {
        latest_.applied[0]=1; latest_.applied[1]=pending.requested[1];
        latest_.applied[2]=pending.requested[2]==1 && pending.character_built ? 1 : 0;
      }
    }
    return true;
  }
  Result Latest() const {return latest_;}
 private:
  std::array<Ticket,3> slots_{};
  Result latest_;
};
}
