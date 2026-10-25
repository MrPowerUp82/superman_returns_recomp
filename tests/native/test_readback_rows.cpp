#include "test_main.h"
#include "../../port/src/graphics/readback_rows.h"
#include <sstream>
#include <array>
#include <vector>
using namespace superman_returns::graphics;
SR_TEST(readback_rows_never_reads_trailing_padding_outside_gpu_footprint) {
  constexpr uint32_t row_bytes=1126*4,pitch=4608,height=634;
  constexpr size_t total=size_t(height-1)*pitch+row_bytes;
  // Guard lives in CPU storage but is deliberately outside the mapped span.
  // This exposes the original bug without invoking undefined host memory access.
  std::vector<uint8_t> data(total+104,0x7f);
  std::fill(data.begin()+total,data.end(),0xa5);
  std::ostringstream out;
  SR_CHECK(WritePaddedReadbackRows(out,{data.data(),total},0,row_bytes,pitch,height));
  const auto image=out.str();
  SR_CHECK_EQ(image.size(),size_t(pitch)*height);
  SR_CHECK_EQ(uint8_t(image[(height-1)*pitch+row_bytes-1]),0x7fu);
  for(size_t i=0;i<104;++i) SR_CHECK_EQ(uint8_t(image[total+i]),0u);
}
SR_TEST(readback_rows_rejects_incomplete_or_overflowing_footprints_without_output) {
  const std::vector<uint8_t> data(32,7);
  for(const auto spec:{std::array<size_t,4>{0,8,4,2},{30,4,4,1},{0,8,16,3},{SIZE_MAX,4,4,2}}) {
    std::ostringstream out;
    SR_CHECK(!WritePaddedReadbackRows(out,data,spec[0],uint32_t(spec[1]),uint32_t(spec[2]),uint32_t(spec[3])));
    SR_CHECK(out.str().empty());
  }
}
SR_TEST(readback_rows_preserves_offset_rows_and_emits_deterministic_padding) {
  const std::vector<uint8_t> data{99,98,1,2,3,77,4,5,6};
  std::ostringstream out;
  SR_CHECK(WritePaddedReadbackRows(out,data,2,3,4,2));
  SR_CHECK(out.str()==std::string("\1\2\3\0\4\5\6\0",8));
  std::ostringstream failed; failed.setstate(std::ios::badbit);
  SR_CHECK(!WritePaddedReadbackRows(failed,data,2,3,4,2));
}
