// Entry points between the app (superman_returns_app.h, frame_stats.cpp) and
// the native renderer. Safe to include in every build: without the native
// sources (CMake SR_NATIVE=OFF) the calls the app makes are inline no-ops.
//
// Build levels (CMake SR_NATIVE, docs/native-port-plan.md section 5):
//   OFF       SR_HAS_NATIVE 0. Nothing native is compiled.
//   CAPTURE   SR_HAS_NATIVE 1, SR_NATIVE_RENDERER_BUILD 0. Hooks whose address
//             is confirmed in game_profile.h are compiled and observe only
//             (sr_native_capture); sr_renderer=native falls back to xenos.
//   RENDERER  SR_HAS_NATIVE 1, SR_NATIVE_RENDERER_BUILD 1. Requires every
//             profile entry confirmed (static_assert in native_bridge.cpp).
#pragma once

#include <cstdint>
#include <memory>

#ifndef SR_HAS_NATIVE
#define SR_HAS_NATIVE 0
#endif
#ifndef SR_NATIVE_RENDERER_BUILD
#define SR_NATIVE_RENDERER_BUILD 0
#endif

namespace rex::system {
class IGraphicsSystem;
}

namespace superman_returns::native {

using GraphicsSystemFactory = std::unique_ptr<rex::system::IGraphicsSystem> (*)();

#if SR_HAS_NATIVE

// Graphics system for --sr_renderer=native. Returns nullptr when the native
// renderer cannot run (not built, profile unconfirmed, no shader corpus);
// the reason is logged once and the caller uses the Xenos backend.
// `create_xenos` builds the stock backend: the native graphics system falls
// back to it if D3D12 presentation cannot be set up. In sr_native_ab_mode the
// result is the Xenos backend with the A/B swap observer installed.
std::unique_ptr<rex::system::IGraphicsSystem> CreateNativeGraphicsSystem(
    GraphicsSystemFactory create_xenos, GraphicsSystemFactory create_xenos_ab);

// The native renderer consumes the hooked D3D calls (native or A/B mode).
bool RendererActive();
// sr_native_capture: hooks record what they see (logs/native_capture.json).
bool CaptureActive();

// Capture-mode bookkeeping, called by every compiled hook. `r3`/`r4` are the
// first arguments (or results) of the call, recorded as samples.
void NoteHookCall(const char* role, uint32_t r3, uint32_t r4);
// Something a confirmed hook saw that contradicts its role (logged once).
void LogCaptureAnomalyOnce(const char* what, uint32_t value);

// Capture bookkeeping of one guest swap (both swap hook paths call it).
void NoteGuestSwap(uint8_t* base, uint32_t dev, uint32_t front_buffer);

// Called by frame_stats.cpp's hook of sub_82112050 after the original ran,
// with the r3/r4 it received. Forwards to the renderer while game_profile.h
// names sub_82112050 as the (confirmed) D3DDevice_Swap.
void OnFrameStatsSwap(uint8_t* base, uint32_t dev, uint32_t front_buffer);

#else

inline void OnFrameStatsSwap(uint8_t*, uint32_t, uint32_t) {}

#endif

}  // namespace superman_returns::native
