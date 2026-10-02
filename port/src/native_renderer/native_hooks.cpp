// Ported from crazyriddler/rexglue-native-kit @136bc6c4,
// reference/conan/port/src/native/d3d_capture.cpp and native_hooks.cpp
// (Conan native renderer). The kit ships no license file at that revision.
//
// Hooks on the game's statically linked XDK Direct3D functions. Every hook
// calls the recompiled original first (as in the kit: the original flushes
// the draw's dirty state into the command segment the PM4 mirror parses),
// then forwards to the renderer when it is active. With the renderer off the
// hooks are pass-throughs plus, with sr_native_capture, a call record.
//
// Changes from the kit:
//  * Addresses come from game_profile.h by role. A hook is compiled only when
//    its role is confirmed and not absent (SR_HOOK_ENABLED), so an unconfirmed
//    candidate can never redirect a game function.
//  * The device is taken from each call's r3 (NoteGuestDevice) instead of
//    Conan's global 0x82C81A64.
//  * Conan's 27 render-pass hooks, its Present hook and the EXP-047 name
//    lookup cache are not ported (docs/native-port-plan.md section 2.2); the
//    kit's JSON draw catalog (d3d_capture_out) is replaced by the simpler
//    capture record of native_bridge.cpp.
//  * D3DDevice_Swap at sub_82112050 is already hooked by frame_stats.cpp,
//    which forwards to native_bridge.cpp (OnFrameStatsSwap).

#include <cstring>

#include <rex/ppc.h>

#include "game_profile.h"
#include "hang_watchdog.h"
#include "native_bridge.h"
#include "native_graphics_system.h"
#include "native_renderer.h"
#include "sdk_compat.h"

