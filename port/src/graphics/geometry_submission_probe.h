#pragma once

// Allocation-free, per-frame evidence of D3D12 submissions. A guest buffer
// address/key or a shader hash alone cannot identify a persistent RT object.
// A host buffer generation/version is useful evidence for detecting changes,
// but does not identify the game's logical mesh/instance or prove BLAS reuse.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include "guest/vertex_layout.h"

namespace superman_returns::graphics {

enum class GeometrySubmissionKind : uint8_t { kVertices, kIndexed, kInline };

// Post-expansion topology and index source actually submitted to D3D12.
// This is distinct from the original Xenos primitive and index buffer.
enum class GeometryGpuTopology : uint8_t {
  kUnknown, kPointList, kLineList, kLineStrip, kTriangleList, kTriangleStrip
};
enum class GeometryIndexSource : uint8_t { kNone, kGuest, kGenerated };

inline const char* GeometryGpuTopologyName(GeometryGpuTopology topology) {
  switch (topology) {
    case GeometryGpuTopology::kPointList: return "points";
    case GeometryGpuTopology::kLineList: return "lines";
    case GeometryGpuTopology::kLineStrip: return "line_strip";
    case GeometryGpuTopology::kTriangleList: return "triangles";
    case GeometryGpuTopology::kTriangleStrip: return "triangle_strip";
    default: return "unknown";
  }
}

inline const char* GeometryIndexSourceName(GeometryIndexSource source) {
  switch (source) {
    case GeometryIndexSource::kNone: return "none";
    case GeometryIndexSource::kGuest: return "guest";
    case GeometryIndexSource::kGenerated: return "generated";
  }
  return "unknown";
}

inline const char* GeometrySubmissionKindName(GeometrySubmissionKind kind) {
  switch (kind) {
    case GeometrySubmissionKind::kVertices: return "vertices";
    case GeometrySubmissionKind::kIndexed: return "indexed";
    case GeometrySubmissionKind::kInline: return "inline";
  }
  return "unknown";
}

struct GeometryBufferEvidence {
  uint64_t guest_key = 0;
  uint64_t host_generation = 0;
  uint32_t guest_address = 0;
  uint32_t bytes = 0;
  uint32_t stride = 0;
  uint32_t offset = 0;
  uint64_t content_version = 0;
  uint8_t planned_action = 0;
  uint32_t vertex_declaration = 0;
  uint32_t stream_slot = 0;
  // Whole resident host buffer size, distinct from `bytes` (the IA view size).
  uint32_t buffer_bytes = 0;

