#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <cmath>
#include <span>
#include "post_vertex_geometry.h"

namespace superman_returns::graphics {
enum class ObjectMotionRejection {
  kNone, kNoScene, kCapture, kCorrespondence, kCamera, kCapacity, kLineage,
  kResources, kJitter, kCoverage, kPixelValidity
};
inline const char* ObjectMotionRejectionName(ObjectMotionRejection reason) {
  switch(reason) {
    case ObjectMotionRejection::kNone:return "ready";
    case ObjectMotionRejection::kNoScene:return "no_scene";
    case ObjectMotionRejection::kCapture:return "uncaptured_draw";
    case ObjectMotionRejection::kCorrespondence:return "unverified_correspondence";
    case ObjectMotionRejection::kCamera:return "camera_conflict";
    case ObjectMotionRejection::kCapacity:return "capacity";
    case ObjectMotionRejection::kLineage:return "scene_lineage";
    case ObjectMotionRejection::kResources:return "resources";
    case ObjectMotionRejection::kJitter:return "jitter";
    case ObjectMotionRejection::kCoverage:return "blended_coverage";
    case ObjectMotionRejection::kPixelValidity:return "gpu_pixel_validity_unverified";
  }
  return "unknown";
}
// Conservative whole-frame readiness. This does not own geometry history:
// capture/history MUST continue on raster frames so the next frame can recover.
class ObjectMotionFrame {
 public:
  void Begin(uint64_t frame,uint32_t width,uint32_t height) {
    draws_=captured_=matched_=0;
    blended_=false;
    failure_=frame && width && height ? ObjectMotionRejection::kNone : ObjectMotionRejection::kNoScene;
  }
  void ExpectDraw(uint64_t serial) {
    if(!serial || draws_==expected_.size()) {Fail(ObjectMotionRejection::kCapacity);return;}
    for(uint32_t i=0;i<draws_;++i)
      if(expected_[i].serial==serial) {Fail(ObjectMotionRejection::kCapture);return;}
    expected_[draws_++]={serial,false};
  }
  void CaptureDraw(uint64_t serial,bool matched,bool single_layer) {
    for(uint32_t i=0;i<draws_;++i) {
      auto& draw=expected_[i];
      if(draw.serial!=serial) continue;
      if(draw.captured) {Fail(ObjectMotionRejection::kCapture);return;}
      draw.captured=true;++captured_;matched_+=matched;
      blended_|=!single_layer;
      return;
    }
    Fail(ObjectMotionRejection::kCapture);
  }
  void Fail(ObjectMotionRejection failure) {
    if(failure_==ObjectMotionRejection::kNone) failure_=failure;
  }
  ObjectMotionRejection Rejection() const {
    if(failure_!=ObjectMotionRejection::kNone) return failure_;
    if(!draws_) return ObjectMotionRejection::kNoScene;
    if(captured_!=draws_) return ObjectMotionRejection::kCapture;
    if(blended_) return ObjectMotionRejection::kCoverage;
    if(matched_!=draws_) return ObjectMotionRejection::kCorrespondence;
    return ObjectMotionRejection::kNone;
  }
  uint32_t draws() const {return draws_;}
  uint32_t captured() const {return captured_;}
  uint32_t matched() const {return matched_;}
 private:
  struct Draw {uint64_t serial=0;bool captured=false;};
  std::array<Draw,4096> expected_{};
  uint32_t draws_=0,captured_=0,matched_=0;
  bool blended_=false;
  ObjectMotionRejection failure_=ObjectMotionRejection::kNoScene;
};
inline ObjectMotionRejection ObjectMotionConsumerRejection(ObjectMotionRejection geometry,
    bool lineage_verified,bool gpu_pixels_verified) {
  if(geometry!=ObjectMotionRejection::kNone) return geometry;
  if(!lineage_verified) return ObjectMotionRejection::kLineage;
  if(!gpu_pixels_verified) return ObjectMotionRejection::kPixelValidity;
  return ObjectMotionRejection::kNone;
}

inline bool ProveObjectMotionIdentityUv(std::span<const std::array<float,4>> clips,
                                      std::span<const std::array<float,2>> uv) {
  if(clips.empty() || clips.size()!=uv.size() || clips.size()>6) return false;
  const float w=clips[0][3];
  if(!std::isfinite(w) || w<=0) return false;
  for(size_t i=0;i<clips.size();++i) {
    const auto& c=clips[i];const auto& t=uv[i];
    if(c[3]!=w || !std::isfinite(c[0]) || !std::isfinite(c[1]) ||
       !std::isfinite(t[0]) || !std::isfinite(t[1]) ||
       std::abs(t[0]-(c[0]/w*.5f+.5f))>1e-6f ||
       std::abs(t[1]-(.5f-c[1]/w*.5f))>1e-6f) return false;
  }
  return true;
}

// Conservative discontinuity heuristic in normalized screen space, independent
// of world-coordinate scale. Scene lifetime/frame-gap changes reset separately.
inline bool ObjectMotionCameraJump(const Matrix4& previous,const Matrix4& current) {
  Matrix4 inverse{};
  if(!InvertProjection(current,inverse)) return true;
  for(float z:{.25f,.75f}) for(float x:{-1.f,1.f}) for(float y:{-1.f,1.f}) {
    auto world=TransformColumn(inverse,{x,y,z,1});
    if(!std::isfinite(world[3]) || std::abs(world[3])<1e-6f) return true;
    for(uint32_t i=0;i<3;++i) world[i]/=world[3];
    world[3]=1;
    const auto projected=TransformColumn(previous,world);
    for(float v:projected) if(!std::isfinite(v)) return true;
    if(projected[3]<=1e-6f || std::abs(projected[0]/projected[3]-x)>1.f ||
       std::abs(projected[1]/projected[3]-y)>1.f) return true;
  }
  return false;
}

struct MotionSceneTicket {
  uint64_t frame=0,scene=0,camera=0;
  uint32_t width=0,height=0;
  bool operator==(const MotionSceneTicket&) const=default;
};
struct MotionContentVersion {
  uintptr_t resource=0;
  uint64_t lifetime=0,frame=0,serial=0;
  uint32_t width=0,height=0;
  bool operator==(const MotionContentVersion&) const=default;
};
// Tracks only explicitly audited full-size writes, never infers lineage from
// resource pointer, dimensions, shader hash or guest address. The caller owns
// resources/lifetime tokens and must notify EVERY mutation before Find().
class MotionColorLineage {
 public:
  static constexpr uint32_t kCapacity=64;
  void Begin(uint64_t frame) {frame_=frame;count_=edge_count_=0;overflow_=false;}
  void Invalidate(uintptr_t resource) {
    for(uint32_t i=0;i<edge_count_;)
      if(edges_[i].destination.resource==resource) edges_[i]=edges_[--edge_count_];
      else ++i;
    for(uint32_t i=0;i<count_;) {
      if(entries_[i].version.resource==resource) entries_[i]=entries_[--count_];
      else ++i;
    }
  }
  bool Seed(MotionContentVersion version,MotionSceneTicket ticket) {
    Invalidate(version.resource);
    if(overflow_ || !Valid(version) || ticket.frame!=frame_ || !ticket.scene || !ticket.camera ||
       ticket.width!=version.width || ticket.height!=version.height) return false;
    if(count_==kCapacity) {overflow_=true;return false;}
    entries_[count_++]={version,ticket};ResolvePending();return !overflow_;
  }
  std::optional<MotionSceneTicket> Find(MotionContentVersion version) const {
    if(overflow_ || !Valid(version)) return {};
    for(uint32_t i=0;i<count_;++i)
      if(entries_[i].version==version) return entries_[i].ticket;
    return {};
  }
  bool Propagate(MotionContentVersion source,MotionContentVersion destination,bool audited_full_write) {
    const auto ticket=Find(source);
    Invalidate(destination.resource); // Failed/partial writes also destroy old content.
    if(!audited_full_write || !Valid(source) || !Valid(destination) || source.resource==destination.resource ||
       destination.width!=source.width || destination.height!=source.height ||
       destination.serial<=source.serial) return false;
    if(ticket) return Seed(destination,*ticket);
    if(edge_count_==kCapacity) {overflow_=true;return false;}
    edges_[edge_count_++]={source,destination};
    return false; // Recorded, but still unqualified until the exact source is seeded.
  }
 private:
  void ResolvePending() {
    for(uint32_t i=0;i<edge_count_;) {
      const auto edge=edges_[i];const auto ticket=Find(edge.source);
      if(!ticket) {++i;continue;}
      if(count_==kCapacity) {overflow_=true;return;}
      entries_[count_++]={edge.destination,*ticket};
      edges_[i]=edges_[--edge_count_];i=0;
    }
  }
  bool Valid(MotionContentVersion v) const {
    return frame_ && v.resource && v.lifetime && v.frame==frame_ && v.serial && v.width && v.height;
  }
  struct Entry {MotionContentVersion version;MotionSceneTicket ticket;};
  std::array<Entry,kCapacity> entries_{};
  struct Edge {MotionContentVersion source,destination;};
  std::array<Edge,kCapacity> edges_{};
  uint64_t frame_=0;uint32_t count_=0,edge_count_=0;bool overflow_=false;
};
}
