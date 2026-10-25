#include "../../port/src/graphics/geometry_submission_probe.h"
#include "test_main.h"
#include <array>
#include <cmath>
#include <cstring>

using namespace superman_returns::graphics;

SR_TEST(geometry_probe_tracks_submission_kinds_and_surface_correspondence) {
  GeometrySubmissionProbe<8> probe;
  probe.Reset(701, 700);
  GeometrySubmission vertices{};
  vertices.kind = GeometrySubmissionKind::kVertices;
  vertices.surface_draw = 0;
  vertices.primitive = 4;
  vertices.elements = 33;
  vertices.vs_hash = 0xAA;
  vertices.stream_count = 1;
  vertices.streams[0] = {0x111, 0, 0x2000, 1024, 32, 0, 0, 0};
  probe.Observe(vertices);

  GeometrySubmission indexed = vertices;
  indexed.kind = GeometrySubmissionKind::kIndexed;
  indexed.surface_draw = 1;
  indexed.index = {0x222, 0, 0x4000, 512, 2, 0, 0, 1};
  indexed.index32 = false;
  indexed.elements = 96;
  probe.Observe(indexed);

  GeometrySubmission inlined{};
  inlined.kind = GeometrySubmissionKind::kInline;
  inlined.surface_draw = 2;
  inlined.streams[0] = {0, 0, 0x6000, 160, 16, 0, 0, 0};
  probe.Observe(inlined);

  SR_CHECK_EQ(probe.frame(), 701);
  SR_CHECK_EQ(probe.guest_swap(), 700);
  SR_CHECK_EQ(probe.total(), 3);
  SR_CHECK_EQ(probe.count(GeometrySubmissionKind::kIndexed), 1);
  SR_CHECK_EQ(probe.count(GeometrySubmissionKind::kVertices), 1);
  SR_CHECK_EQ(probe.count(GeometrySubmissionKind::kInline), 1);
  SR_CHECK_EQ(probe.sample_count(), 3);
  SR_CHECK_EQ(probe.sample(1).surface_draw, 1);
  SR_CHECK_EQ(probe.sample(1).index.guest_address, 0x4000);
  SR_CHECK(!probe.sample(1).index.HasVersionedHostContent());
}

SR_TEST(geometry_probe_bounded_sampling_still_counts_draws_and_resets) {
  GeometrySubmissionProbe<4> probe;
  probe.Reset(11, 10);
  GeometrySubmission submission{};
  submission.kind = GeometrySubmissionKind::kIndexed;
  for (uint32_t i = 0; i < 300; ++i) {
    submission.surface_draw = i;
    probe.Observe(submission);
  }
  SR_CHECK_EQ(probe.total(), 300);
  SR_CHECK_EQ(probe.sample_count(), 4);
  SR_CHECK_EQ(probe.count(GeometrySubmissionKind::kIndexed), 300);
  SR_CHECK_EQ(probe.sample(0).surface_draw, 0);
  SR_CHECK_EQ(probe.sample(3).surface_draw, 4);
  probe.Reset(12, 11);
  SR_CHECK_EQ(probe.total(), 0);
  SR_CHECK_EQ(probe.sample_count(), 0);
  SR_CHECK_EQ(probe.frame(), 12);
}

SR_TEST(geometry_probe_requires_nonzero_generation_and_host_content_version) {
  GeometryBufferEvidence evidence{0x12, 0, 0x3000, 4096, 32, 0, 0, 2};
  SR_CHECK(!evidence.HasVersionedHostContent());
  evidence.host_generation = 2;
  SR_CHECK(!evidence.HasVersionedHostContent());
  evidence.content_version = 3;
  SR_CHECK(evidence.HasVersionedHostContent());
  evidence.guest_key = 0;
  SR_CHECK(!evidence.HasVersionedHostContent());
}

