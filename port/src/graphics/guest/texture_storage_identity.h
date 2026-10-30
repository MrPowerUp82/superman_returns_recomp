#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace superman_returns::graphics::guest {
enum class TextureStorageBackend {kD3D12,kVulkan};
// Restart-only override, shared by capture/render workers. Vulkan remains
// opt-in: two game A/B pairs regressed despite passing GPU correctness tests.
inline bool TextureStorageReuseEnabled(TextureStorageBackend backend=TextureStorageBackend::kD3D12) {
  static const int override_value=[] {
    const char* value=std::getenv("SR_NATIVE_TEXTURE_STORAGE_REUSE");
    return !value || !value[0] ? -1 : (value[0]=='0'?0:1);
  }();
  return override_value<0 ? backend==TextureStorageBackend::kD3D12 : override_value!=0;
}

// Identity of decoded pixels AND their image view. Keep the original fetch for
// bindings/samplers. Whitelist only fields absent from the texture decoder:
// clamp U/V/W; mag/min/mip/aniso; volume mag/min; LOD bias.
// In particular mip min/max, swizzle, signs/gamma and unknown bits stay exact.
inline std::array<uint32_t,6> TextureStorageFetch(std::span<const uint32_t,6> fetch,bool sharing=true) {
  std::array<uint32_t,6> key;std::copy(fetch.begin(),fetch.end(),key.begin());
  if(sharing && (key[0]&3)==2) {
    key[0]&=~0x0007fc00u;
    key[3]&=~0x0ff80000u;
    key[4]&=~0x003ff003u;
  }
  return key;
}
}
