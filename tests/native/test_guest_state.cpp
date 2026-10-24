#include "../../port/src/graphics/guest/constant_snapshot.h"
#include "../../port/src/graphics/guest/draw_state.h"
#include "test_main.h"
#include <cstring>
using namespace superman_returns::graphics::guest;
SR_TEST(normalized_ushort_strip_promotes_restart_to_32_bit_cut) {
  const uint8_t bytes[]{1,0,0xff,0x7f,2,0,0xff,0xff};
  std::vector<uint32_t> values;std::string error;
  SR_CHECK(NormalizeIndices(bytes,0,4,{false,0,0x7fff},Primitive::kTriangleStrip,values,error));
  SR_CHECK_EQ(values[0],1u);SR_CHECK_EQ(values[1],UINT32_MAX);
  SR_CHECK_EQ(values[2],2u);SR_CHECK_EQ(values[3],0xffffu);
  SR_CHECK(NormalizeIndices(bytes,0,4,{false,0,UINT32_MAX},Primitive::kTriangleStrip,values,error));
  SR_CHECK_EQ(values[1],0x7fffu);SR_CHECK_EQ(values[3],0xffffu);
  SR_CHECK(NormalizeIndices(bytes,0,4,{false,0,0x7fff},Primitive::kTriangles,values,error));
  SR_CHECK_EQ(values[1],0x7fffu);
}
SR_TEST(guest_snapshot_owns_data) {
  std::array<uint32_t, 1024> source{};
  source[0] = 0x3f800000;
  ConstantSnapshot snapshot;
  std::string error;
  SR_CHECK(CaptureFloatConstants(source, false, snapshot.vs, error));
  source[0] = 0;
  SR_CHECK_EQ(snapshot.vs[0], 0x3f800000);
}
SR_TEST(constant_nan_sanitizer_preserves_infinity) {
  std::array<uint32_t, 1024> source{};
  source[0] = 0x7fc00001;
  source[1] = 0x7f800000;
  source[2] = 0xff800000;
  std::array<uint32_t, 1024> out{};
  std::string error;
  SR_CHECK(CaptureFloatConstants(source, false, out, error));
  SR_CHECK_EQ(out[0], 0);
  SR_CHECK_EQ(out[1], source[1]);
  SR_CHECK_EQ(out[2], source[2]);
  source[0] = 0x0000803f;
  SR_CHECK(CaptureFloatConstants(source, true, out, error));
  SR_CHECK_EQ(out[0], 0x3f800000);
  out.fill(42);
  SR_CHECK(
      !CaptureFloatConstants(std::span(source).first(20), false, out, error));
  SR_CHECK_EQ(out[0], 42);
}
SR_TEST(constant_capture_preserves_ieee_bits_in_both_byte_orders) {
  std::array<uint32_t,1024> host{},source{},out{};
  std::string error;
  uint32_t random=0xCE34F712;
  for(uint32_t bank=0;bank<64;++bank) {
    for(size_t i=0;i<host.size();++i) {
      random^=random<<13;random^=random>>17;random^=random<<5;
      // First banks cover every sign/exponent and exact/edge mantissas.
      const uint32_t mantissas[]={0,1,0x400000,0x7FFFFF};
      host[i]=bank<2?(((uint32_t(i)/4)&255u)<<23)|((bank&1u)<<31)|mantissas[i%4]:random;
    }
    for(bool be:{false,true}) {
      for(size_t i=0;i<host.size();++i) {
        const auto v=host[i];
        source[i]=be?(v>>24)|((v>>8)&0xFF00)|((v<<8)&0xFF0000)|(v<<24):v;
      }
      SR_CHECK(CaptureFloatConstants(source,be,out,error));
      SR_CHECK(error.empty());
      for(size_t i=0;i<out.size();++i) {
        const uint32_t magnitude=host[i]&0x7FFFFFFF;
        SR_CHECK_EQ(out[i],magnitude>0x7F800000?0u:host[i]);
      }
    }
  }
}
SR_TEST(constant_capture_handles_in_place_and_unaligned_longer_source) {
  alignas(16) std::array<uint32_t,1028> storage{};
  auto source=std::span(storage).subspan(1);
  source[0]=0x7FA00001;source[1]=0x80000000;source[1023]=0xFF800000;
  std::array<uint32_t,1024> out{};
  std::string error;
  SR_CHECK(CaptureFloatConstants(source,false,out,error));
  SR_CHECK_EQ(out[0],0u);SR_CHECK_EQ(out[1],0x80000000u);SR_CHECK_EQ(out[1023],0xFF800000u);
  out[0]=0x0100C07F; // guest-order NaN
  out[1]=0x0000807F; // guest-order infinity
  SR_CHECK(CaptureFloatConstants(out,true,out,error));
  SR_CHECK_EQ(out[0],0u);SR_CHECK_EQ(out[1],0x7F800000u);
  SR_CHECK(CaptureFloatConstants(out,false,out,error));
  SR_CHECK_EQ(out[1],0x7F800000u);
}
SR_TEST(constant_capture_rejects_truncation_without_changing_output) {
  std::array<uint32_t,1024> source{},out{};out.fill(0x87654321);
  const auto original=out;
  std::string error;
  for(bool be:{false,true}) for(size_t length:{size_t(0),size_t(1023)}) {
    SR_CHECK(!CaptureFloatConstants(std::span(source).first(length),be,out,error));
    SR_CHECK(!error.empty());SR_CHECK(out==original);
  }
  SR_CHECK(CaptureFloatConstants(source,false,out,error));
  SR_CHECK(error.empty());
}
SR_TEST(indices_reject_truncated_and_overflowing_ranges) {
  std::array<uint8_t, 3> bytes{};
  std::vector<uint32_t> out{42};
  std::string error;
  SR_CHECK(!NormalizeIndices(bytes, 0, 1, {true, 0, UINT32_MAX},
                             Primitive::kTriangles, out, error));
  SR_CHECK(out == std::vector<uint32_t>{42});
  SR_CHECK(!NormalizeIndices(bytes, UINT32_MAX, UINT32_MAX,
                             {true, 0, UINT32_MAX}, Primitive::kTriangles, out,
                             error));
  // 8-in-32 byte swapping of 16-bit indices may address beyond nominal width.
  std::array<uint8_t, 2> short_pair{};
  SR_CHECK(!NormalizeIndices(short_pair, 0, 1, {false, 2, UINT32_MAX},
                             Primitive::kTriangles, out, error));
}
SR_TEST(quad_expansion_keeps_winding) {
  std::array<uint16_t, 4> indices{0, 1, 2, 3};
  std::vector<uint32_t> out;
  std::string error;
  auto bytes = std::span(reinterpret_cast<const uint8_t *>(indices.data()),
                         sizeof(indices));
  SR_CHECK(NormalizeIndices(bytes, 0, 4, {false, 0, UINT32_MAX},
                            Primitive::kQuads, out, error));
  SR_CHECK(out == std::vector<uint32_t>({0, 1, 2, 0, 2, 3}));
}
SR_TEST(restart_only_cuts_strips) {
  std::array<uint32_t, 3> indices{1, 0x00fffffe, 2};
  std::vector<uint32_t> out;
  std::string error;
  auto bytes = std::span(reinterpret_cast<const uint8_t *>(indices.data()),
                         sizeof(indices));
  SR_CHECK(NormalizeIndices(bytes, 0, 3, {true, 0, 0xfffffe},
                            Primitive::kTriangleStrip, out, error));
  SR_CHECK_EQ(out[1], UINT32_MAX);
  SR_CHECK(NormalizeIndices(bytes, 0, 3, {true, 0, 0xfffffe},
                            Primitive::kTriangles, out, error));
  SR_CHECK_EQ(out[1], 0xfffffe);
}
SR_TEST(ushort2_type_is_preserved) {
  VertexFetchMeta meta{2, 0, 4, 0x2C2259};
  ConstantSnapshot snap;
  std::memcpy(snap.shared.data() + 512, &meta, sizeof(meta));
  VertexFetchMeta copy{};
  std::memcpy(&copy, snap.shared.data() + 512, sizeof(copy));
  SR_CHECK_EQ(copy.type, 0x2C2259);
  SR_CHECK_EQ(copy.stride, 4);
}
