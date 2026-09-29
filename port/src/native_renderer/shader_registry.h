// Ported from crazyriddler/rexglue-native-kit @136bc6c4,
// reference/conan/port/src/native/shader_registry.h (Conan native renderer).
// The kit ships no license file at that revision; parts derived from Xenia /
// ReXGlue keep their BSD license. Changes for Superman Returns are listed in
// docs/native-port-plan.md section 2.
//
// Guest shader object -> shader corpus identity (container_hash, the key used
// by artifacts/shaders/catalog.json and XenosRecomp).
#pragma once

#include <cstdint>

namespace superman_returns::native {

struct GuestShaderInfo {
  uint64_t container_hash = 0;
  bool is_vertex = false;
};

// Returns nullptr for objects not created through the hooked XDK creators.
const GuestShaderInfo* LookupGuestShader(uint32_t guest_object);

}  // namespace superman_returns::native
