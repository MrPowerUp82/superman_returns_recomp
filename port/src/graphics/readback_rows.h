#pragma once
#include <ostream>
#include <span>
#include <cstdint>
#include <cstddef>
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

struct ReadbackVolumeLayout {
  size_t offset;
  uint32_t row_bytes,row_pitch,rows,depth;
  size_t slice_pitch;
};

// A Texture3D mip is one subresource containing every Z slice. Validate the
// entire mapped footprint before exporting even Z0, then preserve the existing
// 2D row layout for the selected slice. GPU row/slice padding is never read.
inline bool WritePaddedReadbackSlice(std::ostream& out,std::span<const uint8_t> data,
    const ReadbackVolumeLayout& layout,uint32_t slice) {
  if(!layout.rows || !layout.depth || !layout.row_bytes ||
     layout.row_bytes>layout.row_pitch || slice>=layout.depth ||
     layout.offset>data.size()) return false;
  const uint64_t padded_rows=uint64_t(layout.rows)*layout.row_pitch;
  if(padded_rows>SIZE_MAX || layout.slice_pitch<size_t(padded_rows)) return false;
  const size_t slice_bytes=size_t(layout.rows-1)*layout.row_pitch+layout.row_bytes;
  const size_t available=data.size()-layout.offset;
  if(slice_bytes>available ||
     size_t(layout.depth-1)>(available-slice_bytes)/layout.slice_pitch) return false;
  return WritePaddedReadbackRows(out,data,
      layout.offset+size_t(slice)*layout.slice_pitch,
      layout.row_bytes,layout.row_pitch,layout.rows);
}
}
