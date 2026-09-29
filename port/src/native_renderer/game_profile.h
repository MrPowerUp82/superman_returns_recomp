// Every Superman Returns fact the native renderer depends on, in one place.
//
// The renderer ported from crazyriddler/rexglue-native-kit is generic XDK
// Direct3D knowledge; the kit's GAME_ADAPTATION_GUIDE.md section 2 lists the
// facts that change per game. Conan's values were spread over four files;
// here they are roles, so the renderer never names a Conan number.
//
// NOTHING IN THIS FILE HAS BEEN CONFIRMED. The candidates come from
// docs/data/xdk_match.tsv (tools/re/xdk_sigs.py of the kit, run against the
// owner's image) and from code already in port/src. docs/native-port-plan.md
// section 3 gives, for every entry, the evidence so far and how to confirm it
// locally (section 8: tools/native_validate.ps1).
//
// Rules:
//  * SR_ADDR_<ROLE> is the guest address as 8 hex digits without 0x: the hook
//    code pastes it into the recompiled symbol name (sub_<ADDR>).
//  * SR_CONFIRMED_<ROLE> stays 0 until the owner has checked the address in
//    the disassembly (and, when possible, at run time in the capture build).
//    Replace the `// UNCONFIRMED` note with the evidence when setting it to 1.
//  * A role the game never calls (for example predicated tiling) is confirmed
//    with SR_ABSENT_<ROLE> 1: its hook is not built.
//  * A hook is only compiled when its role is confirmed and not absent
//    (SR_HOOK_ENABLED). The renderer can only be activated, and the CMake
//    SR_NATIVE=RENDERER build only compiles, when kProfileConfirmed is true.
#pragma once

#include <cstdint>

#define SR_HEX_(digits) 0x##digits
#define SR_HEX(digits) SR_HEX_(digits)
#define SR_HOOK_ENABLED(role) (SR_CONFIRMED_##role && !SR_ABSENT_##role)

// ---- XDK D3D hook addresses (docs/native-port-plan.md table 3.1) ----------

// D3DDevice_DrawVertices(dev, prim, start, count). Conan 82580918.
#define SR_ADDR_DRAW_VERTICES 820FBBF8  // UNCONFIRMED: xdk_sigs missing (0.060), D3D block
#define SR_CONFIRMED_DRAW_VERTICES 0
#define SR_ABSENT_DRAW_VERTICES 0

// D3DDevice_DrawIndexedVertices(dev, prim, base, start_index, count). Conan 82580D00.
#define SR_ADDR_DRAW_INDEXED_VERTICES 820FC000  // UNCONFIRMED: missing (0.062)
#define SR_CONFIRMED_DRAW_INDEXED_VERTICES 0
#define SR_ABSENT_DRAW_INDEXED_VERTICES 0

// D3DDevice_DrawVerticesUP(dev, prim, count, data, stride). Conan 825808B8.
#define SR_ADDR_DRAW_VERTICES_UP 820FBBB0  // UNCONFIRMED: missing (0.583)
#define SR_CONFIRMED_DRAW_VERTICES_UP 0
#define SR_ABSENT_DRAW_VERTICES_UP 0

// D3DDevice_BeginVertices(dev, prim, count, stride) -> r3 = vertex data. Conan 825803F8.
#define SR_ADDR_BEGIN_VERTICES 820FB6E8  // UNCONFIRMED: missing (0.121)
#define SR_CONFIRMED_BEGIN_VERTICES 0
#define SR_ABSENT_BEGIN_VERTICES 0

// D3DDevice_EndVertices(dev). Conan 82580898. The TSV candidate 82551FD8 lies
// outside the D3D block; in Conan EndVertices sits 0x20 before DrawVerticesUP,
// which would put it near 820FBB90 here (layout guess, not in the TSV).
#define SR_ADDR_END_VERTICES 820FBB90  // UNCONFIRMED: layout guess
#define SR_CONFIRMED_END_VERTICES 0
#define SR_ABSENT_END_VERTICES 0

// D3DDevice_Resolve(dev, flags, src_rect, dest_tex, dest_point, level, slice,
// clear_color, clear_z(f1), clear_stencil, params). Conan 822F5028.
#define SR_ADDR_RESOLVE 8210C5F8  // UNCONFIRMED: missing (0.090)
#define SR_CONFIRMED_RESOLVE 0
#define SR_ABSENT_RESOLVE 0