SR_TEST(geometry_probe_correlates_only_the_current_surface_observation) {
  SR_CHECK_EQ(CorrelateSurfaceDraw(42, 43), 42);
  SR_CHECK_EQ(CorrelateSurfaceDraw(42, 42), GeometrySubmission::kNoSurfaceDraw);
  SR_CHECK_EQ(CorrelateSurfaceDraw(42, 44), GeometrySubmission::kNoSurfaceDraw);
  SR_CHECK_EQ(CorrelateSurfaceDraw(0, 0), GeometrySubmission::kNoSurfaceDraw);
}

SR_TEST(geometry_probe_accepts_only_versioned_direct_triangle_inputs_for_position_decode) {
  GeometrySubmission draw{};
  draw.gpu_topology = GeometryGpuTopology::kTriangleList;
  draw.gpu_index_source = GeometryIndexSource::kGuest;
  draw.gpu_elements = 120;
  draw.stream_count = 1;
  draw.streams[0] = {0x12, 3, 0x1000, 4000, 32, 0, 4, 0};
  draw.index = {0x13, 5, 0x3000, 512, 2, 0, 6, 0};
  SR_CHECK_EQ(ClassifyDirectTriangleInput(draw), DirectTriangleInputStatus::kCandidate);

  draw.gpu_index_source = GeometryIndexSource::kGenerated;
  SR_CHECK_EQ(ClassifyDirectTriangleInput(draw), DirectTriangleInputStatus::kGeneratedGeometry);
  draw.gpu_index_source = GeometryIndexSource::kGuest;
  draw.index.content_version = 0;
  SR_CHECK_EQ(ClassifyDirectTriangleInput(draw), DirectTriangleInputStatus::kUnversionedIndex);
  draw.index.content_version = 6;
  draw.streams[0].content_version = 0;
  SR_CHECK_EQ(ClassifyDirectTriangleInput(draw), DirectTriangleInputStatus::kUnversionedStream);
  draw.streams[0].content_version = 4;

  draw.gpu_index_source = GeometryIndexSource::kNone;
  draw.gpu_topology = GeometryGpuTopology::kTriangleStrip;
  SR_CHECK_EQ(ClassifyDirectTriangleInput(draw), DirectTriangleInputStatus::kCandidate);
  draw.gpu_topology = GeometryGpuTopology::kLineStrip;
  SR_CHECK_EQ(ClassifyDirectTriangleInput(draw), DirectTriangleInputStatus::kNonTriangle);
}

SR_TEST(geometry_probe_rejects_unobserved_streams_and_cpu_expanded_rectangles) {
  GeometrySubmission draw{};
  draw.gpu_topology = GeometryGpuTopology::kTriangleList;
  draw.gpu_index_source = GeometryIndexSource::kNone;
  draw.gpu_elements = 6;
  SR_CHECK_EQ(ClassifyDirectTriangleInput(draw), DirectTriangleInputStatus::kMissingStreams);
  draw.stream_count = 3;
  SR_CHECK_EQ(ClassifyDirectTriangleInput(draw), DirectTriangleInputStatus::kUnobservedStreams);
  draw.stream_count = 1;
  draw.streams[0] = {0x12, 1, 0x1000, 96, 16, 0, 2, 0};
  SR_CHECK_EQ(ClassifyDirectTriangleInput(draw), DirectTriangleInputStatus::kCandidate);
  draw.gpu_vertices_expanded = true;
  SR_CHECK_EQ(ClassifyDirectTriangleInput(draw), DirectTriangleInputStatus::kGeneratedGeometry);
  draw.gpu_vertices_expanded = false;
  draw.gpu_elements = 2;
  SR_CHECK_EQ(ClassifyDirectTriangleInput(draw), DirectTriangleInputStatus::kTooFewElements);
}