  bool HasVersionedHostContent() const {
    return guest_key && host_generation && content_version;
  }
};

// POSITION0 declaration evidence only. Layout and host version do not prove
// decoded position values, world transforms, skinning or BLAS eligibility.
enum class PositionLayoutStatus : uint8_t {
  kNotChecked, kMissingPosition, kMissingStream, kUnsupportedType,
  kOutOfBounds, kOtherEncoded, kFloat3, kFloat4
};

inline const char* PositionLayoutStatusName(PositionLayoutStatus status) {
  switch (status) {
    case PositionLayoutStatus::kNotChecked: return "not_checked";
    case PositionLayoutStatus::kMissingPosition: return "missing_position0";
    case PositionLayoutStatus::kMissingStream: return "missing_position_stream";
    case PositionLayoutStatus::kUnsupportedType: return "unsupported_position_type";
    case PositionLayoutStatus::kOutOfBounds: return "position_out_of_bounds";
    case PositionLayoutStatus::kOtherEncoded: return "other_encoded_position";
    case PositionLayoutStatus::kFloat3: return "float3_layout";
    case PositionLayoutStatus::kFloat4: return "float4_layout";
  }
  return "unknown";
}

struct PositionLayoutEvidence {
  PositionLayoutStatus status = PositionLayoutStatus::kNotChecked;
  uint32_t stream_slot = 0, type = 0, offset = 0, stride = 0;
};

// An indexed draw can have a valid index *window* while the values within
// that window still refer to vertices outside the view. Such a draw is not
// cleared for RT extraction by kIndexValuesNeeded.
enum class PositionAccessStatus : uint8_t {
  kNotChecked, kBackingTooSmall, kIndexWindowTooSmall, kWindowTooSmall,
  kIndexValuesNeeded, kNonIndexedRangeValid
};
inline const char* PositionAccessStatusName(PositionAccessStatus status) {
  switch (status) {
    case PositionAccessStatus::kNotChecked: return "not_checked";
    case PositionAccessStatus::kBackingTooSmall: return "backing_too_small";
    case PositionAccessStatus::kIndexWindowTooSmall: return "index_window_too_small";
    case PositionAccessStatus::kWindowTooSmall: return "position_window_too_small";
    case PositionAccessStatus::kIndexValuesNeeded: return "index_values_needed";
    case PositionAccessStatus::kNonIndexedRangeValid: return "nonindexed_range_valid";
  }
  return "unknown";
}

enum class PositionValueStatus : uint8_t {
  kNotSampled, kUnavailableFreshUpload, kStripRestart, kOutOfBounds,
  kNonFinite, kDecoded
};
inline const char* PositionValueStatusName(PositionValueStatus status) {
  switch (status) {
    case PositionValueStatus::kNotSampled: return "not_sampled";
    case PositionValueStatus::kUnavailableFreshUpload: return "no_full_fresh_upload";
    case PositionValueStatus::kStripRestart: return "strip_restart";
    case PositionValueStatus::kOutOfBounds: return "out_of_bounds";
    case PositionValueStatus::kNonFinite: return "nonfinite";
    case PositionValueStatus::kDecoded: return "decoded";
  }
  return "unknown";
}
struct PositionValueEvidence {
  PositionValueStatus status = PositionValueStatus::kNotSampled;
  uint32_t vertex_index = 0;
  float x = 0, y = 0, z = 0;
};

struct GeometrySubmission {
  static constexpr uint32_t kNoSurfaceDraw = UINT32_MAX;
  static constexpr size_t kSampledStreams = 2;
  GeometrySubmissionKind kind = GeometrySubmissionKind::kVertices;
  uint32_t surface_draw = kNoSurfaceDraw;
  uint32_t primitive = 0;
  uint32_t elements = 0;
  uint32_t first_element = 0;
  int32_t base_vertex = 0;
  uint32_t guest_pass = 0;
  uint64_t vs_hash = 0, ps_hash = 0;
  bool index32 = false;
  bool raster_draw_suppressed = false;
  GeometryGpuTopology gpu_topology = GeometryGpuTopology::kUnknown;
  GeometryIndexSource gpu_index_source = GeometryIndexSource::kNone;
  uint32_t gpu_elements = 0;
  uint32_t gpu_first_element = 0;
  bool gpu_index32 = false;
  bool gpu_vertices_expanded = false;
  uint32_t stream_count = 0;
  std::array<GeometryBufferEvidence, kSampledStreams> streams{};
  GeometryBufferEvidence index{};
  PositionLayoutEvidence position_layout{};
  PositionAccessStatus position_access = PositionAccessStatus::kNotChecked;
  PositionValueEvidence position_value{};
};

// Diagnostic readiness to decode position data from direct, versioned host
// buffers. Even a candidate lacks validated position layout, transforms,
// instance lifecycle and offscreen coverage: it is NOT BLAS-ready.
enum class DirectTriangleInputStatus : uint8_t {
  kCandidate, kNonTriangle, kTooFewElements, kGeneratedGeometry,
  kMissingStreams, kUnobservedStreams, kUnversionedStream, kUnversionedIndex
};

inline const char* DirectTriangleInputStatusName(DirectTriangleInputStatus status) {
  switch (status) {
    case DirectTriangleInputStatus::kCandidate: return "position_decode_candidate";
    case DirectTriangleInputStatus::kNonTriangle: return "non_triangle";
    case DirectTriangleInputStatus::kTooFewElements: return "too_few_elements";
    case DirectTriangleInputStatus::kGeneratedGeometry: return "generated_geometry";
    case DirectTriangleInputStatus::kMissingStreams: return "missing_streams";
    case DirectTriangleInputStatus::kUnobservedStreams: return "unobserved_streams";
    case DirectTriangleInputStatus::kUnversionedStream: return "unversioned_stream";
    case DirectTriangleInputStatus::kUnversionedIndex: return "unversioned_index";
  }
  return "unknown";
}

inline DirectTriangleInputStatus ClassifyDirectTriangleInput(const GeometrySubmission& draw) {
  if (draw.gpu_topology != GeometryGpuTopology::kTriangleList &&
      draw.gpu_topology != GeometryGpuTopology::kTriangleStrip)
    return DirectTriangleInputStatus::kNonTriangle;
  if (draw.gpu_elements < 3) return DirectTriangleInputStatus::kTooFewElements;
  if (draw.gpu_vertices_expanded || draw.gpu_index_source == GeometryIndexSource::kGenerated)
    return DirectTriangleInputStatus::kGeneratedGeometry;
  if (draw.stream_count == 0) return DirectTriangleInputStatus::kMissingStreams;
  if (draw.stream_count > draw.streams.size())
    return DirectTriangleInputStatus::kUnobservedStreams;
  for (size_t i = 0; i < draw.stream_count; ++i)
    if (!draw.streams[i].HasVersionedHostContent())
      return DirectTriangleInputStatus::kUnversionedStream;
  if (draw.gpu_index_source == GeometryIndexSource::kGuest &&
      !draw.index.HasVersionedHostContent())
    return DirectTriangleInputStatus::kUnversionedIndex;
  return DirectTriangleInputStatus::kCandidate;
}

inline PositionLayoutEvidence InspectPositionLayout(
    const GeometrySubmission& draw, std::span<const guest::VertexAttribute> attributes) {
  PositionLayoutEvidence result{};
  if (ClassifyDirectTriangleInput(draw) != DirectTriangleInputStatus::kCandidate)
    return result;
  const guest::VertexAttribute* position = nullptr;
  for (const auto& attribute : attributes) {
    if (attribute.usage == 0 && attribute.index == 0) {
      position = &attribute;
      break;
    }
  }
  if (!position) {
    result.status = PositionLayoutStatus::kMissingPosition;
    return result;
  }
  result.stream_slot = position->stream;
  result.type = position->type;
  result.offset = position->offset;
  const GeometryBufferEvidence* stream = nullptr;
  for (size_t i = 0; i < draw.stream_count && i < draw.streams.size(); ++i) {
    if (draw.streams[i].stream_slot == position->stream) {
      stream = &draw.streams[i];
      break;
    }
  }
  if (!stream) {
    result.status = PositionLayoutStatus::kMissingStream;
    return result;
  }
  result.stride = stream->stride;
  const auto encoding = guest::GetVertexEncoding(position->type);
  if (encoding.size == 0) {
    result.status = PositionLayoutStatus::kUnsupportedType;
  } else if (stream->stride == 0 ||
             uint64_t(position->offset) + encoding.size > stream->stride ||
             uint64_t(position->offset) + encoding.size > stream->bytes) {
    result.status = PositionLayoutStatus::kOutOfBounds;
  } else if (position->type == 0x2A23B9) {
    result.status = PositionLayoutStatus::kFloat3;
  } else if (position->type == 0x1A23A6) {
    result.status = PositionLayoutStatus::kFloat4;
  } else {
    result.status = PositionLayoutStatus::kOtherEncoded;
  }
  return result;
}

inline PositionAccessStatus InspectPositionAccess(const GeometrySubmission& draw) {
  if (draw.position_layout.status != PositionLayoutStatus::kFloat3 &&
      draw.position_layout.status != PositionLayoutStatus::kFloat4)
    return PositionAccessStatus::kNotChecked;
  const GeometryBufferEvidence* stream = nullptr;
  for (size_t i = 0; i < draw.stream_count && i < draw.streams.size(); ++i) {
    if (draw.streams[i].stream_slot == draw.position_layout.stream_slot) {
      stream = &draw.streams[i];
      break;
    }
  }
  if (!stream || stream->buffer_bytes == 0 ||
      uint64_t(stream->offset) + stream->bytes > stream->buffer_bytes)
    return PositionAccessStatus::kBackingTooSmall;
  if (draw.gpu_index_source == GeometryIndexSource::kGuest) {
    const uint32_t width = draw.gpu_index32 ? 4 : 2;
    if (draw.index.buffer_bytes == 0 ||
        uint64_t(draw.index.offset) + draw.index.bytes > draw.index.buffer_bytes)
      return PositionAccessStatus::kBackingTooSmall;
    if ((uint64_t(draw.gpu_first_element) + draw.gpu_elements) * width > draw.index.bytes)
      return PositionAccessStatus::kIndexWindowTooSmall;
    return PositionAccessStatus::kIndexValuesNeeded;
  }
  if (draw.gpu_index_source != GeometryIndexSource::kNone || draw.gpu_elements == 0)
    return PositionAccessStatus::kNotChecked;
  const auto encoding = guest::GetVertexEncoding(draw.position_layout.type);
  const uint64_t first_position_end = uint64_t(draw.position_layout.offset) + encoding.size;
  if (!stream->stride || first_position_end > stream->bytes)
    return PositionAccessStatus::kWindowTooSmall;
  // Divide the available range before multiplying. Even adversarial draw counts
  // cannot wrap the last byte into a seemingly valid stream window.
  const uint64_t last_vertex = uint64_t(draw.gpu_first_element) + draw.gpu_elements - 1;
  return last_vertex <= (stream->bytes - first_position_end) / stream->stride
             ? PositionAccessStatus::kNonIndexedRangeValid
             : PositionAccessStatus::kWindowTooSmall;
}

// The POSITION0 location of a non-indexed draw in the already swapped, full
// host upload. The stream offset is a buffer offset; the vertex index is
// relative to the bound IA view. UINT32_MAX denotes an unavailable sample.
inline uint32_t HostPositionSampleOffset(uint32_t view_offset, uint32_t view_bytes,
                                         uint32_t backing_bytes, uint32_t stride,
                                         uint32_t vertex_index, uint32_t attribute_offset,
                                         uint32_t width) {
  if (!stride || !width || uint64_t(view_offset) + view_bytes > backing_bytes ||
      uint64_t(attribute_offset) + width > stride ||
      uint64_t(attribute_offset) + width > view_bytes)
    return UINT32_MAX;
  if (vertex_index > (view_bytes - attribute_offset - width) / stride)
    return UINT32_MAX;
  return uint32_t(uint64_t(view_offset) + uint64_t(vertex_index) * stride +
                  attribute_offset);
}

inline PositionValueEvidence DecodeHostFloat3(std::span<const uint8_t> vertex,
                                               uint32_t position_offset) {
  PositionValueEvidence result{};
  if (uint64_t(position_offset) + 3 * sizeof(float) > vertex.size()) {
    result.status = PositionValueStatus::kOutOfBounds;
    return result;
  }
  std::memcpy(&result.x, vertex.data() + position_offset, sizeof(float));
  std::memcpy(&result.y, vertex.data() + position_offset + 4, sizeof(float));
  std::memcpy(&result.z, vertex.data() + position_offset + 8, sizeof(float));
  result.status = std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z)
                      ? PositionValueStatus::kDecoded : PositionValueStatus::kNonFinite;
  return result;
}

