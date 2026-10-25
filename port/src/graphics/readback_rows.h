#pragma once
#include <ostream>
#include <span>
#include <cstdint>
#include <array>
#include <algorithm>
namespace superman_returns::graphics {
inline bool WritePaddedReadbackRows(std::ostream& out,std::span<const uint8_t> data,
    size_t offset,uint32_t row_bytes,uint32_t row_pitch,uint32_t height) {
  if(!height || !row_bytes || row_bytes>row_pitch || offset>data.size() ||
     row_bytes>data.size()-offset || uint64_t(height-1)*row_pitch>data.size()-offset-row_bytes) return false;
  const std::array<char,256> zeros{};
  for(uint32_t row=0;row<height && out;++row) {
    out.write(reinterpret_cast<const char*>(data.data()+offset+size_t(row)*row_pitch),row_bytes);
    uint32_t padding=row_pitch-row_bytes;
    while(padding && out) {
      const auto count=std::min<size_t>(padding,zeros.size());
      out.write(zeros.data(),std::streamsize(count)); padding-=uint32_t(count);
    }
  }
  return bool(out);
}
}
