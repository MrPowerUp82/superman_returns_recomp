// Runtime shader translation (sr_native_runtime_shaders).
//
// Releases ship no translated shaders. The creation hooks hand every shader
// container the game builds to this class, which translates it on the user's
// machine with the tools in shader_tools/ (the XenosRecomp HLSL emitter and
// DXC, the same two steps tools/shaders/build_catalog.py runs for the corpus),
// in background threads, and keeps the DXIL in an on-disk cache so the next
// run starts with everything translated.
//
// Both tools run as separate processes: the emitter asserts on shaders it does
// not support, and an assert must not take the game down.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace superman_returns::native {

enum class TranslateState {
  kUnknown,  // no container was ever offered for this hash
  kPending,  // queued or being translated
  kReady,
  kFailed,
};

class ShaderTranslator {
 public:
  static ShaderTranslator& Get();

  // True when sr_native_runtime_shaders is on and the tools were found.
  bool Enabled();

  // A container the game created (cheap, call it for every shader). Starts a
  // background translation unless the DXIL is already cached.
  void Offer(uint64_t hash, bool vertex, const uint8_t* container, size_t size);

  // The DXIL of `hash`. Waits up to sr_native_runtime_shader_wait_ms for a
  // translation that is still running; `state` says why `dxil` stayed empty.
  TranslateState Fetch(uint64_t hash, bool vertex, std::vector<uint8_t>& dxil);

  // The on-disk cache only, never waits (pipeline cache precompilation).
  bool LoadCached(uint64_t hash, bool vertex, std::vector<uint8_t>& dxil);

 private:
  ShaderTranslator() = default;
};

}  // namespace superman_returns::native
