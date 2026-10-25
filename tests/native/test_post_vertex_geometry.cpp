#include "../../port/src/graphics/post_vertex_geometry.h"
#include "test_main.h"
#include <limits>

using namespace superman_returns::graphics;

SR_TEST(post_vertex_geometry_recovers_world_from_row_dot_projection_and_host_pixel_offset) {
  // Matrix acts on column vectors: each clip component is a dot with a row.
  const Matrix4 vp{2,0,0,4, 0,3,0,-6, 0,0,0.5f,1, 0,0,0.25f,1};
  const std::array<float, 4> world{2, -1, 4, 1};
  auto clip = TransformColumn(vp, world);
  const std::array<float, 4> screen{1,1,0,0};
  const std::array<float, 2> pixel{1.0f/1280, -1.0f/720};
  clip[0] += pixel[0] * clip[3];
  clip[1] += pixel[1] * clip[3];
  Matrix4 inv{};
  SR_CHECK(InvertProjection(vp, inv));
  const auto p = RecoverWorldPosition(inv, clip, screen, pixel);
  SR_CHECK(p.valid);
  for (int i=0; i<3; ++i) SR_CHECK(std::abs(p.xyz[i]-world[i]) < 1e-5f);
  // A transpose must not accidentally pass as the same camera convention.
  Matrix4 transposed{};
  for (int r=0;r<4;++r) for(int c=0;c<4;++c) transposed[r*4+c]=inv[c*4+r];
  const auto wrong=RecoverWorldPosition(transposed, clip, screen, pixel);
  SR_CHECK(!wrong.valid || std::abs(wrong.xyz[0]-world[0]) > 0.1f);
}

SR_TEST(post_vertex_geometry_rejects_singular_nonfinite_and_points_at_infinity) {
  Matrix4 inverse{};
  SR_CHECK(!InvertProjection(Matrix4{}, inverse));
  Matrix4 identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  SR_CHECK(!RecoverWorldPosition(identity, {1,2,3,0}, {1,1,0,0}, {0,0}).valid);
  SR_CHECK(!RecoverWorldPosition(identity, {1,2,3,1}, {0,1,0,0}, {0,0}).valid);
  identity[0]=std::numeric_limits<float>::infinity();
  SR_CHECK(!InvertProjection(identity,inverse));
}

SR_TEST(post_vertex_geometry_bounds_strip_expansion_without_overflow_or_partial_triangles) {
  SR_CHECK_EQ(PostVertexCapacity(6, false), 6u);
  SR_CHECK_EQ(PostVertexCapacity(7, false), 6u);
  SR_CHECK_EQ(PostVertexCapacity(5, true), 9u);
  SR_CHECK_EQ(PostVertexCapacity(2, true), 0u);
  SR_CHECK_EQ(PostVertexCapacity(UINT32_MAX, true), 0u);
}
