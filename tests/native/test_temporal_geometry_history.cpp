#include "test_main.h"
#include "../../port/src/graphics/temporal_geometry_history.h"
#include <bit>
#include <limits>
using namespace superman_returns::graphics;
SR_TEST(temporal_projection_signature_includes_actual_screen_and_half_pixel_fixups) {
  std::array<uint32_t,16> matrix{};
  std::array<uint32_t,6> fixup{};fixup[0]=fixup[1]=std::bit_cast<uint32_t>(1.f);
  const auto first=CaptureTemporalProjectionInputs(matrix,fixup);
  for(uint32_t component=0;component<6;++component) {
    auto changed=fixup;changed[component]=std::bit_cast<uint32_t>(.00078125f);
    const auto next=CaptureTemporalProjectionInputs(matrix,changed);
    SR_CHECK(first!=next);
    SR_CHECK(std::equal(matrix.begin(),matrix.end(),next.begin()));
  }
}
SR_TEST(static_world_history_rejects_duplicate_views_but_accepts_camera_movement_between_frames) {
  TemporalGeometrySample s;s.mesh=1;s.camera_domain=2;s.geometry_inputs=3;
  s.proof=GeometryHistoryProof::kStaticWorldInputs;s.range={0,0,3};s.projection_inputs=10;
  TemporalGeometryHistory h;h.Begin(1,1280,720,false);h.Add(s);h.Seal();h.End();
  h.Begin(2,1280,720,false);s.range={1,48,3};s.projection_inputs=20;auto index=h.Add(s);h.Seal();
  SR_CHECK(h.Resolve(*index).status==GeometryHistoryStatus::kMatch);h.End();
  h.Begin(3,1280,720,false);s.range={2,0,3};s.projection_inputs=30;index=h.Add(s);
  s.range.offset=48;s.projection_inputs=31;h.Add(s);h.Seal();
  SR_CHECK(h.Resolve(*index).status==GeometryHistoryStatus::kAmbiguous);h.End();
  h.Begin(4,1280,720,false);s.range={0,0,3};s.projection_inputs=40;index=h.Add(s);h.Seal();
  SR_CHECK(h.Resolve(*index).status==GeometryHistoryStatus::kAmbiguous);
}
SR_TEST(static_world_range_ignores_unread_constants_but_keeps_reachable_matrix_and_remap) {
  std::array<uint32_t,1024> words{};words[16]=std::bit_cast<uint32_t>(100.f);
  std::array<uint32_t,636> a{},b{};
  SR_CHECK(CaptureStaticWorldDependenciesInVertexRange(0x2C45BFFE76D77398ull,words,1,50,a));
  words[98*4]=std::bit_cast<uint32_t>(9.f);words[250*4]=std::bit_cast<uint32_t>(7.f);words[18]=std::bit_cast<uint32_t>(3.f);
  SR_CHECK(CaptureStaticWorldDependenciesInVertexRange(0x2C45BFFE76D77398ull,words,1,50,b));SR_CHECK(a==b);
  words[100*4+3]=std::bit_cast<uint32_t>(1477349.875f);
  words[101*4+3]=std::bit_cast<uint32_t>(9.f);
  SR_CHECK(CaptureStaticWorldDependenciesInVertexRange(0x2C45BFFE76D77398ull,words,1,50,b));SR_CHECK(a==b);
  words[100*4]=std::bit_cast<uint32_t>(1.f);
  SR_CHECK(CaptureStaticWorldDependenciesInVertexRange(0x2C45BFFE76D77398ull,words,1,50,b));SR_CHECK(a!=b);
  words[100*4]=0;words[17]=std::bit_cast<uint32_t>(1.f);
  SR_CHECK(CaptureStaticWorldDependenciesInVertexRange(0x2C45BFFE76D77398ull,words,1,50,b));SR_CHECK(a!=b);
}
SR_TEST(static_world_range_covers_float_reciprocal_boundary_and_rejects_wrapped_vertex_ids) {
  std::array<uint32_t,1024> words{};words[16]=std::bit_cast<uint32_t>(100.5f);
  std::array<uint32_t,636> a{},b{};
  SR_CHECK(CaptureStaticWorldDependenciesInVertexRange(0x2C45BFFE76D77398ull,words,100,100,a));
  words[100*4]=std::bit_cast<uint32_t>(1.f);
  SR_CHECK(CaptureStaticWorldDependenciesInVertexRange(0x2C45BFFE76D77398ull,words,100,100,b));SR_CHECK(a!=b);
  words[100*4]=0;words[102*4]=std::bit_cast<uint32_t>(1.f);
  SR_CHECK(CaptureStaticWorldDependenciesInVertexRange(0x2C45BFFE76D77398ull,words,100,100,b));SR_CHECK(a!=b);
  SR_CHECK(!CaptureStaticWorldDependenciesInVertexRange(0x2C45BFFE76D77398ull,words,20,10,b));
  SR_CHECK(!CaptureStaticWorldDependenciesInVertexRange(0x2C45BFFE76D77398ull,words,0,0x1000000,b));
  words[16]=std::bit_cast<uint32_t>(.5f);
  SR_CHECK(!CaptureStaticWorldDependenciesInVertexRange(0x2C45BFFE76D77398ull,words,0,1,b));
}
SR_TEST(temporal_geometry_history_does_not_infer_object_identity_from_mesh_or_draw_order) {
  TemporalGeometryHistory history;
  TemporalGeometrySample sample; sample.mesh=44;sample.camera_domain=1;
  sample.range={0,0,3};sample.geometry_inputs=100;
  history.Begin(1,1280,720,false);auto first=history.Add(sample);history.Seal();history.End();
  history.Begin(2,1280,720,false);auto next=history.Add(sample);history.Seal();
  SR_CHECK(first.has_value() && next.has_value());
  SR_CHECK(history.Resolve(*next).status==GeometryHistoryStatus::kUnverifiedIdentity);
  history.End();
  sample.proof=GeometryHistoryProof::kVerifiedInstance; sample.instance=77;sample.instance_generation=1;
  history.Begin(3,1280,720,false);history.Add(sample);history.Seal();history.End();
  sample.geometry_inputs=200;sample.range={1,64,3}; // Genuine deformation/movement may change inputs.
  history.Begin(4,1280,720,false);next=history.Add(sample);history.Seal();
  SR_CHECK(history.Resolve(*next).status==GeometryHistoryStatus::kMatch);
  SR_CHECK_EQ(history.Resolve(*next).previous.offset,0u);
}
SR_TEST(temporal_geometry_history_requires_sealing_and_rejects_ambiguous_instances) {
  TemporalGeometryHistory history;
  TemporalGeometrySample s; s.proof=GeometryHistoryProof::kVerifiedInstance;
  s.mesh=1;s.instance=2;s.instance_generation=3;s.camera_domain=4;s.geometry_inputs=5;s.range={0,0,3};
  history.Begin(1,8,8,false);history.Add(s);history.Seal();history.End();
  history.Begin(2,8,8,false);auto index=history.Add(s);
  SR_CHECK(history.Resolve(*index).status==GeometryHistoryStatus::kFrameNotSealed);
  s.geometry_inputs=6;history.Add(s);history.Seal();
  SR_CHECK(history.Resolve(*index).status==GeometryHistoryStatus::kAmbiguous);
}
SR_TEST(temporal_geometry_history_invalidates_frame_gaps_resize_and_generation_reuse) {
  TemporalGeometrySample s;s.proof=GeometryHistoryProof::kVerifiedInstance;
  s.mesh=1;s.instance=2;s.instance_generation=3;s.camera_domain=4;s.geometry_inputs=5;s.range={0,0,3};
  TemporalGeometryHistory h;h.Begin(1,8,8,false);h.Add(s);h.Seal();h.End();
  h.Begin(3,8,8,false);auto i=h.Add(s);h.Seal();SR_CHECK(h.Resolve(*i).status==GeometryHistoryStatus::kNoPreviousFrame);h.End();
  h.Begin(4,9,8,false);i=h.Add(s);h.Seal();SR_CHECK(h.Resolve(*i).status==GeometryHistoryStatus::kNoPreviousFrame);h.End();
  h.Begin(5,9,8,false);++s.instance_generation;i=h.Add(s);h.Seal();
  SR_CHECK(h.Resolve(*i).status==GeometryHistoryStatus::kMissing);
}
SR_TEST(static_world_proof_hashes_actual_position_dependencies_not_camera_or_shadow_projection) {
  std::array<uint32_t,1024> words{};words[16]=std::bit_cast<uint32_t>(10.f);
  std::array<uint32_t,636> a{},b{};
  SR_CHECK(CaptureStaticWorldDependencies(0x2C45BFFE76D77398ull,words,a));
  words[0]=std::bit_cast<uint32_t>(7.f);words[9*4]=std::bit_cast<uint32_t>(3.f);
  SR_CHECK(CaptureStaticWorldDependencies(0x2C45BFFE76D77398ull,words,b));SR_CHECK(a==b);
  words[98*4]=std::bit_cast<uint32_t>(1.f);
  SR_CHECK(CaptureStaticWorldDependencies(0x2C45BFFE76D77398ull,words,b));SR_CHECK(a!=b);
  words[16]=std::bit_cast<uint32_t>(.5f);
  SR_CHECK(!CaptureStaticWorldDependencies(0x2C45BFFE76D77398ull,words,b));
  words[16]=std::bit_cast<uint32_t>(10.f);words[1023]=std::bit_cast<uint32_t>(std::numeric_limits<float>::infinity());
  SR_CHECK(!CaptureStaticWorldDependencies(0x2C45BFFE76D77398ull,words,b));
}
