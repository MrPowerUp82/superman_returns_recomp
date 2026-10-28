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

SR_TEST(readback_volume_exports_all_four_slices_with_row_and_slice_padding) {
  // Three prefix bytes, two 3-byte rows per slice, 4-byte row stride,
  // 12-byte slice stride. The final mapped row has no trailing padding.
  std::vector<uint8_t> data(46,0xa5);
  for(uint32_t z=0;z<4;++z)
    for(uint32_t row=0;row<2;++row)
      for(uint32_t x=0;x<3;++x)
        data[3+z*12+row*4+x]=uint8_t(1+z*6+row*3+x);
  const ReadbackVolumeLayout layout{3,3,4,2,4,12};
  const std::array<std::string,4> expected{
      std::string("\1\2\3\0\4\5\6\0",8),
      std::string("\7\10\11\0\12\13\14\0",8),
      std::string("\15\16\17\0\20\21\22\0",8),
      std::string("\23\24\25\0\26\27\30\0",8)};
  for(uint32_t z=0;z<4;++z) {
    std::ostringstream out;
    SR_CHECK(WritePaddedReadbackSlice(out,data,layout,z));
    SR_CHECK(out.str()==expected[z]);
  }
}

SR_TEST(readback_volume_rejects_truncated_last_slice_before_exporting_any_slice) {
  const std::vector<uint8_t> data(45,7);
  for(uint32_t z=0;z<4;++z) {
    std::ostringstream out;
    SR_CHECK(!WritePaddedReadbackSlice(out,data,{3,3,4,2,4,12},z));
    SR_CHECK(out.str().empty());
  }
}

SR_TEST(readback_volume_rejects_overlapping_invalid_and_overflowing_layouts) {
  const std::vector<uint8_t> data(32,7);
  for(const ReadbackVolumeLayout layout:{
      ReadbackVolumeLayout{0,3,4,2,2,7}, // next slice overlaps padded rows
      {0,5,4,2,2,8},{0,0,4,2,2,8},{0,3,4,0,2,8},
      {0,3,4,2,0,8},{SIZE_MAX,3,4,2,2,8},
      {0,3,4,2,3,SIZE_MAX},{0,3,4,2,2,SIZE_MAX},
      {0,1,UINT32_MAX,UINT32_MAX,UINT32_MAX,SIZE_MAX}}) {
    std::ostringstream out;
    SR_CHECK(!WritePaddedReadbackSlice(out,data,layout,0));
    SR_CHECK(out.str().empty());
  }
  std::ostringstream out;
  SR_CHECK(!WritePaddedReadbackSlice(out,data,{0,3,4,2,2,8},2));
  SR_CHECK(out.str().empty());
}

SR_TEST(readback_volume_keeps_single_slice_export_identical_and_reports_stream_failure) {
  const std::vector<uint8_t> data{99,98,1,2,3,77,4,5,6};
  std::ostringstream rows,slice;
  SR_CHECK(WritePaddedReadbackRows(rows,data,2,3,4,2));
  SR_CHECK(WritePaddedReadbackSlice(slice,data,{2,3,4,2,1,8},0));
  SR_CHECK(slice.str()==rows.str());
  std::ostringstream failed; failed.setstate(std::ios::badbit);
  SR_CHECK(!WritePaddedReadbackSlice(failed,data,{2,3,4,2,1,8},0));
}
