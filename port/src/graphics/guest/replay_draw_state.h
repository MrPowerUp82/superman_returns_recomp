#pragma once
#include "../../native_renderer/pm4_mirror.h"
#include <bitset>

namespace superman_returns::graphics::guest {
// Immutable after preparation. A decode job never borrows the live PM4 mirror.
class ReplayDrawState {
public:
  ReplayDrawState() = default;
  explicit ReplayDrawState(const native::Pm4Mirror& mirror) {
    for(uint32_t i=0;i<232;++i) {
      const uint32_t reg=i<192?0x4800+i:0x4900+i-192;
      registers_[i]=mirror.reg(reg);written_[i]=mirror.written(reg);
    }
    for(size_t i=0;i<other_registers.size();++i)other_[i]=mirror.reg(other_registers[i]);
    floats_[0]=mirror.ShareFloatConstants(false);floats_[1]=mirror.ShareFloatConstants(true);
  }
  uint32_t reg(uint32_t index) const {
    if(index-0x4800u<192u)return registers_[index-0x4800u];
    if(index-0x4900u<40u)return registers_[192+index-0x4900u];
    for(size_t i=0;i<other_registers.size();++i)if(index==other_registers[i])return other_[i];
    return 0;
  }
  bool written(uint32_t index) const {
    if(index-0x4800u<192u)return written_[index-0x4800u];
    if(index-0x4900u<40u)return written_[192+index-0x4900u];
    return false;
  }
  const std::shared_ptr<const std::array<uint32_t,1024>>& ShareFloatConstants(bool pixel) const {
    return floats_[pixel?1:0];
  }
private:
  static constexpr std::array<uint32_t,9> other_registers{
      0x2380,0x2381,0x2382,0x2383,0x2081,0x2082,0x2080,0x200e,0x200f};
  std::array<uint32_t,232> registers_{};
  std::bitset<232> written_;
  std::array<uint32_t,9> other_{};
  std::array<std::shared_ptr<const std::array<uint32_t,1024>>,2> floats_;
};
} // namespace superman_returns::graphics::guest