SR_TEST(geometry_probe_preserves_late_first_position_candidate) {
  GeometrySubmissionProbe<32> probe;
  probe.Reset(2001, 2000);
  GeometrySubmission draw{};
  draw.kind = GeometrySubmissionKind::kIndexed;
  draw.gpu_topology = GeometryGpuTopology::kTriangleList;
  draw.gpu_index_source = GeometryIndexSource::kGuest;
  draw.gpu_elements = 120;
  draw.stream_count = 1;
  draw.streams[0] = {0x10, 1, 0x1000, 4096, 32, 0, 1, 0};
  draw.index = {0x20, 2, 0x2000, 512, 2, 0, 0, 0};
  for (uint32_t i = 0; i < 300; ++i) {
    draw.surface_draw = i;
    probe.Observe(draw);
  }
  draw.surface_draw = 300;
  draw.index.content_version = 3;
  probe.Observe(draw);

  SR_CHECK_EQ(probe.total(), 301);
  SR_CHECK_EQ(probe.count(GeometrySubmissionKind::kIndexed), 301);
  SR_CHECK_EQ(probe.status_count(DirectTriangleInputStatus::kUnversionedIndex), 300);
  SR_CHECK_EQ(probe.status_count(DirectTriangleInputStatus::kCandidate), 1);
  bool found_candidate = false;
  for (size_t i = 0; i < probe.sample_count(); ++i)
    if (probe.sample(i).surface_draw == 300) found_candidate = true;
  SR_CHECK(found_candidate);
  probe.Reset(2002, 2001);
  SR_CHECK_EQ(probe.status_count(DirectTriangleInputStatus::kCandidate), 0);
  SR_CHECK_EQ(probe.status_count(DirectTriangleInputStatus::kUnversionedIndex), 0);
}

SR_TEST(geometry_probe_validates_position_zero_declaration_and_stream_bounds) {
  GeometrySubmission draw{};
  draw.gpu_topology = GeometryGpuTopology::kTriangleList;
  draw.gpu_index_source = GeometryIndexSource::kNone;
  draw.gpu_elements = 12;
  draw.stream_count = 2;
  draw.streams[0] = {0x12, 2, 0x1000, 512, 32, 0, 1, 0, 0x100, 0};
  draw.streams[1] = {0x13, 2, 0x2000, 512, 24, 0, 1, 0, 0x100, 3};

  using guest::VertexAttribute;
  std::array<VertexAttribute, 2> attributes{{
      {3, 4, 0x2A23B9, 0, 0}, {0, 8, 0x2C83A4, 3, 0}}};
  auto layout = InspectPositionLayout(draw, attributes);
  SR_CHECK_EQ(layout.status, PositionLayoutStatus::kFloat3);
  SR_CHECK_EQ(layout.stream_slot, 3u);
  SR_CHECK_EQ(layout.type, 0x2A23B9u);
  SR_CHECK_EQ(layout.offset, 4u);
  SR_CHECK_EQ(layout.stride, 24u);

  attributes[0].type = 0x1A23A6;
  SR_CHECK_EQ(InspectPositionLayout(draw, attributes).status, PositionLayoutStatus::kFloat4);
  attributes[0].type = 0x2C23A5;
  SR_CHECK_EQ(InspectPositionLayout(draw, attributes).status, PositionLayoutStatus::kOtherEncoded);
  attributes[0].type = 0x12345678;
  SR_CHECK_EQ(InspectPositionLayout(draw, attributes).status, PositionLayoutStatus::kUnsupportedType);
  attributes[0].type = 0x2A23B9;
  attributes[0].offset = 20;
  SR_CHECK_EQ(InspectPositionLayout(draw, attributes).status, PositionLayoutStatus::kOutOfBounds);
  attributes[0].offset = 4;
  draw.streams[1].bytes = 5;
  SR_CHECK_EQ(InspectPositionLayout(draw, attributes).status, PositionLayoutStatus::kOutOfBounds);
  draw.streams[1].bytes = 512;
  attributes[0].stream = 7;
  SR_CHECK_EQ(InspectPositionLayout(draw, attributes).status, PositionLayoutStatus::kMissingStream);
  attributes[0].usage = 5;
  SR_CHECK_EQ(InspectPositionLayout(draw, attributes).status, PositionLayoutStatus::kMissingPosition);
}