// The input index has already been converted to host endian and any enabled
// programmable restart has already been normalized to the D3D12 cut value.
inline PositionValueEvidence ResolveSampleIndex(uint32_t index, int32_t base_vertex,
                                                bool index32, bool cut_enabled) {
  PositionValueEvidence result{};
  if (cut_enabled && index == (index32 ? UINT32_MAX : UINT16_MAX)) {
    result.status = PositionValueStatus::kStripRestart;
    return result;
  }
  const int64_t effective = int64_t(index) + base_vertex;
  if (effective < 0 || effective > UINT32_MAX) {
    result.status = PositionValueStatus::kOutOfBounds;
    return result;
  }
  result.vertex_index = uint32_t(effective);
  return result;
}

// The surface sequence records target binds within PrepareDraw, which can
// succeed even when a later step rejects the draw. Correlate only a new bind
// observation from this specific PrepareDraw invocation.
inline uint32_t CorrelateSurfaceDraw(uint32_t before, uint32_t after) {
  return after > before && after - before == 1
             ? before : GeometrySubmission::kNoSurfaceDraw;
}

template <size_t Capacity>
class GeometrySubmissionProbe {
  static_assert(Capacity > 0);
 public:
  void Reset(uint64_t frame, uint64_t guest_swap) {
    frame_ = frame;
    guest_swap_ = guest_swap;
    total_ = 0;
    counts_.fill(0);
    status_counts_.fill(0);
    position_counts_.fill(0);
    access_counts_.fill(0);
    value_counts_.fill(0);
    size_ = 0;
  }

