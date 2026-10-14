#pragma once
#include <algorithm>
#include <cstdint>
namespace superman_returns::graphics::vulkan {
enum class FramePresentationKind { kReal,kGenerated,kRepeat };
enum class GenerationAction { kWarmup,kInterpolate,kRepeat,kStale };
class FrameGenerationPolicy {
public:
  GenerationAction PreviewSource(uint64_t serial,uint64_t now_ns) const {
    if(!serial || serial<last_serial_) return GenerationAction::kStale;
    if(serial==last_serial_) return GenerationAction::kRepeat;
    const bool warmup=!last_serial_ || now_ns<last_ns_ || now_ns-last_ns_>250000000;
    return warmup?GenerationAction::kWarmup:GenerationAction::kInterpolate;
  }
  GenerationAction OnSource(uint64_t serial,uint64_t now_ns) {
    const auto action=PreviewSource(serial,now_ns);
    if(action==GenerationAction::kRepeat || action==GenerationAction::kStale)return action;
    const bool warmup=action==GenerationAction::kWarmup;
    half_ns_=warmup?16666667:std::clamp<uint64_t>((now_ns-last_ns_)/2,1000000,50000000);
    last_serial_=serial;last_ns_=now_ns;
    return warmup?GenerationAction::kWarmup:GenerationAction::kInterpolate;
  }
  uint64_t HalfIntervalNs() const {return half_ns_;}
  void OnPresented(FramePresentationKind kind) {
    if(kind==FramePresentationKind::kReal) ++real_;
    else if(kind==FramePresentationKind::kGenerated) ++generated_;
    else ++repeated_;
  }
  // History reset does not erase cumulative presentation statistics.
  void Reset() {last_serial_=last_ns_=0;half_ns_=16666667;}
  uint64_t real() const {return real_;}
  uint64_t generated() const {return generated_;}
  uint64_t repeated() const {return repeated_;}
  uint64_t presented() const {return real_+generated_+repeated_;}
private:
  uint64_t last_serial_=0,last_ns_=0,half_ns_=16666667;
  uint64_t real_=0,generated_=0,repeated_=0;
};
}
