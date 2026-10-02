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

// XDK 2.0.3529 LTCG: the D3DShader container is embedded directly inside the
// shader object, not pointed to by a separate allocation.  These offsets were
// confirmed by disassembly of SetVertexShader / SetPixelShader and by
// diagnostic logging at draw time (see DIAG DRAW logs).
static constexpr uint32_t kVSContainerOffset = 40;   // addi r11,r29,40
static constexpr uint32_t kPSContainerOffset = 872;  // addic. r11,r29,872

// Lazily hash the inline containers from the shader objects that are currently
// bound to the device.  Called once per draw from native_renderer.cpp before
// LookupGuestShader so that g_shaders is populated on first use even when
// OnCreateShader fired before the container header was finalized.
void TryRegisterInlineShaders(uint8_t* base, uint32_t vs_obj, uint32_t ps_obj);

}  // namespace superman_returns::native
