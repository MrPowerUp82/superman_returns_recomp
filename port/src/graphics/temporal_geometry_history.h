#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>
namespace superman_returns::graphics {
using TemporalProjectionInputs=std::array<uint32_t,22>;
inline TemporalProjectionInputs CaptureTemporalProjectionInputs(
    std::span<const uint32_t,16> matrix,std::span<const uint32_t,6> shared_fixup) {
  TemporalProjectionInputs result{};
  std::copy(matrix.begin(),matrix.end(),result.begin());
  std::copy(shared_fixup.begin(),shared_fixup.end(),result.begin()+16);
  return result;
}
enum class GeometryHistoryProof {kUnknown,kStaticWorldInputs,kVerifiedInstance};
enum class GeometryHistoryStatus {kMatch,kFrameNotSealed,kUnverifiedIdentity,kNoPreviousFrame,kMissing,kAmbiguous};
struct TemporalClipRange {uint32_t slot=0;uint64_t offset=0;uint32_t vertices=0;};
struct TemporalGeometrySample {
  uint64_t mesh=0,camera_domain=0,instance=0,instance_generation=0,geometry_inputs=0;
  GeometryHistoryProof proof=GeometryHistoryProof::kUnknown;
  TemporalClipRange range;
  uint64_t projection_inputs=0; // Actual clip-only inputs: detects duplicate views within a frame.
};
struct TemporalGeometryMatch {GeometryHistoryStatus status=GeometryHistoryStatus::kMissing;TemporalClipRange previous;};
// Input correspondence is independent of draw order. Static proof requires an
// audited unchanged world-input signature. Moving/deforming objects require an
// explicitly verified instance and generation, not a mesh hash or skinned VS.
// Resolve only after Seal: a later duplicate may invalidate earlier samples.
class TemporalGeometryHistory {
 public:
  static constexpr size_t kCapacity=4096;
  void Begin(uint64_t frame,uint32_t width,uint32_t height,bool reset) {
    history_valid_=!reset && previous_frame_ && frame==previous_frame_+1 &&
        width && height && width==previous_width_ && height==previous_height_;
    frame_=frame;width_=width;height_=height;sealed_=false;dropped_=0;
    current_.clear();samples_.clear();
  }
  std::optional<size_t> Add(TemporalGeometrySample sample) {
    if(sealed_ || !frame_ || samples_.size()>=kCapacity || sample.range.slot>=3 ||
       sample.range.offset%16 || !sample.range.vertices || sample.range.vertices%3) {++dropped_;return {};}
    const size_t index=samples_.size();samples_.push_back(sample);
    const auto key=KeyFor(sample);
    if(key) {
      const auto [it,inserted]=current_.try_emplace(*key,Entry{sample.range,sample.geometry_inputs,sample.projection_inputs,false});
      if(!inserted && (it->second.inputs!=sample.geometry_inputs || it->second.projection!=sample.projection_inputs ||
                       it->second.range.vertices!=sample.range.vertices))
        it->second.ambiguous=true;
    }
    return index;
  }
  void Seal() {sealed_=true;}
  TemporalGeometryMatch Resolve(size_t index) const {
    if(!sealed_) return {GeometryHistoryStatus::kFrameNotSealed,{}};
    if(index>=samples_.size()) return {};
    const auto key=KeyFor(samples_[index]);
    if(!key) return {GeometryHistoryStatus::kUnverifiedIdentity,{}};
    if(!history_valid_) return {GeometryHistoryStatus::kNoPreviousFrame,{}};
    const auto now=current_.find(*key),old=previous_.find(*key);
    if(old==previous_.end()) return {};
    if(now->second.ambiguous || old->second.ambiguous) return {GeometryHistoryStatus::kAmbiguous,{}};
    if(old->second.range.vertices!=samples_[index].range.vertices) return {};
    return {GeometryHistoryStatus::kMatch,old->second.range};
  }
  void End() {
    if(!sealed_) {previous_.clear();previous_frame_=0;return;}
    previous_=std::move(current_);previous_frame_=frame_;previous_width_=width_;previous_height_=height_;
  }
  uint32_t dropped() const {return dropped_;}
  const std::vector<TemporalGeometrySample>& samples() const {return samples_;}
 private:
  struct Key {
    uint64_t mesh,camera,identity,generation;
    GeometryHistoryProof proof;
    bool operator==(const Key&) const=default;
  };
  struct Hash {
    size_t operator()(const Key& key) const {
      uint64_t h=key.mesh;
      for(uint64_t value:{key.camera,key.identity,key.generation,uint64_t(key.proof)})
        h^=value+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);
      return size_t(h);
    }
  };
  struct Entry {TemporalClipRange range;uint64_t inputs,projection;bool ambiguous;};
  static std::optional<Key> KeyFor(const TemporalGeometrySample& sample) {
    if(!sample.mesh || !sample.camera_domain || !sample.geometry_inputs) return {};
    if(sample.proof==GeometryHistoryProof::kStaticWorldInputs)
      return Key{sample.mesh,sample.camera_domain,sample.geometry_inputs,0,sample.proof};
    if(sample.proof==GeometryHistoryProof::kVerifiedInstance && sample.instance && sample.instance_generation)
      return Key{sample.mesh,sample.camera_domain,sample.instance,sample.instance_generation,sample.proof};
    return {};
  }
  std::unordered_map<Key,Entry,Hash> current_,previous_;
  std::vector<TemporalGeometrySample> samples_;
  uint64_t frame_=0,previous_frame_=0;
  uint32_t width_=0,height_=0,previous_width_=0,previous_height_=0,dropped_=0;
  bool history_valid_=false,sealed_=false;
};