namespace {

namespace native = superman_returns::native;

inline uint32_t GuestLoad32(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

// Common entry of the hooks that receive the device in r3.
inline void OnDeviceCall(const char* role, uint32_t dev, uint32_t arg) {
  native::NoteGuestDevice(dev);
  native::NoteHookCall(role, dev, arg);
}

// DrawVerticesUP calls BeginVertices/EndVertices itself.
thread_local bool t_in_draw_up = false;

// Inline-vertex draw opened by BeginVertices, consumed by EndVertices.
struct PendingInlineDraw {
  bool active = false;
  uint32_t prim = 0, count = 0, stride = 0, data = 0;
};
PendingInlineDraw g_pending_inline;

// BlockOnFence's arguments for the poll hook below.
thread_local uint32_t t_fence_dev = 0, t_fence_value = 0;

}  // namespace

#define SR_DEFINE_HOOK_(addr) \
  REX_EXTERN(__imp__sub_##addr); \
  extern "C" REX_FUNC(sub_##addr)
#define SR_DEFINE_HOOK(addr) SR_DEFINE_HOOK_(addr)
#define SR_ORIGINAL_(addr) __imp__sub_##addr
#define SR_ORIGINAL(addr) SR_ORIGINAL_(addr)

// ---- Draws -------------------------------------------------------------------

#if SR_HOOK_ENABLED(DRAW_VERTICES)
// D3DDevice_DrawVertices(dev, PrimType, StartVertex, VertexCount)
SR_DEFINE_HOOK(SR_ADDR_DRAW_VERTICES) {
  const uint32_t dev = ctx.r3.u32, prim = ctx.r4.u32, start = ctx.r5.u32, count = ctx.r6.u32;
  OnDeviceCall("DrawVertices", dev, prim);
  SR_ORIGINAL(SR_ADDR_DRAW_VERTICES)(ctx, base);
  if (native::Enabled()) native::Renderer::Get().DrawVertices(base, prim, start, count);
}
#endif

#if SR_HOOK_ENABLED(DRAW_INDEXED_VERTICES)
// D3DDevice_DrawIndexedVertices(dev, PrimType, BaseVertexIndex, StartIndex, IndexCount)
SR_DEFINE_HOOK(SR_ADDR_DRAW_INDEXED_VERTICES) {
  const uint32_t dev = ctx.r3.u32, prim = ctx.r4.u32, base_vertex = ctx.r5.u32,
                 start = ctx.r6.u32, count = ctx.r7.u32;
  OnDeviceCall("DrawIndexedVertices", dev, prim);
  SR_ORIGINAL(SR_ADDR_DRAW_INDEXED_VERTICES)(ctx, base);
  if (native::Enabled()) {
    native::Renderer::Get().DrawIndexedVertices(base, prim, int32_t(base_vertex), start, count);
  }
}
#endif

#if SR_HOOK_ENABLED(DRAW_VERTICES_UP)
// D3DDevice_DrawVerticesUP(dev, PrimType, VertexCount, pVertexData, Stride) - Begin/End based.
SR_DEFINE_HOOK(SR_ADDR_DRAW_VERTICES_UP) {
  const uint32_t dev = ctx.r3.u32, prim = ctx.r4.u32, count = ctx.r5.u32, data = ctx.r6.u32,
                 stride = ctx.r7.u32;
  OnDeviceCall("DrawVerticesUP", dev, prim);
  t_in_draw_up = true;
  SR_ORIGINAL(SR_ADDR_DRAW_VERTICES_UP)(ctx, base);
  t_in_draw_up = false;
  if (native::Enabled()) native::Renderer::Get().DrawInlineVertices(base, prim, data, count, stride);
}
#endif

#if SR_HOOK_ENABLED(BEGIN_VERTICES)
// D3DDevice_BeginVertices(dev, PrimType, VertexCount, Stride) -> r3 = where the
// game writes the inline vertices (inside the command segment).
SR_DEFINE_HOOK(SR_ADDR_BEGIN_VERTICES) {
  const uint32_t dev = ctx.r3.u32, prim = ctx.r4.u32, count = ctx.r5.u32, stride = ctx.r6.u32;
  if (!t_in_draw_up) OnDeviceCall("BeginVertices", dev, prim);
  SR_ORIGINAL(SR_ADDR_BEGIN_VERTICES)(ctx, base);
  if (native::Enabled() && !t_in_draw_up) {
    g_pending_inline = {true, prim, count, stride, ctx.r3.u32};
  }
}
#endif

#if SR_HOOK_ENABLED(END_VERTICES)
// D3DDevice_EndVertices(dev): the inline vertex data is complete here.
SR_DEFINE_HOOK(SR_ADDR_END_VERTICES) {
  native::NoteHookCall("EndVertices", ctx.r3.u32, 0);
  if (g_pending_inline.active) {
    g_pending_inline.active = false;
    native::Renderer::Get().DrawInlineVertices(base, g_pending_inline.prim, g_pending_inline.data,
                                               g_pending_inline.count, g_pending_inline.stride);
  }
  SR_ORIGINAL(SR_ADDR_END_VERTICES)(ctx, base);
}
#endif

// ---- Resolve, tiling, clear ----------------------------------------------------

#if SR_HOOK_ENABLED(RESOLVE)
// D3DDevice_Resolve(dev, Flags, pSrcRect, pDestTexture, pDestPoint, Level,
// Slice, pClearColor, ClearZ, ClearStencil, pParameters)
SR_DEFINE_HOOK(SR_ADDR_RESOLVE) {
  const uint32_t dev = ctx.r3.u32, flags = ctx.r4.u32, rect = ctx.r5.u32, dest = ctx.r6.u32,
                 point = ctx.r7.u32, clear_color = ctx.r10.u32;
  const float clear_z = float(ctx.f1.f64);
  OnDeviceCall("Resolve", dev, flags);
  // Original first: the resolve's RB_COPY_* registers land in the command
  // stream the PM4 mirror parses.
  SR_ORIGINAL(SR_ADDR_RESOLVE)(ctx, base);
  if (native::Enabled()) {
    native::Renderer::Get().Resolve(base, flags, rect, dest, point, clear_color, clear_z, 0);
  }
}
#endif

#if SR_HOOK_ENABLED(BEGIN_TILING)
// D3DDevice_BeginTiling(dev, Flags, Count, pTileRects, pClearColor, ClearZ, ClearStencil)
SR_DEFINE_HOOK(SR_ADDR_BEGIN_TILING) {
  OnDeviceCall("BeginTiling", ctx.r3.u32, ctx.r5.u32);
  if (native::Enabled()) {
    native::Renderer::Get().BeginTiling(base, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32,
                                        float(ctx.f1.f64), ctx.r8.u32);
  }
  SR_ORIGINAL(SR_ADDR_BEGIN_TILING)(ctx, base);
}
#endif

#if SR_HOOK_ENABLED(END_TILING)
// D3DDevice_EndTiling: its per-tile resolves go through the Resolve hook.
SR_DEFINE_HOOK(SR_ADDR_END_TILING) {
  OnDeviceCall("EndTiling", ctx.r3.u32, 0);
  SR_ORIGINAL(SR_ADDR_END_TILING)(ctx, base);
  if (native::Enabled()) native::Renderer::Get().EndTiling();
}
#endif

#if SR_HOOK_ENABLED(CLEAR)
// The shared float4 clear entry also catches direct engine calls that bypass
// the public D3DCOLOR wrapper. A null rectangle clears the whole surface.
SR_DEFINE_HOOK(SR_ADDR_CLEAR) {
  const uint32_t dev = ctx.r3.u32, flags = ctx.r4.u32, rect = ctx.r5.u32,
                 color_ptr = ctx.r6.u32, stencil = ctx.r8.u32;
  float color[4] = {};
  if (color_ptr && (flags & 0xF)) {
    for (uint32_t i = 0; i < 4; ++i) {
      uint32_t bits = GuestLoad32(base, color_ptr + 4 * i);
      std::memcpy(&color[i], &bits, 4);
    }
  }
  const float z = float(ctx.f1.f64);
  OnDeviceCall("Clear", dev, flags);
  SR_ORIGINAL(SR_ADDR_CLEAR)(ctx, base);
  if (native::Enabled()) {
    native::Renderer::Get().Clear(base, rect ? 1u : 0u, rect, flags, color, z, stencil);
  }
}
#endif

// ---- Command segment, constants, buffers ---------------------------------------

#if SR_HOOK_ENABLED(RING_MAKE_SPACE)
// XDK command segment switch (segment full / kickoff): the PM4 mirror parses
// the tail of the old segment and resynchronizes on the new one.
SR_DEFINE_HOOK(SR_ADDR_RING_MAKE_SPACE) {
  const uint32_t dev = ctx.r3.u32;
  OnDeviceCall("RingMakeSpace", dev, 0);
  if (native::Enabled()) native::Renderer::Get().SyncRing(base, dev);
  SR_ORIGINAL(SR_ADDR_RING_MAKE_SPACE)(ctx, base);
  if (native::Enabled()) native::Renderer::Get().ResyncRing(base, dev);
}
#endif

#if SR_HOOK_ENABLED(RING_ALLOC_LARGE)
// Large command segment allocation (after the segment switch could not satisfy it).
SR_DEFINE_HOOK(SR_ADDR_RING_ALLOC_LARGE) {
  const uint32_t dev = ctx.r3.u32;
  OnDeviceCall("RingAllocLarge", dev, 0);
  if (native::Enabled()) native::Renderer::Get().SyncRing(base, dev);
  SR_ORIGINAL(SR_ADDR_RING_ALLOC_LARGE)(ctx, base);
  if (native::Enabled()) native::Renderer::Get().ResyncRing(base, dev);
}
#endif

#if SR_HOOK_ENABLED(RESERVE_INLINE_CONSTANTS)
// Inline shader constant upload: (dev, r4, r5, vec4_count) reserves a PM4
// SET_CONSTANT packet in the ring and returns the data pointer the game
// fills. The ALU register is ((r5 - (r4 << 8)) * 4) & 0x7FC dwords.
SR_DEFINE_HOOK(SR_ADDR_RESERVE_INLINE_CONSTANTS) {
  const uint32_t r4 = ctx.r4.u32, r5 = ctx.r5.u32, count = ctx.r6.u32;
  OnDeviceCall("ReserveInlineConstants", ctx.r3.u32, count);
  SR_ORIGINAL(SR_ADDR_RESERVE_INLINE_CONSTANTS)(ctx, base);
  if (native::Enabled() && ctx.r3.u32) {
    const uint32_t unified = (r5 - (r4 << 8)) & 0x1FF;
    native::Renderer::Get().NoteRingConstants(unified >= 256, unified & 0xFF, count, ctx.r3.u32);
  }
}
#endif

#if SR_HOOK_ENABLED(LOAD_SHADER_LITERALS)
// Shader literal constants: at shader bind the XDK emits LOAD_ALU_CONSTANT
// packets from the shader object; (dev, table, data_base) walks
// {u16 vec4 register, u16 dword count, u32 offset} entries.
SR_DEFINE_HOOK(SR_ADDR_LOAD_SHADER_LITERALS) {
  OnDeviceCall("LoadShaderLiterals", ctx.r3.u32, ctx.r4.u32);
  if (native::Enabled()) {
    native::Renderer::Get().ApplyLoadAluConstants(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
  }
  SR_ORIGINAL(SR_ADDR_LOAD_SHADER_LITERALS)(ctx, base);
}
#endif

#if SR_HOOK_ENABLED(GPU_BEGIN_SHADER_CONSTANT_F4)
// D3DDevice_GpuBeginShaderConstantF4(dev, bPixelShader, StartRegister,
//   ppCachedConstants, ppWriteCombinedConstants, Vector4fCount): the game
// writes constants through the returned ring pointer only.
SR_DEFINE_HOOK(SR_ADDR_GPU_BEGIN_SHADER_CONSTANT_F4) {
  const bool pixel = ctx.r4.u32 != 0;
  const uint32_t start = ctx.r5.u32, ring_out = ctx.r7.u32, count = ctx.r8.u32;
  OnDeviceCall("GpuBeginShaderConstantF4", ctx.r3.u32, count);
  SR_ORIGINAL(SR_ADDR_GPU_BEGIN_SHADER_CONSTANT_F4)(ctx, base);
  if (native::Enabled() && ring_out) {
    native::Renderer::Get().NoteRingConstants(pixel, start, count, GuestLoad32(base, ring_out));
  }
}
#endif

// D3DVertexBuffer_Unlock / D3DIndexBuffer_Unlock (r3 = buffer object): the
// guest has rewritten the buffer contents.
[[maybe_unused]] static void InvalidateBufferObject(uint8_t* base, uint32_t object) {
  if (!object || !native::Enabled()) return;
  const uint32_t address = GuestLoad32(base, object + 0x18) & ~3u;
  const uint32_t size = GuestLoad32(base, object + 0x1C) & 0x00FFFFFFu;
  native::Renderer::Get().InvalidateGuestRange(address, size ? size : 0x10000);
}

#if SR_HOOK_ENABLED(VERTEX_BUFFER_UNLOCK)
SR_DEFINE_HOOK(SR_ADDR_VERTEX_BUFFER_UNLOCK) {
  const uint32_t object = ctx.r3.u32;
  native::NoteHookCall("VertexBufferUnlock", object, 0);
  SR_ORIGINAL(SR_ADDR_VERTEX_BUFFER_UNLOCK)(ctx, base);
  InvalidateBufferObject(base, object);
}
#endif

#if SR_HOOK_ENABLED(INDEX_BUFFER_UNLOCK)
SR_DEFINE_HOOK(SR_ADDR_INDEX_BUFFER_UNLOCK) {
  const uint32_t object = ctx.r3.u32;
  native::NoteHookCall("IndexBufferUnlock", object, 0);
  SR_ORIGINAL(SR_ADDR_INDEX_BUFFER_UNLOCK)(ctx, base);
  InvalidateBufferObject(base, object);
}
#endif

// ---- Frame boundary and GPU waits ----------------------------------------------

#if SR_HOOK_ENABLED(SWAP) && SR_HEX(SR_ADDR_SWAP) != SR_FRAME_STATS_SWAP_HOOK
// D3DDevice_Swap(dev, pFrontBuffer, pParameters) when it is not the function
// frame_stats.cpp already hooks. The original still runs so the guest's
// swap/vblank/fence semantics are untouched; the native frame is presented
// right after.
SR_DEFINE_HOOK(SR_ADDR_SWAP) {
  const uint32_t dev = ctx.r3.u32, front_buffer = ctx.r4.u32;
  static uint64_t swap_number = 0;
  ++swap_number;
  SR_ORIGINAL(SR_ADDR_SWAP)(ctx, base);
  native::NoteGuestSwap(base, dev, front_buffer);
  if (native::Enabled()) {
    native::HangWatchdogBeat();
    native::Renderer::Get().OnSwap(base, front_buffer, swap_number);
  }
}
#endif

// XDK GPU waits. BlockOnFence (r3 = device, r4 = fence) loops calling the poll
// (a few pause instructions, a read-pointer check and the XDK's hang
// detector; returns 1 while the caller should keep waiting) until the GPU's
// fence counter passes `fence`:
//   (dev[fence_current] - fence) >= (dev[fence_current] - *dev[fence_completed_ptr])
// On a PC that spin burns a core. With the native graphics system the poll
// sleeps until that exact condition holds (inside BlockOnFence) or any GPU
// progress (elsewhere), 1 ms cap either way so the hang detector still runs.
// Under Xenos (xenos / A/B mode) both waits are no-ops: the spin is unchanged.
#if SR_HOOK_ENABLED(BLOCK_ON_FENCE)
SR_DEFINE_HOOK(SR_ADDR_BLOCK_ON_FENCE) {
  const uint32_t saved_dev = t_fence_dev, saved_value = t_fence_value;
  t_fence_dev = ctx.r3.u32;
  t_fence_value = ctx.r4.u32;
  OnDeviceCall("BlockOnFence", ctx.r3.u32, ctx.r4.u32);
  SR_ORIGINAL(SR_ADDR_BLOCK_ON_FENCE)(ctx, base);
  t_fence_dev = saved_dev;
  t_fence_value = saved_value;
}
#endif

#if SR_HOOK_ENABLED(POLL_GPU_PROGRESS)
SR_DEFINE_HOOK(SR_ADDR_POLL_GPU_PROGRESS) {
  const uint64_t generation = native::GpuProgressGeneration();
  native::NoteHookCall("PollGpuProgress", ctx.r3.u32, 0);
  SR_ORIGINAL(SR_ADDR_POLL_GPU_PROGRESS)(ctx, base);
  if (ctx.r3.u32 != 1) return;
  if (t_fence_dev) {
    const uint32_t dev = t_fence_dev, fence = t_fence_value;
    native::WaitForGpuCondition(
        [base, dev, fence] {
          const auto& layout = native::profile::kDevice;
          const uint32_t current = GuestLoad32(base, dev + layout.fence_current);
          const uint32_t completed =
              GuestLoad32(base, GuestLoad32(base, dev + layout.fence_completed_ptr));
          return current - fence >= current - completed;
        },
        1000);
  } else {
    native::WaitForGpuProgress(generation, 1000);
  }
}
#endif
