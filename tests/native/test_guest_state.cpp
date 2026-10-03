#include "../../port/src/graphics/guest/constant_snapshot.h"
#include "../../port/src/graphics/guest/draw_state.h"
#include "test_main.h"
#include <cstring>
using namespace superman_returns::graphics::guest;
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
  VertexFetchMeta meta{2, 0, 4, 5};
  ConstantSnapshot snap;
  std::memcpy(snap.shared.data() + 512, &meta, sizeof(meta));
  VertexFetchMeta copy{};
  std::memcpy(&copy, snap.shared.data() + 512, sizeof(copy));
  SR_CHECK_EQ(copy.type, 5);
  SR_CHECK_EQ(copy.stride, 4);
}
