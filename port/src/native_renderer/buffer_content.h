#pragma once
#include <cstdint>

namespace superman_returns::native {
// Page writes may belong to a neighboring allocation. Small buffers confirm
// their contents before invalidating; the frame check also covers virtual aliases.
struct BufferContent {
  uint64_t value=0,frame=~uint64_t(0);
  bool valid=false;
  template<class Hash> bool Refresh(uint64_t current_frame,bool page_written,Hash hash) {
    if(valid && frame==current_frame && !page_written) return false;
    const uint64_t next=hash();
    const bool changed=valid && next!=value;
    value=next;frame=current_frame;valid=true;
    return changed;
  }
};
}