// VS2C45 starts from float(SV_VertexID & 0xFFFFFF). With instance_data.x>=1,
// instance=trunc((vertexId+0.5)*rcp(stride)) is nonnegative. The broad fallback
// includes c98..c255 conservatively, plus instance_data c4.
// Camera c0..3, view inverse c5..7 and shadow c9..11 do not enter world XYZ.
// Shader-local literal c255 is distinct from runtime CONST_REL(255).
inline bool CaptureStaticWorldDependencies(uint64_t shader,std::span<const uint32_t,1024> words,
                                           std::array<uint32_t,636>& result) {
  if(shader!=0x2C45BFFE76D77398ull) return false;
  const float stride=std::bit_cast<float>(words[16]);
  if(!std::isfinite(stride) || stride<1) return false;
  std::array<uint32_t,636> captured{};
  for(size_t i=0;i<captured.size();++i) {
    const uint32_t value=i<4 ? words[16+i] : words[98*4+i-4];
    if(!std::isfinite(std::bit_cast<float>(value))) return false;
    captured[i]=value;
  }
  result=captured;return true;
}
inline bool CaptureStaticWorldDependenciesInVertexRange(uint64_t shader,
    std::span<const uint32_t,1024> words,uint32_t first,uint32_t last,
    std::array<uint32_t,636>& result) {
  if(shader!=0x2C45BFFE76D77398ull || first>last || last>0xFFFFFFu) return false;
  const float stride=std::bit_cast<float>(words[16]);
  if(!std::isfinite(stride) || stride<1 || stride>16777216.f ||
     !std::isfinite(std::bit_cast<float>(words[17]))) return false;
  // Original source: instance=trunc((float(vertexId)+0.5)*rcp(c4.x));
  // a0=clamp(round(instance*2),-256,255); world reads c100+a0 and c101+a0.
  // Expand the rounded reciprocal by TWO representable floats each way,
  // covering Direct3D's 1-ULP DIVISION tolerance; expand add/mul by one.
  // The pinned corpus AND runtime-cache DXIL lower this HLSL rcp to precise
  // `fdiv float 1.0, stride` (no fast/approx flag), not the looser dx.op Rcp.
  // An approximate-Rcp variant requires a different envelope before use.
  // https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm
  const float infinity=std::numeric_limits<float>::infinity();
  const auto lower=[&](float v){return std::nextafter(v,-infinity);};
  const auto upper=[&](float v){return std::nextafter(v,infinity);};
  const float reciprocal=1.f/stride;
  const float qlow=lower(lower(float(double(first)+.5))*lower(lower(reciprocal)));
  const float qhigh=upper(upper(float(double(last)+.5))*upper(upper(reciprocal)));
  const uint32_t first_instance=uint32_t(std::clamp(std::floor(double(qlow)),0.,128.));
  const uint32_t last_instance=uint32_t(std::clamp(std::floor(double(qhigh)),0.,128.));
  std::array<uint32_t,636> captured{};
  captured[0]=words[16];captured[1]=words[17];
  const uint32_t first_constant=100+2*first_instance;
  const uint32_t last_constant=std::min(255u,101+2*last_instance);
  captured[2]=first_constant;captured[3]=last_constant;
  if(first_constant<=last_constant) {
    for(uint32_t row=first_constant;row<=last_constant;++row)
      // Both reachable rows contribute XYZ only to world/raster position.
      // c100.w varies with lighting in real gameplay; neither row's W enters
      // r1.xyz or its independently constructed homogeneous r1.w.
      for(uint32_t component=0;component<3;++component) {
        const uint32_t value=words[row*4+component];
        if(!std::isfinite(std::bit_cast<float>(value))) return false;
        captured[4+(row-98)*4+component]=value;
      }
  }
  result=captured;return true;
}
}
