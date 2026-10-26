#pragma once
#include "observed_instance_lifetimes.h"
#include <array>
#include <span>
#include <algorithm>
#include <bit>
#include <cmath>

namespace superman_returns::graphics {
inline bool IsObservedSkinnedMotionShader(uint64_t source) {
  return source==0xC20921D0DBBD2F1Full || source==0xC687EB65D635C5D7ull;
}
// C209/C687 vertex correspondence is supplied by the caller's frozen canonical
// mesh/index/fetch versions and post-VS clips. This records the audited
// renderable scope plus all uploaded VS bank/boolean inputs conservatively;
// it does not identify a character or establish submesh/part identity. This
// input record alone must never grant GeometryHistoryProof::kVerifiedInstance.
struct ObservedSkinnedMotionInputs {
  ObservedInstanceToken owner;
  std::array<uint32_t,1028> position_words{};
  std::array<uint32_t,16> clip_rows{};
  bool operator==(const ObservedSkinnedMotionInputs&) const=default;
};
inline bool CaptureObservedSkinnedMotionInputs(uint64_t source,
    const ObservedRenderInvocation& invocation,
    std::span<const uint32_t,1024> uploaded_float_words,
    std::span<const uint32_t,4> uploaded_bool_words,
    ObservedSkinnedMotionInputs& result) {
  const bool scope=(source==0xC20921D0DBBD2F1Full && invocation.render==0x82300328 && invocation.vtable==0x82021D18) ||
      (source==0xC687EB65D635C5D7ull && invocation.render==0x82306BB0 && invocation.vtable==0x820217E0);
  if(!scope || invocation.token.address<0x10000 ||
     (invocation.token.address&3) || !invocation.token.generation) return false;
  ObservedSkinnedMotionInputs captured;
  for(size_t i=0;i<uploaded_float_words.size();++i) {
    if(!std::isfinite(std::bit_cast<float>(uploaded_float_words[i]))) return false;
    captured.position_words[i]=uploaded_float_words[i];
  }
  // Both sources: c8..11 are object-to-clip; do not use them as a shared camera.
  std::copy_n(uploaded_float_words.begin()+32,16,captured.clip_rows.begin());
  std::copy(uploaded_bool_words.begin(),uploaded_bool_words.end(),captured.position_words.begin()+1024);
  captured.owner=invocation.token;
  result=captured;return true;
}
}