// D3DDevice_BeginTiling / EndTiling. Conan 822F3EF8 / 822F4480. The TSV
// candidate for BeginTiling (828801A8) is outside the D3D block. If the game
// never uses predicated tiling, confirm both as absent.
#define SR_ADDR_BEGIN_TILING 828801A8  // UNCONFIRMED: missing (0.043), likely wrong
#define SR_CONFIRMED_BEGIN_TILING 0
#define SR_ABSENT_BEGIN_TILING 0
#define SR_ADDR_END_TILING 8210DA98  // UNCONFIRMED: missing (0.047)
#define SR_CONFIRMED_END_TILING 0
#define SR_ABSENT_END_TILING 0

// D3DDevice_Clear(dev, count, rects, flags, color, z(f1), stencil). Conan 822F9EE0.
#define SR_ADDR_CLEAR 82101998  // UNCONFIRMED: fuzzy (0.979)
#define SR_CONFIRMED_CLEAR 0
#define SR_ABSENT_CLEAR 0

// Command segment switch (D3D_RingMakeSpace + KickOff) and large segment
// allocation: the PM4 mirror resyncs around them. Conan 822DF848 / 822DF548.
#define SR_ADDR_RING_MAKE_SPACE 827631D8  // UNCONFIRMED: missing (0.172), outside D3D block
#define SR_CONFIRMED_RING_MAKE_SPACE 0
#define SR_ABSENT_RING_MAKE_SPACE 0
#define SR_ADDR_RING_ALLOC_LARGE 820FCF90  // UNCONFIRMED: missing (0.372)
#define SR_CONFIRMED_RING_ALLOC_LARGE 0
#define SR_ABSENT_RING_ALLOC_LARGE 0

// Inline SET_CONSTANT reserve (dev, r4, r5, vec4_count) -> r3 = data. Conan 82580358.
#define SR_ADDR_RESERVE_INLINE_CONSTANTS 82113010  // UNCONFIRMED: exact (1.000)
#define SR_CONFIRMED_RESERVE_INLINE_CONSTANTS 0
#define SR_ABSENT_RESERVE_INLINE_CONSTANTS 0

// Shader literal loader (dev, table, data) -> LOAD_ALU_CONSTANT. Conan 822F7408.
#define SR_ADDR_LOAD_SHADER_LITERALS 82108470  // UNCONFIRMED: exact (1.000)
#define SR_CONFIRMED_LOAD_SHADER_LITERALS 0
#define SR_ABSENT_LOAD_SHADER_LITERALS 0

// D3DDevice_GpuBeginShaderConstantF4(dev, pixel, start, cached, ring_out, count).
// Conan 822E7A48. TSV candidate outside the D3D block; may be unused by SR.
#define SR_ADDR_GPU_BEGIN_SHADER_CONSTANT_F4 82861150  // UNCONFIRMED: missing (0.079), likely wrong
#define SR_CONFIRMED_GPU_BEGIN_SHADER_CONSTANT_F4 0
#define SR_ABSENT_GPU_BEGIN_SHADER_CONSTANT_F4 0

// D3DVertexBuffer_Unlock / D3DIndexBuffer_Unlock (r3 = buffer). Conan 822EA0D8 / 822EA1E0.
#define SR_ADDR_VERTEX_BUFFER_UNLOCK 820F4000  // UNCONFIRMED: exact (1.000)
#define SR_CONFIRMED_VERTEX_BUFFER_UNLOCK 0
#define SR_ABSENT_VERTEX_BUFFER_UNLOCK 0
#define SR_ADDR_INDEX_BUFFER_UNLOCK 820F4150  // UNCONFIRMED: ambiguous with 82769CB0; D3D block one
#define SR_CONFIRMED_INDEX_BUFFER_UNLOCK 0
#define SR_ABSENT_INDEX_BUFFER_UNLOCK 0

// XDK shader creators: r3 = shader container in, shader object out.
// Conan 822E84C0 / 822E85D0. Both TSV candidates are outside the D3D block.
#define SR_ADDR_CREATE_SHADER_A 826A8050  // UNCONFIRMED: missing (0.132), likely wrong
#define SR_CONFIRMED_CREATE_SHADER_A 0
#define SR_ABSENT_CREATE_SHADER_A 0
#define SR_ADDR_CREATE_SHADER_B 827A6130  // UNCONFIRMED: missing (0.167), likely wrong
#define SR_CONFIRMED_CREATE_SHADER_B 0
#define SR_ABSENT_CREATE_SHADER_B 0