SR_TEST(geometry_probe_counts_position_layouts_without_accepting_generated_draws) {
  GeometrySubmissionProbe<32> probe;
  probe.Reset(1700, 1699);
  GeometrySubmission draw{};
  draw.gpu_topology = GeometryGpuTopology::kTriangleList;
  draw.gpu_elements = 6;
  draw.stream_count = 1;
  draw.streams[0] = {0x12, 2, 0x1000, 512, 24, 0, 1, 0, 0x100, 0};
  std::array<guest::VertexAttribute, 1> position{{{0, 0, 0x2A23B9, 0, 0}}};
  draw.position_layout = InspectPositionLayout(draw, position);
  probe.Observe(draw);
  draw.gpu_index_source = GeometryIndexSource::kGenerated;
  probe.Observe(draw);
  SR_CHECK_EQ(probe.position_count(PositionLayoutStatus::kFloat3), 1u);
  SR_CHECK_EQ(probe.position_count(PositionLayoutStatus::kNotChecked), 1u);
  probe.Reset(1701, 1700);
  SR_CHECK_EQ(probe.position_count(PositionLayoutStatus::kFloat3), 0u);
}

SR_TEST(geometry_probe_checks_draw_window_relative_to_bound_view_and_backing_buffer) {
  GeometrySubmission draw{};
  draw.gpu_topology = GeometryGpuTopology::kTriangleList;
  draw.gpu_elements = 3;
  draw.gpu_first_element = 4;
  draw.stream_count = 1;
  draw.streams[0] = {0x12, 2, 0x1000, 100, 16, 32, 1, 2, 0x100, 0};
  draw.streams[0].buffer_bytes = 132;
  std::array<guest::VertexAttribute, 1> attrs{{{0, 4, 0x2A23B9, 0, 0}}};
  SR_CHECK_EQ(InspectPositionLayout(draw, attrs).status, PositionLayoutStatus::kFloat3);
  draw.position_layout = InspectPositionLayout(draw, attrs);
  SR_CHECK_EQ(InspectPositionAccess(draw), PositionAccessStatus::kWindowTooSmall);
  draw.gpu_first_element = 3;
  SR_CHECK_EQ(InspectPositionAccess(draw), PositionAccessStatus::kNonIndexedRangeValid);
  draw.gpu_first_element = UINT32_MAX;
  draw.gpu_elements = UINT32_MAX;
  SR_CHECK_EQ(InspectPositionAccess(draw), PositionAccessStatus::kWindowTooSmall);
  draw.gpu_elements = 3;
  draw.streams[0].buffer_bytes = 131;
  SR_CHECK_EQ(InspectPositionAccess(draw), PositionAccessStatus::kBackingTooSmall);
  draw.streams[0].buffer_bytes = 132;
  draw.gpu_index_source = GeometryIndexSource::kGuest;
  draw.gpu_first_element = 63;
  draw.index.bytes = 128;
  draw.index.offset = 8;
  draw.index.buffer_bytes = 136;
  draw.gpu_index32 = false;
  SR_CHECK_EQ(InspectPositionAccess(draw), PositionAccessStatus::kIndexWindowTooSmall);
  draw.gpu_first_element = 40;
  SR_CHECK_EQ(InspectPositionAccess(draw), PositionAccessStatus::kIndexValuesNeeded);
  draw.index.buffer_bytes = 135;
  SR_CHECK_EQ(InspectPositionAccess(draw), PositionAccessStatus::kBackingTooSmall);
  draw.index.buffer_bytes = 136;
  draw.gpu_index32 = true;
  SR_CHECK_EQ(InspectPositionAccess(draw), PositionAccessStatus::kIndexWindowTooSmall);
}

SR_TEST(geometry_probe_counts_access_without_confusing_unread_index_values_with_valid_vertices) {
  GeometrySubmissionProbe<8> probe;
  probe.Reset(92, 91);
  GeometrySubmission draw{};
  draw.kind = GeometrySubmissionKind::kIndexed;
  draw.position_layout.status = PositionLayoutStatus::kFloat3;
  draw.position_access = PositionAccessStatus::kIndexValuesNeeded;
  probe.Observe(draw);
  draw.position_access = PositionAccessStatus::kNonIndexedRangeValid;
  probe.Observe(draw);
  SR_CHECK_EQ(probe.access_count(PositionAccessStatus::kIndexValuesNeeded), 1u);
  SR_CHECK_EQ(probe.access_count(PositionAccessStatus::kNonIndexedRangeValid), 1u);
  SR_CHECK_EQ(probe.value_count(PositionValueStatus::kNotSampled), 2u);
  probe.Reset(93, 92);
  SR_CHECK_EQ(probe.access_count(PositionAccessStatus::kIndexValuesNeeded), 0u);
  SR_CHECK_EQ(probe.value_count(PositionValueStatus::kNotSampled), 0u);
}

