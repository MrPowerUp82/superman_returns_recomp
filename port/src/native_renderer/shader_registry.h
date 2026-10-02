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
#include <string>

#include "shader_library.h"

namespace superman_returns::native {

struct GuestShaderInfo {
  uint64_t container_hash = 0;
  bool is_vertex = false;
  bool dynamic_vertex_fetch = false;
  // How the container was recognised; kNone = legacy hash without a library.
  PreShaderMatch match = PreShaderMatch::kNone;
};

// Returns nullptr for objects not created through the hooked XDK creators.
const GuestShaderInfo* LookupGuestShader(uint32_t guest_object);

// The pre-translated shader library (sr_native_preshaders), or nullptr when it
// is disabled or missing. `where` receives its path or why it is absent.
const ShaderLibrary* PreShaderLibrary(std::string* where = nullptr);

// XDK 2.0.3529 LTCG: shader objects carry a copy of their container at one of
// these offsets (addi r11,r29,40 in SetVertexShader, addic. r11,r29,872 in
// SetPixelShader). Direct3D patches those copies, so they are only a
// draw-time fallback.
static constexpr uint32_t kVSContainerOffset = 40;
static constexpr uint32_t kPSContainerOffset = 872;

// Draw-time fallback for shader objects the creator hooks did not register.
// Each unknown object is examined once (until a creator reuses its address).
void TryRegisterInlineShaders(uint8_t* base, uint32_t vs_obj, uint32_t ps_obj);

}  // namespace superman_returns::native