  void Observe(const GeometrySubmission& submission) {
    const size_t kind = static_cast<size_t>(submission.kind);
    if (kind >= counts_.size()) return;
    const bool first_kind = counts_[kind]++ == 0;
    const size_t status = static_cast<size_t>(ClassifyDirectTriangleInput(submission));
    const bool first_status = status_counts_[status]++ == 0;
    const size_t layout = static_cast<size_t>(
        status == static_cast<size_t>(DirectTriangleInputStatus::kCandidate)
            ? submission.position_layout.status : PositionLayoutStatus::kNotChecked);
    const bool first_layout = position_counts_[layout]++ == 0;
    const size_t access = static_cast<size_t>(submission.position_access);
    const bool first_access = access_counts_[access]++ == 0;
    const size_t value = static_cast<size_t>(submission.position_value.status);
    const bool first_value = value_counts_[value]++ == 0;
    const uint32_t ordinal = total_++;
    // Preserve the first example of each rejection/acceptance category, even
    // when position candidates first appear late in the frame.
    // Once capacity is exhausted, counts remain exact without allocating.
    if (size_ < Capacity && (first_kind || first_status || first_layout ||
                            first_access || first_value || ordinal == 0 ||
                            (ordinal & (ordinal - 1)) == 0)) {
      samples_[size_++] = submission;
    }
  }