SR_TEST(geometry_probe_decodes_little_endian_host_float3_and_rejects_bad_values) {
  std::array<uint8_t, 24> vertex{};
  const float values[3] = {3.25f, -7.5f, 20.0f};
  std::memcpy(vertex.data() + 4, values, sizeof(values));
  auto position = DecodeHostFloat3(vertex, 4);
  SR_CHECK_EQ(position.status, PositionValueStatus::kDecoded);
  SR_CHECK_EQ(position.x, values[0]);
  SR_CHECK_EQ(position.y, values[1]);
  SR_CHECK_EQ(position.z, values[2]);
  SR_CHECK_EQ(DecodeHostFloat3(vertex, 13).status, PositionValueStatus::kOutOfBounds);
  const float inf = INFINITY;
  std::memcpy(vertex.data() + 4, &inf, sizeof(inf));
  SR_CHECK_EQ(DecodeHostFloat3(vertex, 4).status, PositionValueStatus::kNonFinite);
}

SR_TEST(geometry_probe_samples_a_view_relative_position_only_from_a_valid_full_host_buffer) {
  // IA view begins 32 bytes into the resident 132-byte host buffer.
  SR_CHECK_EQ(HostPositionSampleOffset(32, 100, 132, 16, 3, 4, 12), 84u);
  SR_CHECK_EQ(HostPositionSampleOffset(32, 100, 132, 16, 6, 4, 12), UINT32_MAX);
  SR_CHECK_EQ(HostPositionSampleOffset(32, 100, 131, 16, 3, 4, 12), UINT32_MAX);
  SR_CHECK_EQ(HostPositionSampleOffset(32, 100, 132, 12, 0, 4, 12), UINT32_MAX);
  SR_CHECK_EQ(HostPositionSampleOffset(32, 100, 132, 0, 0, 4, 12), UINT32_MAX);
  SR_CHECK_EQ(HostPositionSampleOffset(32, 100, 132, 16, UINT32_MAX, 4, 12),
              UINT32_MAX);
  std::array<uint8_t, 132> buffer{};
  const float raw[3]{11.5f, -3.0f, 0.125f};
  std::memcpy(buffer.data() + 84, raw, sizeof(raw));
  const auto value = DecodeHostFloat3(buffer, HostPositionSampleOffset(32, 100, 132, 16, 3, 4, 12));
  SR_CHECK_EQ(value.status, PositionValueStatus::kDecoded);
  SR_CHECK_EQ(value.x, raw[0]);
  SR_CHECK_EQ(value.y, raw[1]);
  SR_CHECK_EQ(value.z, raw[2]);
}

SR_TEST(geometry_probe_applies_base_vertex_and_host_strip_cut_to_sample_index) {
  SR_CHECK_EQ(ResolveSampleIndex(15, -5, false, false).vertex_index, 10u);
  SR_CHECK_EQ(ResolveSampleIndex(2, -5, false, false).status,
              PositionValueStatus::kOutOfBounds);
  SR_CHECK_EQ(ResolveSampleIndex(0xFFFF, 0, false, true).status,
              PositionValueStatus::kStripRestart);
  SR_CHECK_EQ(ResolveSampleIndex(0xFFFF, 0, true, true).vertex_index, 0xFFFFu);
  SR_CHECK_EQ(ResolveSampleIndex(0xFFFFFFFF, 0, true, true).status,
              PositionValueStatus::kStripRestart);
  SR_CHECK_EQ(ResolveSampleIndex(0x00FFFFFE, 1, true, true).vertex_index, 0x00FFFFFFu);
}
