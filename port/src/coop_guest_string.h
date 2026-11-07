#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace superman_returns::coop {
// Reader must reject inaccessible ranges and guest-address overflow. Never
// consume a failed block: it may have been only partially copied.
template<class Read>
std::string ReadGuestSymbol(uint32_t address, Read read) {
  if (!address) return {};
  std::string value;
  for (uint64_t offset=0; offset<128; offset+=32) {
    std::array<uint8_t,32> block{};
    const bool complete=read(uint64_t(address)+offset,block.data(),block.size());
    for (size_t i=0;i<block.size();++i) {
      uint8_t byte=block[i];
      if (!complete && !read(uint64_t(address)+offset+i,&byte,1)) return {};
      if (!byte) return value;
      if (byte<32 || byte>126) return {};
      value+=char(byte);
    }
  }
  return {};
}
}