  uint64_t frame() const { return frame_; }
  uint64_t guest_swap() const { return guest_swap_; }
  uint32_t total() const { return total_; }
  uint32_t count(GeometrySubmissionKind kind) const {
    const size_t index = static_cast<size_t>(kind);
    return index < counts_.size() ? counts_[index] : 0;
  }
  uint32_t status_count(DirectTriangleInputStatus status) const {
    const size_t index = static_cast<size_t>(status);
    return index < status_counts_.size() ? status_counts_[index] : 0;
  }
  uint32_t position_count(PositionLayoutStatus status) const {
    const size_t index = static_cast<size_t>(status);
    return index < position_counts_.size() ? position_counts_[index] : 0;
  }
  uint32_t access_count(PositionAccessStatus status) const {
    return access_counts_[static_cast<size_t>(status)];
  }
  uint32_t value_count(PositionValueStatus status) const {
    return value_counts_[static_cast<size_t>(status)];
  }
  size_t sample_count() const { return size_; }
  const GeometrySubmission& sample(size_t index) const { return samples_[index]; }

 private:
  uint64_t frame_ = 0, guest_swap_ = 0;
  uint32_t total_ = 0;
  std::array<uint32_t, 3> counts_{};
  std::array<uint32_t, static_cast<size_t>(DirectTriangleInputStatus::kUnversionedIndex) + 1>
      status_counts_{};
  std::array<uint32_t, static_cast<size_t>(PositionLayoutStatus::kFloat4) + 1>
      position_counts_{};
  std::array<uint32_t, static_cast<size_t>(PositionAccessStatus::kNonIndexedRangeValid) + 1>
      access_counts_{};
  std::array<uint32_t, static_cast<size_t>(PositionValueStatus::kDecoded) + 1>
      value_counts_{};
  std::array<GeometrySubmission, Capacity> samples_{};
  size_t size_ = 0;
};

}  // namespace superman_returns::graphics
