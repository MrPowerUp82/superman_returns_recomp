#pragma once
// AUD-01: per-class temporal acceptance/rejection telemetry for M1.
// Pure bookkeeping; never changes what history is accepted.
#include <array>
#include <cstddef>
#include <cstdint>
#include "temporal_geometry_history.h"

namespace superman_returns::graphics {

enum class TemporalMotionClass : uint8_t {
  StaticWorld = 0, SkinnedCharacter, DynamicCloth, VehicleBatch,
  RigidAsset, RigidCommon, Unqualified, Count
};

inline const char* TemporalMotionClassName(TemporalMotionClass c) {
  switch (c) {
    case TemporalMotionClass::StaticWorld: return "static_world";
    case TemporalMotionClass::SkinnedCharacter: return "skinned";
    case TemporalMotionClass::DynamicCloth: return "cloth";
    case TemporalMotionClass::VehicleBatch: return "vehicle_batch";
    case TemporalMotionClass::RigidAsset: return "rigid_asset";
    case TemporalMotionClass::RigidCommon: return "rigid_common";
    default: return "unqualified";
  }
}

// rigid_source follows native_motion_capture.inc: 1 observed asset, 2 common,
// 3 single instance, 4 vehicle batch. Skin/cloth are flagged by the caller
// (IsObservedSkinnedMotionShader / cloth shader).
inline TemporalMotionClass ClassifyTemporalMotionSource(bool skinned, bool cloth, uint8_t rigid_source,
                                                        GeometryHistoryProof proof) {
  if (proof == GeometryHistoryProof::kStaticWorldInputs) return TemporalMotionClass::StaticWorld;
  if (proof != GeometryHistoryProof::kVerifiedInstance) return TemporalMotionClass::Unqualified;
  if (skinned) return TemporalMotionClass::SkinnedCharacter;
  if (cloth) return TemporalMotionClass::DynamicCloth;
  if (rigid_source == 4) return TemporalMotionClass::VehicleBatch;
  if (rigid_source == 1 || rigid_source == 3) return TemporalMotionClass::RigidAsset;
  if (rigid_source == 2) return TemporalMotionClass::RigidCommon;
  return TemporalMotionClass::Unqualified;
}

struct TemporalClassMetrics {
  uint64_t draws = 0, vertices = 0, matched_draws = 0, matched_vertices = 0;
  uint64_t rejected_ambiguous = 0, rejected_missing = 0, rejected_unverified = 0;
  double DrawAcceptance() const { return draws ? double(matched_draws) / double(draws) : 0.0; }
  double VertexAcceptance() const { return vertices ? double(matched_vertices) / double(vertices) : 0.0; }
};

struct TemporalClassCoverageStats {
  std::array<TemporalClassMetrics, size_t(TemporalMotionClass::Count)> per_class{};
  const TemporalClassMetrics& operator[](TemporalMotionClass c) const { return per_class[size_t(c)]; }
  void Record(TemporalMotionClass c, uint32_t vertices, GeometryHistoryStatus status) {
    auto& m = per_class[size_t(c)];
    ++m.draws; m.vertices += vertices;
    switch (status) {
      case GeometryHistoryStatus::kMatch: ++m.matched_draws; m.matched_vertices += vertices; break;
      case GeometryHistoryStatus::kAmbiguous: ++m.rejected_ambiguous; break;
      case GeometryHistoryStatus::kMissing: ++m.rejected_missing; break;
      default: ++m.rejected_unverified; break;
    }
  }
  uint64_t TotalDraws() const { uint64_t t = 0; for (auto& m : per_class) t += m.draws; return t; }
  uint64_t TotalMatchedDraws() const { uint64_t t = 0; for (auto& m : per_class) t += m.matched_draws; return t; }
};

}  // namespace superman_returns::graphics