// D3DDevice_Swap(dev, front_buffer, params). Conan 822E8EB8. sub_82112050 is
// the only VdSwap caller and already hooked by frame_stats.cpp, which forwards
// to the native renderer (native_bridge.cpp) while this address stays 82112050.
#define SR_ADDR_SWAP 82112050  // UNCONFIRMED: project evidence, TSV has no candidate
#define SR_CONFIRMED_SWAP 0
#define SR_ABSENT_SWAP 0
#define SR_FRAME_STATS_SWAP_HOOK 0x82112050

// D3D BlockOnFence(dev, fence) and the poll it spins on. Conan 822DF1D8 / 822DE050.
#define SR_ADDR_BLOCK_ON_FENCE 822115D8  // UNCONFIRMED: missing (0.240)
#define SR_CONFIRMED_BLOCK_ON_FENCE 0
#define SR_ABSENT_BLOCK_ON_FENCE 0
#define SR_ADDR_POLL_GPU_PROGRESS 820F33E8  // UNCONFIRMED: exact (1.000)
#define SR_CONFIRMED_POLL_GPU_PROGRESS 0
#define SR_ABSENT_POLL_GPU_PROGRESS 0

namespace superman_returns::native::profile {

struct HookRole {
  const char* role;
  uint32_t address;
  bool confirmed;
  bool absent;
};

#define SR_ROLE(name, role) \
  HookRole { name, SR_HEX(SR_ADDR_##role), SR_CONFIRMED_##role != 0, SR_ABSENT_##role != 0 }

inline constexpr HookRole kHookRoles[] = {
    SR_ROLE("DrawVertices", DRAW_VERTICES),
    SR_ROLE("DrawIndexedVertices", DRAW_INDEXED_VERTICES),
    SR_ROLE("DrawVerticesUP", DRAW_VERTICES_UP),
    SR_ROLE("BeginVertices", BEGIN_VERTICES),
    SR_ROLE("EndVertices", END_VERTICES),
    SR_ROLE("Resolve", RESOLVE),
    SR_ROLE("BeginTiling", BEGIN_TILING),
    SR_ROLE("EndTiling", END_TILING),
    SR_ROLE("Clear", CLEAR),
    SR_ROLE("RingMakeSpace", RING_MAKE_SPACE),
    SR_ROLE("RingAllocLarge", RING_ALLOC_LARGE),
    SR_ROLE("ReserveInlineConstants", RESERVE_INLINE_CONSTANTS),
    SR_ROLE("LoadShaderLiterals", LOAD_SHADER_LITERALS),
    SR_ROLE("GpuBeginShaderConstantF4", GPU_BEGIN_SHADER_CONSTANT_F4),
    SR_ROLE("VertexBufferUnlock", VERTEX_BUFFER_UNLOCK),
    SR_ROLE("IndexBufferUnlock", INDEX_BUFFER_UNLOCK),
    SR_ROLE("CreateShaderA", CREATE_SHADER_A),
    SR_ROLE("CreateShaderB", CREATE_SHADER_B),
    SR_ROLE("Swap", SWAP),
    SR_ROLE("BlockOnFence", BLOCK_ON_FENCE),
    SR_ROLE("PollGpuProgress", POLL_GPU_PROGRESS),
};

#undef SR_ROLE

// ---- Guest D3DDevice ------------------------------------------------------

// Global holding the D3DDevice pointer (Conan 0x82C81A64). Unknown for SR.
// 0 = take the device from the r3 argument of the hooked D3D calls (the XDK
// keeps one device per process), which needs no extra address; the capture
// build still lists the globals that hold it (sr_native_capture) so a later
// profile can pin one. A nonzero value must be confirmed.
inline constexpr uint32_t kDevicePtrAddr = 0;  // 0 = from hook arguments
inline constexpr bool kDevicePtrConfirmed = false;

// D3DDevice field offsets. Conan's XDK revision (kit docs/XDK_D3D_NOTES.md);
// xdk_sigs.py masks load/store immediates, so its exact matches do not prove
// these. Confirm with the setters in docs/native-port-plan.md table 3.2.
struct DeviceLayout {
  uint32_t fetch_constants = 0x480;  // [32] x 24 bytes
  uint32_t vs_constants = 0x780;     // 256 float4
  uint32_t ps_constants = 0x1780;    // 256 float4
  uint32_t vs_bools = 0x2780;        // 4 dwords
  uint32_t ps_bools = 0x2790;        // 4 dwords
  uint32_t vs_loops = 0x27A0;        // 16 dwords
  uint32_t ps_loops = 0x27E0;        // 16 dwords
  uint32_t register_shadow = 0x2880;  // first Xenos register group (0x2000)
  uint32_t vertex_decl = 0x2E24;
  uint32_t index_buffer = 0x308C;
  uint32_t render_targets = 0x3090;  // [4] surfaces
  uint32_t depth_stencil = 0x30A0;
  uint32_t stream_buffers = 0x30A4;  // [16]
  uint32_t stream_strides = 0x30E8;  // [16] in dwords
  uint32_t textures = 0x30F8;        // [26] texture objects
  uint32_t viewport = 0x3160;        // X, Y, W, H, MinZ, MaxZ as floats
  uint32_t shader_a = 0x318C;
  uint32_t shader_b = 0x3190;
  uint32_t ring_write = 0x30;  // command segment write pointer
  uint32_t ring_limit = 0x34;
  uint32_t fence_current = 10908;        // dev[+10908]: fence counter
  uint32_t fence_completed_ptr = 10896;  // dev[+10896]: -> GPU-written fence
  uint32_t size = 0x5700;
};
inline constexpr DeviceLayout kDevice{};  // UNCONFIRMED: Conan's XDK revision
inline constexpr bool kDeviceLayoutConfirmed = false;

// Register shadow -> Xenos register groups flushed by the XDK's generic
// writer ({first register, count, device offset}); same XDK revision caveat.
struct RegisterShadowRange {
  uint32_t first, count, offset;
};
inline constexpr RegisterShadowRange kRegisterShadow[] = {
    {0x2000, 16, 0x2880}, {0x2100, 21, 0x28CC}, {0x2180, 5, 0x2920},
    {0x2200, 12, 0x2934}, {0x2280, 21, 0x2964}, {0x2300, 38, 0x29B8}};

// ---- Frame shape -----------------------------------------------------------

// Front buffer size: render_scale.cpp shows device setup filling 1280x720
// (sub_82611A20 mode 4) and the engine render size (device +60/+64).
inline constexpr uint32_t kOutputWidth = 1280;
inline constexpr uint32_t kOutputHeight = 720;
// Width of the game's 3D scene surfaces. Conan rendered at 1024 and stretched
// to 1280 ("full_scene_resolution" scaled by 1280/1024). SR's engine stores
// 1280x720 as its render size, so the factor is 1 (option has no effect).
inline constexpr uint32_t kSceneWidth = 1280;  // UNCONFIRMED: from render_scale.cpp only

// Guest vblanks per presented frame (D3DPRESENT_INTERVAL). Conan presents on
// every second vblank; SR runs at 30 FPS on the console but this is not
// measured. Only sets the vblank rate of the native graphics system.
inline constexpr uint32_t kVblanksPerFrame = 2;  // UNCONFIRMED

// Loaded image range dumped by SR_DUMP_IMAGE (superman_returns_app.h); the
// capture build scans its data for the device pointer.
inline constexpr uint32_t kImageBase = 0x82000000;
inline constexpr uint32_t kImageEnd = 0x829E0000;

// ---- Render passes ---------------------------------------------------------

// Conan hooked its engine's pass table (29 named passes) and used some of them
// for options (shadow quality, SSAO, FXAA before the HUD, soft particles) and
// for asynchronous PSO compilation in passes redrawn every frame. No pass
// table is known for SR: every role is -1, which turns those features off.
inline constexpr int kPassShadowMaps = -1;
inline constexpr int kPassOpaque = -1;
inline constexpr int kPassEndTiling = -1;
inline constexpr int kPassSorted = -1;
inline constexpr int kPassUpscale = -1;
inline constexpr int kPassHud = -1;
// Passes the game redraws from scratch every frame (async PSO compile allowed).
inline constexpr int kRedrawnPasses[] = {-1};
inline constexpr int kMaxPasses = 32;

// ---- Shader-derived names (shader catalog reflection) ----------------------

// Inverse view-projection constant used for SSAO / soft particles
// (Conan g_mProjectionToWorld). Unknown: projection_regs.inc stays empty.
inline constexpr const char* kInverseViewProjectionName = nullptr;

// ---- Gate -------------------------------------------------------------------

constexpr bool AllHooksConfirmed() {
  for (const HookRole& r : kHookRoles) {
    if (!r.confirmed) return false;
  }
  return true;
}

inline constexpr bool kProfileConfirmed =
    AllHooksConfirmed() && kDeviceLayoutConfirmed && (kDevicePtrAddr == 0 || kDevicePtrConfirmed);

}  // namespace superman_returns::native::profile
