// Ported from crazyriddler/rexglue-native-kit @136bc6c4,
// reference/conan/port/src/native/native_renderer.cpp (Conan native renderer).
// The kit ships no license file at that revision; parts derived from Xenia /
// ReXGlue keep their BSD license. Changes for Superman Returns are listed in
// docs/native-port-plan.md section 2.
//
#include "native_renderer.h"
#include "guest_cpu_profile.h"
#include "checked_guest_memory.h"
#include "guest_hash.h"
#include "cpu_features.h"
#if defined(__AVX2__) || defined(__x86_64__) || defined(_M_X64)
#if defined(__AVX2__) || defined(__x86_64__) || defined(_M_X64)
#include "xxh3_avx2.h"
#endif
#endif
#include "texture_write_audit.h"
#include "texture_content.h"
#include "texture_hash_replay.h"
#include "texture_source_probe.h"
#include "stacked_resolve_policy.h"
#include "../graphics/guest/render_state.h"
#include "../graphics/guest/edram_alias.h"
#include "../graphics/guest/render_packet.h"
#include "../graphics/guest/parallel_replay.h"
#include "../graphics/guest/replay_trace.h"
#include "../graphics/guest/primitive_expansion.h"
#include "../graphics/guest/pm4_capture.h"
#include "../graphics/frame_timeline.h"
#include "../graphics/d3d12/device_capabilities.h"
#include "../graphics/graphics_options_runtime.h"
#include "../render_scale_policy.h"
#include "../sr_settings.h"

#include <dxgi1_4.h>

#include <algorithm>
#include <bit>
#include <array>
#include <immintrin.h>
#include <chrono>
#include <atomic>
#include <set>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/xenos.h>
#include <rex/hash.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/d3d12/d3d12_presenter.h>
#include <rex/ui/d3d12/d3d12_provider.h>

#include "game_profile.h"
#include "index_endian.h"
#include "../graphics/guest/constant_snapshot.h"
#include "../graphics/readback_rows.h"
#include "../graphics/motion_depth_boundary.h"
#include "../graphics/scene_color_vertex_input.h"
#include "../graphics/pixel_coverage_shader.h"
#include "../graphics/motion_raster_coverage.h"
#include "instance_probe.h"
#include "../graphics/observed_skinned_motion.h"
#include "../graphics/observed_skinned_part_key.h"
#include "../graphics/observed_cloth_motion.h"
#include "native_bridge.h"
#include "native_graphics_system.h"
#include "sdk_compat.h"
#include "sr_graphics_system.h"
#include "shader_pack.h"
#include "shader_registry.h"
#include "shader_translator.h"
#include "vertex_signature.h"
#include "texture_decode.h"
#include "texture_binding.h"

REXCVAR_DEFINE_INT32(sr_native_debug_spin_us, 0, "Superman Returns Native",
                     "Debug: busy-wait this many microseconds in every native draw entry (timing bisection)");
REXCVAR_DEFINE_INT32(sr_native_debug_resolve_mode, 0, "Superman Returns Native",
                     "Debug bisection: 1 = resolves skip copies, 2 = resolves skip clears");
REXCVAR_DEFINE_DOUBLE(sr_native_dump_frame_at_s, 0.0, "Superman Returns Native",
                      "Debug: at this bench time, read back every native surface and resolve "
                      "texture into sr_native_dump_dir (.raw; tools/native_dump_to_png.py)");
REXCVAR_DEFINE_INT32(sr_native_dump_after_pass, -1, "Superman Returns Native",
                     "Debug: with sr_native_dump_frame_at_s, dump at the end of this pass instead of "
                     "at the end of the frame");
REXCVAR_DEFINE_STRING(sr_native_dump_dir, "", "Superman Returns Native", "Directory for sr_native_dump_frame_at_s");
REXCVAR_DEFINE_STRING(sr_native_motion_capture_trigger_path, "", "Superman Returns Native",
    "Opt-in: capture two consecutive main-scene post-VS clip frames after this file appears; no DLSS application");
REXCVAR_DEFINE_BOOL(sr_native_debug_observed_skin_motion, false, "Superman Returns Native",
    "Diagnostic: observed skinned part/lifetime and exact clip correspondence; no complete DLSS motion claim");
REXCVAR_DEFINE_BOOL(sr_native_debug_motion_jitter, false, "Superman Returns Native",
    "Diagnostic: apply known Halton jitter to the two-frame HDR motion capture; never enables DLSS");
REXCVAR_DEFINE_BOOL(sr_native_debug_motion_coverage_pso_failure, false, "Superman Returns Native",
    "Diagnostic: force the optional motion coverage PSO to fail, exercising original-draw fallback");
REXCVAR_DEFINE_INT32(sr_native_dump_before_draw, -1, "Superman Returns Native",
                     "Debug: with sr_native_dump_frame_at_s, dump every native surface into "
                     "sr_native_dump_dir right before the Nth draw call (0-based, drawn or "
                     "skipped) of that frame, prefixed d<N>_");
REXCVAR_DEFINE_STRING(sr_native_dump_vs, "", "Superman Returns Native",
                      "Debug: with sr_native_dump_frame_at_s, dump every native surface right before the "
                      "first draw using this vertex shader (hex container hash, files before_*) and "
                      "right after it (after_*)");
REXCVAR_DEFINE_BOOL(sr_native_watch_blue, false, "Superman Returns Native",
                    "Debug: with sr_native_dump_frame_at_s, read the HDR target back after every draw "
                    "of one frame and log the draws that change the number of blue-dash pixels in "
                    "its lower part (slow)");
REXCVAR_DEFINE_STRING(sr_native_debug_ps, "", "Superman Returns Native",
                      "Debug: log the first float constants c0..c40 of draws using this pixel shader "
                      "(hex container hash)");
REXCVAR_DEFINE_STRING(sr_native_watch_xy, "640,50", "Superman Returns Native",
                      "Debug: pixel x,y of the HDR target whose changes sr_native_watch_blue logs");
REXCVAR_DEFINE_BOOL(sr_native_hdr_from_ldr_black, true, "Superman Returns Native",
                    "When the HDR target takes over EDRAM last written as an 8888 surface, start it "
                    "from black (0,0,0,1) instead of reinterpreting the bits: the cleared 8888 value "
                    "0xFF000000 reads as 7e3 (0,0,30,1), which distance-faded terrain blended into "
                    "blue dashes along curbs");
REXCVAR_DEFINE_BOOL(sr_native_debug_skip_reinterpret_draw, false, "Superman Returns Native",
                    "Debug (wrong pixels): skip the full-screen draw of EDRAM reinterpretation to time its cost");
REXCVAR_DEFINE_BOOL(sr_native_merge_7e3_surfaces, true, "Superman Returns Native",
                    "Share one host surface between the guest formats 2_10_10_10_FLOAT (3) and "
                    "2_10_10_10_FLOAT_AS_16_16_16_16 (12): they are the same 7e3 EDRAM encoding, and "
                    "keeping two surfaces cost a full-screen copy at every switch between them "
                    "(~35 per frame)");
REXCVAR_DEFINE_DOUBLE(sr_native_profile_frame_at_s, 0.0, "Superman Returns Native",
                      "Debug: after this many seconds since the first swap, time every command of one "
                      "frame on the GPU (timestamp after each draw/resolve/clear) and log the cost "
                      "per (vertex shader, pixel shader) pair and per operation");
REXCVAR_DEFINE_DOUBLE(sr_native_shadow_scale_mul, 1.0, "Superman Returns Native",
                      "Multiplier on the shadow map resolution (shadow_quality x this); below 1 trades "
                      "shadow sharpness for GPU time");
REXCVAR_DEFINE_INT32(sr_native_debug_nodraw_format, -1, "Superman Returns Native",
                     "Debug (wrong pixels): record every state of the draws that target this guest color "
                     "format but skip the draw call itself, to split state cost from draw cost");
REXCVAR_DEFINE_BOOL(sr_native_ignore_pack, false, "Superman Returns Native",
                    "Ignore the embedded shader pack (testing the runtime shader translator on a "
                    "development build)");
REXCVAR_DEFINE_INT32(sr_native_dump_before_resolve, -1, "Superman Returns Native",
                     "Debug: with sr_native_dump_frame_at_s, dump every native surface into "
                     "sr_native_dump_dir right before the Nth resolve (0-based) of that frame, "
                     "prefixed r<N>_ (for passes the game does not name)");
REXCVAR_DEFINE_DOUBLE(sr_native_trace_frame_at_s, 0.0, "Superman Returns Native",
                      "Debug: log render target binds, clears and resolves of one frame after "
                      "this many seconds");
REXCVAR_DEFINE_STRING(sr_native_trace_trigger_path, "", "Superman Returns Native",
                     "Debug: trace one frame when this file exists; empty disables file polling")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(sr_native_stacked_resolves, true, "Superman Returns Native",
                   "Experimental D3D12 stacked color resolves via Texture3D; mip 0, one sample")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(sr_native_buffer_srv_cache, 4096, "Superman Returns Native",
                    "Experimental D3D12 resident buffer SRV cache capacity (0 disables, max 8192)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(sr_native_pass_mask, "", "Superman Returns Native",
                      "Debug: comma-separated pass indices whose draws are rendered natively "
                      "(empty = all). Pass 0 = draws outside any pass.");
REXCVAR_DEFINE_BOOL(sr_native_buffer_cache_per_frame, false, "Superman Returns Native",
                    "Debug: drop cached vertex/index buffers every frame (stale data bisection)");
REXCVAR_DEFINE_BOOL(sr_native_ring_constants, true, "Superman Returns Native",
                    "Debug: mirror GpuBeginShaderConstantF4 ring constants into the shadow");
REXCVAR_DEFINE_STRING(sr_native_skip_draws, "", "Superman Returns Native",
                      "Debug: skip draws <pass>:<first>-<last> (per-frame draw index within the pass)");
REXCVAR_DEFINE_INT32(sr_native_dump_swap, 0, "Superman Returns Native",
                     "Debug: dump the frame presented by this guest swap number (see "
                     "bench_screenshot_swaps)");
REXCVAR_DEFINE_BOOL(sr_native_pm4_mirror, true, "Superman Returns Native",
                    "Take shader/fetch constants from the parsed command stream (GPU truth) "
                    "instead of the XDK device shadow");
REXCVAR_DEFINE_BOOL(sr_native_pm4_bounded_reads, true, "Superman Returns Native",
                    "Copy live PM4 constant sources with bounded checked reads instead of "
                    "VirtualQuery validation (D3D12, restart required; false = legacy path)");
REXCVAR_DEFINE_BOOL(sr_native_cpu_bundle, true, "Superman Returns Native",
                    "Experiment: combine verified texture tracking, bounded PM4 reads, CPU "
                    "upload staging and complete resolve reuse (D3D12, restart required)");
REXCVAR_DEFINE_BOOL(sr_native_shared_staging, true, "Superman Returns Native",
                    "Assemble shared constants in CPU memory before one upload (restart required)");
REXCVAR_DEFINE_BOOL(sr_native_resolve_identity_cache, true, "Superman Returns Native",
                    "Keep resolve allocation variants by complete identity (restart required)");
REXCVAR_DEFINE_BOOL(sr_native_packet_check, false, "Superman Returns Native",
                    "Development: decode captured neutral render packets alongside D3D12; "
                    "reports capture errors and does not select Vulkan");
REXCVAR_DEFINE_INT32(sr_native_packet_check_every, 120, "Superman Returns Native",
                     "Development: capture one frame every N swaps (1 captures every frame)");
REXCVAR_DEFINE_BOOL(sr_native_fixed16_snorm, false, "Superman Returns Native",
                    "Experiment: store EDRAM k_16_16_16_16 render targets as SNORM (clamp to "
                    "[-1, 1]) instead of float");
REXCVAR_DEFINE_INT32(sr_native_exp_alias_depth_base, -1, "Superman Returns Native",
                     "Experiment: clear the depth surface at this EDRAM base at its first bind "
                     "each frame");
REXCVAR_DEFINE_DOUBLE(sr_native_exp_alias_depth_value, 0.0, "Superman Returns Native",
                      "Experiment: depth value for sr_native_exp_alias_depth_base");
REXCVAR_DEFINE_STRING(sr_native_debug_ps_const, "", "Superman Returns Native",
                      "Debug: override a pixel shader constant, <ps hash>:<reg>:<x>,<y>,<z>,<w>");
REXCVAR_DEFINE_BOOL(sr_native_edram_aliasing, true, "Superman Returns Native",
                    "Clear host surfaces whose EDRAM range was overwritten by another surface "
                    "since their last use (no cross-frame feedback through aliased EDRAM)");
REXCVAR_DEFINE_BOOL(sr_native_tile_resolve_in_place, true, "Superman Returns Native",
                    "Tiling resolves with a (0,0) destination into a full-screen texture keep "
                    "the tile at its screen position");
REXCVAR_DEFINE_STRING(sr_native_dump_texture_addr, "", "Superman Returns Native",
                      "Debug: write the decoded texture at this physical base (hex) as DDS into "
                      "sr_native_dump_dir whenever it is (re)created");
REXCVAR_DEFINE_BOOL(sr_native_gamma_ramp, true, "Superman Returns Native",
                    "Apply the guest display gamma ramp when presenting the front buffer");
REXCVAR_DEFINE_INT32(sr_native_trace_swap, 0, "Superman Returns Native",
                     "Debug: trace the frame presented by this guest swap number");
REXCVAR_DEFINE_BOOL(sr_native_debug_no_present, false, "Superman Returns Native",
                    "Debug: render natively but never refresh the presenter's guest output");
REXCVAR_DEFINE_BOOL(sr_native_own_queue, true, "Superman Returns Native",
                    "Native renderer submits on its own D3D12 direct queue");
REXCVAR_DEFINE_STRING(sr_native_skip_ps, "", "Superman Returns Native",
                      "Debug: skip draws using this pixel shader (hex container hash)");
REXCVAR_DEFINE_DOUBLE(sr_native_edram_alias_alpha, 0.0, "Superman Returns Native",
                      "Alpha written into color surfaces whose EDRAM was clobbered by aliasing");
REXCVAR_DEFINE_BOOL(sr_native_edram_reinterpret, true, "Superman Returns Native",
                    "Surfaces sharing an EDRAM base with a newer surface of another 32bpp "
                    "format take its bits (reinterpreted) instead of keeping stale contents");
REXCVAR_DEFINE_BOOL(sr_native_texture_watch, true, "Superman Returns Native",
                    "Revalidate textures only after guest writes (page write watches) instead "
                    "of hashing them every frame");
REXCVAR_DEFINE_BOOL(sr_native_texture_hash_backoff, true, "Superman Returns Native",
                    "Back off hashing of unchanged small textures to 8 frames; "
                    "physical writes and untracked dynamic textures get prompt checks");
REXCVAR_DEFINE_INT32(sr_native_texture_gamma, 2, "Superman Returns Native",
                     "Xenos gamma textures (fetch sign = GAMMA): 0 = raw, 1 = sRGB views, "
                     "2 = Xenos piecewise-linear curve in the shader (descriptor index bit 31)");
REXCVAR_DEFINE_BOOL(sr_native_worker, true, "Superman Returns Native",
                    "Record D3D12 commands on a worker thread (guest threads only capture state; "
                    "requires sr_native_pm4_mirror)");
REXCVAR_DEFINE_BOOL(sr_native_worker_lag, true, "Superman Returns Native",
                    "Let the recording worker finish a frame while the guest starts the next one");
REXCVAR_DEFINE_BOOL(sr_native_async_pipelines, true, "Superman Returns Native",
                    "Pipelines missing from the cache compile in the background for passes the game "
                    "redraws every frame (scene geometry, shadows, transparents): the draw is skipped "
                    "for a few frames instead of stalling. Other passes wait (EXP-053)");
REXCVAR_DEFINE_BOOL(sr_native_alpha_to_coverage, true, "Superman Returns Native",
                    "Xenos alpha to mask (RB_COLORCONTROL) as host alpha to coverage on MSAA targets");
REXCVAR_DEFINE_STRING(sr_native_debug_vs, "", "Superman Returns Native",
                      "Debug: log streams, vertex range and first/last vertices of draws using this "
                      "vertex shader (hex container hash)");
REXCVAR_DEFINE_INT32(sr_native_debug_vb_addr, 0, "Superman Returns Native",
                     "Debug: log plans and first vertices of draws using this vertex buffer base");
REXCVAR_DEFINE_BOOL(sr_native_half_pixel_offset, true, "Superman Returns Native",
                    "Emulate Xenos D3D pixel centers (PA_SU_VTX_CNTL) with a half-pixel viewport shift");
REXCVAR_DEFINE_STRING(sr_native_ab_swaps, "", "Superman Returns Native",
                      "A/B: comma-separated guest swap numbers whose native output is dumped "
                      "(s<swap>_output_1280x720.raw in sr_native_dump_dir)");
REXCVAR_DEFINE_BOOL(sr_native_scissor, true, "Superman Returns Native",
                    "Apply the guest window/screen scissor (outside predicated tiling)");
REXCVAR_DEFINE_INT32(sr_native_debug_texture_format, -1, "Superman Returns Native",
                     "Debug: log every created texture of this Xenos format");
REXCVAR_DEFINE_BOOL(sr_native_debug_buffers_always_dirty, false, "Superman Returns Native",
                    "Debug: re-upload every draw's vertex/index range (ignores Unlock tracking)");
REXCVAR_DEFINE_BOOL(sr_native_debug_clears, false, "Superman Returns Native", "Debug: log resolve depth clears");
REXCVAR_DEFINE_BOOL(sr_native_debug_no_ztest, false, "Superman Returns Native", "Debug: disable all depth tests");
REXCVAR_DEFINE_DOUBLE(sr_native_render_scale, 1.0, "Superman Returns Native",
                      "D3D12 internal resolution multiplier of 720p (0.5 = 360p, "
                      "0.75 = 540p, 1 = 720p, 1.5 = 1080p, 2 = 1440p)")
    .range(0.5, 4.0)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(sr_native_temporal_probe, false, "Superman Returns Native",
                    "Diagnose missing D3D12 temporal inputs for future DLSS/RT (no visual effect)");
REXCVAR_DEFINE_INT32(sr_native_rt_capture_swap, 0, "Superman Returns Native",
                     "Extract audited post-VS world triangles on this guest swap for DXR validation (0=off)");
REXCVAR_DEFINE_STRING(sr_native_rt_capture_trigger_path, "", "Superman Returns Native",
                      "Extract audited post-VS world triangles once after this file appears (diagnostic, empty=off)");
REXCVAR_DEFINE_BOOL(sr_native_camera_candidate_probe, false, "Superman Returns Native",
                    "Diagnose gViewProjectionXform VS c0 on city draws (no visual effect)");
REXCVAR_DEFINE_INT32(sr_native_surface_probe_swap, 0, "Superman Returns Native",
                    "Diagnose D3D12 render-target/depth draw sequence near this guest swap (0 disables)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(sr_native_shadow_quality, 1, "Superman Returns Native",
                     "Shadow map resolution multiplier (1 = original 1024, 2 = 2048, 4 = 4096)")
    .range(1, 4);
REXCVAR_DEFINE_INT32(sr_native_msaa_samples, 1, "Superman Returns Native",
                     "MSAA samples for the surfaces the game multisamples (0 = game's 4x, "
                     "1 = off, 2, 4, 8; launcher: Off / 4x / 8x). Only the 640x640 shadow map is "
                     "multisampled in the scene; at 4x it cost ~7 ms of a ~35 ms GPU frame, off it "
                     "costs ~2.5 ms")
    .range(0, 8);
REXCVAR_DEFINE_BOOL(sr_native_full_scene_resolution, true, "Superman Returns Native",
                    "Scale scene surfaces by output width / game_profile kSceneWidth (Conan rendered "
                    "1024x576 stretched to 1280x720; SR renders 1280x720, so no effect)");
REXCVAR_DEFINE_BOOL(sr_native_foliage_antialiasing, false, "Superman Returns Native",
                    "Alpha-tested foliage/grilles use alpha-to-coverage with the game's MSAA "
                    "(antialiased, mip-corrected leaf edges) instead of a hard alpha test");
REXCVAR_DEFINE_BOOL(sr_native_ambient_occlusion, false, "Superman Returns Native",
                    "Screen-space ambient occlusion on the 3D scene");
REXCVAR_DEFINE_BOOL(sr_native_ssao_debug, false, "Superman Returns Native", "Debug: replace the scene with the SSAO term");
REXCVAR_DEFINE_DOUBLE(sr_native_ssao_radius, 0.5, "Superman Returns Native", "SSAO sampling radius in world units");
REXCVAR_DEFINE_DOUBLE(sr_native_ssao_intensity, 0.8, "Superman Returns Native", "SSAO strength (0-1)");
REXCVAR_DEFINE_DOUBLE(sr_native_ssao_fade_distance, 40.0, "Superman Returns Native",
                      "SSAO fades out towards this distance from the camera (world units)");
REXCVAR_DEFINE_BOOL(sr_native_soft_particles, false, "Superman Returns Native",
                    "Soft particles: blended effects fade near the geometry behind them");
REXCVAR_DEFINE_DOUBLE(sr_native_soft_particle_distance, 0.4, "Superman Returns Native",
                      "Soft particle fade distance in world units");
REXCVAR_DEFINE_INT32(sr_native_shadow_smoothing, 0, "Superman Returns Native",
                     "Shadow edge smoothing: 0 = off (original), 1 = on (per-pixel rotated PCF), "
                     "2 = softer (wider kernel)")
    .range(0, 2);
REXCVAR_DEFINE_BOOL(sr_native_smooth_effects, true, "Superman Returns Native",
                    "Above the original resolution, magnify small textures of blended effects "
                    "(particles, glows) with a smooth cubic filter instead of bilinear");
REXCVAR_DEFINE_BOOL(sr_native_fxaa, false, "Superman Returns Native",
                    "FXAA post-process antialiasing on the 3D image (the HUD stays sharp)");
REXCVAR_DEFINE_INT32(sr_native_bloom_quality, 1, "Superman Returns Native",
                     "Resolution multiplier of small post-process targets (bloom): 1 = original, 2, 4")
    .range(1, 4);
REXCVAR_DEFINE_INT32(sr_native_anisotropic_filtering, -1, "Superman Returns Native",
                     "Anisotropic filtering for filtered textures (-1 = game, 1 = off, 2, 4, 8, 16)")
    .range(-1, 16);
REXCVAR_DEFINE_INT32(sr_native_debug_rs_salt, 0, "Superman Returns Native",
                     "Debug: add an unused root constant block of N dwords (1-32) so every "
                     "pipeline misses the driver's shader cache (cold-start stutter tests)");
REXCVAR_DEFINE_BOOL(sr_native_debug_disable_driver_shader_cache, false, "Superman Returns Native",
                    "Debug: disable the driver's shader cache (simulates a first run; needs "
                    "Windows developer mode)");
REXCVAR_DEFINE_BOOL(sr_native_pipeline_cache, true, "Superman Returns Native",
                    "Precompile the known pipelines at startup and record new ones "
                    "(superman_returns_pipelines.bin) against shader-compilation stutter");
REXCVAR_DEFINE_INT32(sr_native_shadow_pcf_mode, 0, "Superman Returns Native",
                     "Debug: shadow atlas PCF at shadow_quality > 1 (0 = kernel on the host texel "
                     "grid, 1 = EXP-038 guest-texel kernel)");
REXCVAR_DEFINE_BOOL(sr_native_gpu_breadcrumbs, false, "Superman Returns Native",
                    "Debug: GPU hang breadcrumbs (a pipeline-draining marker write after every "
                    "draw; enable only to diagnose GPU hangs)");
REXCVAR_DEFINE_BOOL(sr_native_gpu_pass_timing, false, "Superman Returns Native",
                    "Debug: log GPU time per render pass (timestamp queries at pass changes)");
REXCVAR_DEFINE_BOOL(sr_native_gpu_timestamps, false, "Superman Returns Native",
                    "Collect frame GPU timestamps/readbacks for diagnostics (disabled by default)");
REXCVAR_DEFINE_BOOL(sr_native_msaa, true, "Superman Returns Native",
                    "Create host render targets with the guest's MSAA sample count");
REXCVAR_DEFINE_BOOL(sr_native_ab_mode, false, "Superman Returns Native",
                    "A/B validation: Xenos keeps rendering and presenting; the native renderer "
                    "renders the same frames offscreen (dumps only)");
REXCVAR_DEFINE_BOOL(sr_native_flip_winding, false, "Superman Returns Native", "Debug: invert front-face winding");
REXCVAR_DEFINE_BOOL(sr_native_draws, true, "Superman Returns Native", "Native renderer: issue draws (debug bisection)");
REXCVAR_DEFINE_BOOL(sr_native_resolves, true, "Superman Returns Native", "Native renderer: perform resolves (debug bisection)");
REXCVAR_DEFINE_STRING(sr_native_shader_dir, "", "Superman Returns Native",
                      "Directory with the offline shader corpus DXIL (<hash>.vs.dxil / .ps.dxil); "
                      "default: <exe>/../../../../artifacts/shaders/dxil");

namespace superman_returns::native {

using Microsoft::WRL::ComPtr;
namespace xenos = rex::graphics::xenos;
namespace reg = rex::graphics::reg;

int g_current_pass = 0;
int g_guest_pass = 0;

std::atomic<uint32_t> g_guest_device{0};
void NoteGuestDevice(uint32_t dev) {
  if (dev) g_guest_device.store(dev, std::memory_order_relaxed);
}

namespace {

bool CpuProfiling() {
  static const bool enabled=[] {
    for (const char* name : {"SR_NATIVE_CPU_PROFILE", "SR_VULKAN_PROFILE", "SR_NATIVE_TEXTURE_AUDIT", "SR_D3D12_TEXTURE_TRACKING_TIMING"}) {
      const char* value=std::getenv(name);
      if(value && *value && *value!='0') return true;
    }
    return false;
  }();
  return enabled;
}
bool ConstantDetailProfiling() {
  static const bool enabled=[] {
    const char* value=std::getenv("SR_NATIVE_CONSTANTS_PROFILE");
    return value && *value && *value!='0';
  }();
  return enabled && CpuProfiling();
}
// Diagnostic only: clocks and counters stay off unless both profile flags are on.
struct ConstantDetailTimings {
  uint64_t flush_ns=0,shared_alloc_ns=0,vs_alloc_ns=0,vs_copy_ns=0,ps_alloc_ns=0,ps_copy_ns=0;
  uint64_t vs_capture_ns=0,vs_write_ns=0,ps_capture_ns=0,ps_write_ns=0;
  uint64_t setup_ns=0,derived_ns=0,bindings_ns=0;
  uint64_t draws=0,mirror_draws=0,vs_uploads=0,vs_reuses=0,ps_uploads=0,ps_reuses=0;
  uint64_t vs_bytes=0,ps_bytes=0,allocation_failures=0,ring_requests=0;
};
thread_local ConstantDetailTimings constant_detail_timings;
bool StreamDetailProfiling() {
  static const bool enabled=[] {
    const char* value=std::getenv("SR_NATIVE_STREAMS_PROFILE");
    return value && *value && *value!='0';
  }();
  return enabled && CpuProfiling();
}
struct StreamDetailTimings {
  uint64_t setup_ns=0,apply_ns=0,ia_ns=0,srv_ns=0,declaration_ns=0,metadata_ns=0;
  uint64_t calls=0,streams=0,ia_binds=0,missing=0,failed_calls=0,dynamic_streams=0;
  uint64_t srv_created=0,srv_reused=0,srv_failed=0,declaration_reads=0,attributes=0,metadata_writes=0;
  uint64_t srv_retained_hits=0,cache_evicted=0,cache_invalidated=0;
  uint64_t actions[4]{}; // Plan actions 0/1/2/other, not actual upload counts.
};
thread_local StreamDetailTimings stream_detail_timings;
bool NativeTraceTriggered() {
  static const std::filesystem::path path=REXCVAR_GET(sr_native_trace_trigger_path);
  if (path.empty()) return false;
  std::error_code error;
  return std::filesystem::exists(path,error) && !error;
}
struct CpuTimings {
  uint64_t capture_ns=0,wait_ns=0,execute_ns=0,prepare_ns=0,constants_ns=0,streams_ns=0,swaps=0;
  uint64_t surfaces_ns=0,render_targets_ns=0,pipeline_ns=0,fixed_state_ns=0;
  uint64_t constant_float_ns=0,constant_slots_ns=0,texture_lookup_ns=0,texture_hash_ns=0,sampler_ns=0;
  uint64_t texture_hash_bytes=0,texture_hash_checks=0,slot_hits=0,slot_misses=0,constant_draws=0;
  uint64_t texture_watch_ns=0,texture_watch_rearms=0,texture_hash_changes=0;
  uint64_t shared_hits=0,shared_uploads=0,shared_upload_ns=0;
  std::chrono::steady_clock::time_point capture_start;
};
thread_local CpuTimings cpu_timings;
CpuTimings front_cpu_timings; // Accessed only under Renderer::front_mutex_.
bool TextureAuditing() {
  static const bool enabled=[] {
    const char* value=std::getenv("SR_NATIVE_TEXTURE_AUDIT");
    return value && *value && *value!='0';
  }();
  return enabled;
}
bool SharedConstantsCaching() {
  static const bool enabled=[] {
    const char* value=std::getenv("SR_NATIVE_SHARED_CONSTANTS_CACHE");
    return value && *value && *value!='0';
  }();
  return enabled;
}
thread_local TextureWriteAudit texture_write_audit;
thread_local uint32_t texture_audit_details=0;
void AuditTextureHash(const char* api,uint64_t frame,uint32_t address,uint64_t bytes,
                      uint32_t format,bool changed,bool notified,bool watch_active) {
  texture_write_audit.Record(bytes,changed,notified);
  // Bound detailed logging for changing video planes. Aggregate counters below
  // continue after this limit; lack of a notification never disables hashing.
  if(changed && !notified && texture_audit_details<64) {
    ++texture_audit_details;
    REXLOG_INFO("native texture audit unnotified: api={} frame={} base={:08X} hash_bytes={} format={} watch_active={}",api,frame,address,bytes,format,watch_active);
  }
}
void FlushTextureAudit(const char* api,uint64_t frame) {
  if(!TextureAuditing() || !texture_write_audit.checks) return;
  const auto& a=texture_write_audit;
  REXLOG_INFO("native texture audit interval: api={} frame={} checks={} hash_kb={} changes={} unnotified_changes={} unnotified_kb={}",api,frame,a.checks,a.bytes/1024,a.changes,a.unnotified_changes,a.unnotified_bytes/1024);
  texture_write_audit={};
}
struct CpuTimer {
  uint64_t& total;
  bool active;
  std::chrono::steady_clock::time_point start;
  explicit CpuTimer(uint64_t& value, bool enabled=CpuProfiling())
      :total(value),active(enabled),start(enabled?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{}) {}
  void Stop() {if(active) {total+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());active=false;}}
  ~CpuTimer() {Stop();}
};

struct CaptureTimings {
  uint64_t reads=0,bytes=0,queries=0,pm4_us=0,textures_us=0;
  uint64_t watch_us=0,read_us=0,hash_us=0,copy_us=0;
  uint64_t new_entries=0,watch_dirty=0,revalidated=0,changed=0;
  uint64_t watch_scan_hits=0,watch_scan_misses=0;
  uint64_t fe_hook_ns=0,fe_begin_ns=0,fe_ring_ns=0,fe_device_ns=0,fe_index_ns=0,fe_streams_ns=0,fe_end_ns=0,fe_flush_ns=0;
  uint64_t fd_shaders_ns=0,fs_prep_ns=0,fs_plan_ns=0,fe_push_ns=0;
  uint64_t fe_pm4_ns=0,fe_textures_ns=0,fs_resolve_ns=0,fs_buffer_ns=0;
  uint64_t fb_refresh_ns=0,fb_hash_ns=0;
  uint64_t fp_primary_ns=0,fp_read_ns=0,fp_copy_ns=0;
};
thread_local CaptureTimings capture_timings;
// SR_FRAME_TIMELINE_DETAIL=1: nanosecond accumulator for one front-end region of the game thread.
// Reads no clock unless the detail mode is on.
struct DetailScope {
  uint64_t& total;
  bool active = graphics::FrameTimeline::Detail();
  uint64_t start = active ? graphics::FrameTimeline::NowNs() : 0;
  explicit DetailScope(uint64_t& value) : total(value) {}
  void Stop() {
    if (active) {total += graphics::FrameTimeline::NowNs() - start;active = false;}
  }
  ~DetailScope() {Stop();}
  DetailScope(const DetailScope&) = delete;
  DetailScope& operator=(const DetailScope&) = delete;
};
// SR_FRAME_TIMELINE_DETAIL=1: why PlanStreams takes its slow path (first failing reason per stream),
// accumulated on the guest thread and logged every 120 frames. Counts only; no timing.
struct StreamPlanCounters {
  uint64_t evaluated=0,fast=0,slow=0,secondary_hits=0;
  uint64_t untracked=0,address=0,size=0,decl=0,stride=0,phase=0,dirty=0;
  uint64_t refresh_calls=0,refresh_hashes=0,refresh_hash_bytes=0;
  uint64_t pm4_reads=0,pm4_bytes=0;
};
thread_local StreamPlanCounters stream_plan_counters;
thread_local CheckedGuestReads checked_guest_reads;
bool TextureWatchScanCaching() {
  static const bool enabled=[] {
    const char* value=std::getenv("SR_NATIVE_TEXTURE_WATCH_SCAN_CACHE");
    return !value || !*value || *value!='0';
  }();
  return enabled;
}
bool SmallBufferHashEveryUse() {
  static const bool enabled=[] {
    const char* value=std::getenv("SR_NATIVE_SMALL_BUFFER_HASH_EVERY_USE");
    return value && *value && *value!='0';
  }();
  return enabled;
}
bool TextureSlotLookupCaching() {
  static const bool enabled=[] {
    const char* value=std::getenv("SR_NATIVE_TEXTURE_SLOT_CACHE");
    return !value || !*value || *value!='0';
  }();
  return enabled;
}
// Ponteiro do host para uma faixa do jogo, ou nullptr se ela cai fora do espaÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â§o de endereÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â§os.
const uint8_t* GuestSource(uint8_t* base, uint32_t address, uint32_t length) {
  if(uint64_t(address)+length>(uint64_t{1}<<32)) return nullptr;
  if(address>=0xa0000000u && address<0xc0000000u) {
    uint32_t physical=address-0xa0000000u;
    if(uint64_t(physical)+length>0x20000000ull) return nullptr;
    // PM4 references GPU physical memory. A cached virtual alias may be
    // PAGE_NOACCESS while the SDK's physical mapping remains committed.
    return REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(physical);
  }
  return base+address;
}
uint64_t Xxh3Baseline(const void* data,size_t size,uint64_t seed) {return uint64_t(XXH3_64bits_withSeed(data,size,seed));}
using Xxh3Fn=uint64_t(*)(const void*,size_t,uint64_t);
// The same XXH3 value on every CPU, with AVX2 for bulk texture/buffer data when
// available. Static initialization is thread-safe for both native APIs;
// SR_NATIVE_HASH_NO_AVX2=1 forces the baseline for comparisons.
Xxh3Fn TextureHash() {
  static const Xxh3Fn fn=[]() -> Xxh3Fn {
    const char* off=std::getenv("SR_NATIVE_HASH_NO_AVX2");
    #if defined(__AVX2__) || defined(__x86_64__) || defined(_M_X64)
    const bool avx2=superman_returns::native::Avx2Available() && !(off && off[0] && off[0]!='0');
    return avx2?&superman_returns::native::Xxh3Avx2:&Xxh3Baseline;
#else
    return &Xxh3Baseline;
#endif
  }();
  return fn;
}
bool TextureHashReplayEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("SR_NATIVE_TEXTURE_REPLAY");
    return value && *value && *value != '0';
  }();
  return enabled;
}
uint64_t TextureHashReplayStartFrame() {
  static const uint64_t frame = [] {
    const char* value = std::getenv("SR_NATIVE_TEXTURE_REPLAY_START_FRAME");
    if (!value || !*value) return uint64_t(1800);
    char* end = nullptr;
    const uint64_t parsed = std::strtoull(value, &end, 10);
    return end != value && !*end && parsed <= 1000000 ? parsed : uint64_t(1800);
  }();
  return frame;
}
struct TextureReplayCorpus {
  bool done = false;
  uint64_t first_frame = 0, bytes = 0;
  uint32_t first_guest_protect = 0, first_snapshot_protect = 0;
  bool mixed_guest_protection = false;
  std::set<std::array<uint32_t, 6>> seen;
  std::vector<std::shared_ptr<const graphics::guest::TextureCapture>> owners;
  std::vector<TextureHashReplayCase> cases;
};
thread_local TextureReplayCorpus texture_replay;
void FinishTextureHashReplay(const char* api, uint64_t frame) {
  auto& corpus = texture_replay;
  if (corpus.done || corpus.cases.empty()) return;
  corpus.done = true;
  uint64_t level_bytes = 0, union_bytes = 0, level_ranges = 0, union_ranges = 0;
  uint32_t overlap_cases = 0;
  for (const auto& sample : corpus.cases) {
    uint64_t before = 0, after = 0;
    for (auto bytes : sample.levels) before += bytes.size();
    for (auto bytes : sample.united) after += bytes.size();
    level_bytes += before;
    union_bytes += after;
    level_ranges += sample.levels.size();
    union_ranges += sample.united.size();
    overlap_cases += before > after;
  }
  const auto result = ReplayTextureHashes(corpus.cases, TextureHash());
  // Identical-policy control estimates timing/order noise on this same corpus.
  auto control = corpus.cases;
  for (auto& sample : control) sample.levels = sample.united;
  const auto noise = ReplayTextureHashes(control, TextureHash());
  REXLOG_INFO("native texture replay: api={} frame={} first_frame={} cases={} overlap_cases={} level_ranges={} union_ranges={} level_kb={} union_kb={} repetitions=4 trials=9 level_us={:.3f} union_us={:.3f} control_a_us={:.3f} control_b_us={:.3f} stable={} hash={} first_guest_protect={:X} first_snapshot_protect={:X} mixed_guest_protection={}",
      api, frame, corpus.first_frame, corpus.cases.size(), overlap_cases,
      level_ranges, union_ranges, level_bytes / 1024, union_bytes / 1024,
      result.level_ns / 1000, result.union_ns / 1000,
      noise.level_ns / 1000, noise.union_ns / 1000,
      result.stable && noise.stable && noise.level_digest == noise.union_digest,
      TextureHash() == &Xxh3Baseline ? "baseline" : "avx2", corpus.first_guest_protect,
      corpus.first_snapshot_protect, corpus.mixed_guest_protection);
  // Each digest belongs to its own range partition, not a cross-policy equality test.
  REXLOG_INFO("native texture replay digests: api={} level={:016X} union={:016X} control_a={:016X} control_b={:016X}",
      api, result.level_digest, result.union_digest, noise.level_digest, noise.union_digest);
  corpus.cases.clear();
  corpus.owners.clear();
}
void MaybeReplayTextureHash(std::span<const uint32_t, 6> fetch, uint64_t frame,
    const char* api, std::shared_ptr<const graphics::guest::TextureCapture> snapshot = {}) {
  auto& corpus = texture_replay;
  if (corpus.done || frame < TextureHashReplayStartFrame()) return;
  try {
    if (!corpus.cases.empty() && (corpus.cases.size() >= 256 ||
        corpus.bytes >= (64u << 20) || frame >= corpus.first_frame + 120 || corpus.seen.size() >= 512)) {
      FinishTextureHashReplay(api, frame);
      return;
    }
    std::array<uint32_t, 6> key;
    std::copy(fetch.begin(), fetch.end(), key.begin());
    if (corpus.seen.size() >= 512 || !corpus.seen.insert(key).second) return;
    std::vector<graphics::guest::TextureRange> ranges;
    std::string error;
    if (!graphics::guest::DescribeTextureRanges(fetch, ranges, error)) return;
    const uint64_t bytes = TextureContentBytes(ranges);
    if (!bytes || bytes > (4u << 20)) return;
    if (corpus.bytes + bytes > (64u << 20)) { FinishTextureHashReplay(api, frame); return; }
    if (!snapshot) {
      // Local checked reads and the resulting capture are diagnostic-only.
      // They never replace content hashes, GPU resources or frontend snapshots.
      CheckedGuestReads reads;
      if (!graphics::guest::CaptureTexture(fetch, frame,
          [&reads](uint32_t address, uint32_t length) {
            return reads.Read(REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(address & 0x1fffffffu), length);
          }, snapshot, error)) return;
    }
    TextureHashReplayCase sample;
    graphics::guest::LinearTexture decoded;
    if (!graphics::guest::DecodeTextureLayoutUsing(fetch,
        [&](uint32_t address, uint32_t length) {
          auto data = snapshot->memory.Read(address, length);
          sample.levels.push_back(data);
          return data;
        }, decoded, error)) return;
    for (const auto& range : snapshot->ranges)
      sample.united.push_back(snapshot->memory.Read(0xa0000000u + range.address, range.length));
    for (const auto& range : snapshot->ranges) {
      MEMORY_BASIC_INFORMATION info{};
      auto* source = REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(range.address);
      if (VirtualQuery(source, &info, sizeof(info))) {
        if (!corpus.first_guest_protect) corpus.first_guest_protect = info.Protect;
        else if (info.Protect != corpus.first_guest_protect) corpus.mixed_guest_protection = true;
      }
    }
    if (corpus.cases.empty()) {
      corpus.first_frame = frame;
      MEMORY_BASIC_INFORMATION info{};
      if (VirtualQuery(sample.united.front().data(), &info, sizeof(info)))
        corpus.first_snapshot_protect = info.Protect;
    }
    corpus.bytes += bytes;
    corpus.owners.push_back(std::move(snapshot));
    corpus.cases.push_back(std::move(sample));
  } catch (const std::exception& error) {
    corpus.done = true;
    corpus.cases.clear();
    corpus.owners.clear();
    REXLOG_WARN("native texture replay failed: api={} error={}", api, error.what());
  }
}
bool TextureSourceProbing() {
  static const bool enabled = [] {
    const char* value = std::getenv("SR_NATIVE_TEXTURE_SOURCE_PROBE");
    return value && *value && *value != '0';
  }();
  return enabled;
}
struct TextureSourceProbeStats {
  bool done = false;
  uint64_t checks = 0, selected = 0, stable = 0, changed = 0, unreadable = 0, excluded = 0;
  uint64_t copied_bytes = 0, stable_bytes = 0;
  double original_ns = 0, guest_ns = 0, owned_ns = 0, copy_ns = 0;
  uint32_t guest_protect = 0, owned_protect = 0;
  std::set<uint32_t> addresses;
};
thread_local TextureSourceProbeStats texture_source_probe;
bool SelectTextureSourceProbe(const char* api, uint64_t frame, uint64_t bytes) {
  auto& s = texture_source_probe;
  if (s.done || frame < TextureHashReplayStartFrame()) return false;
  if (frame >= TextureHashReplayStartFrame() + 120 || s.selected >= 1024 ||
      s.copied_bytes >= (256u << 20)) {
    s.done = true;
    REXLOG_INFO("native texture source probe: api={} frame={} checks={} selected={} stable={} changed={} unreadable={} excluded={} unique_bases={} copied_kb={} stable_kb={} original_ms={:.3f} guest_ms={:.3f} owned_ms={:.3f} copy_ms={:.3f} guest_protect_or={:X} owned_protect_or={:X} stride=32",
        api, frame, s.checks, s.selected, s.stable, s.changed, s.unreadable, s.excluded,
        s.addresses.size(), s.copied_bytes / 1024, s.stable_bytes / 1024,
        s.original_ns / 1e6, s.guest_ns / 1e6, s.owned_ns / 1e6, s.copy_ns / 1e6,
        s.guest_protect, s.owned_protect);
    return false;
  }
  if ((s.checks++ % 32) != 0) return false;
  if (!bytes || bytes > (4u << 20) || s.copied_bytes + bytes > (256u << 20)) {
    ++s.excluded;
    return false;
  }
  ++s.selected;
  return true;
}
template <typename Source>
void MeasureTextureSource(std::span<const graphics::guest::TextureRange> ranges,
    Source&& source, uint64_t seed, uint64_t expected, double original_ns) {
  auto& s = texture_source_probe;
  try {
    CheckedGuestReads reads;
    std::vector<std::span<const uint8_t>> owned;
    owned.reserve(ranges.size());
    const auto started = std::chrono::steady_clock::now();
    for (const auto& range : ranges) {
      auto bytes = reads.Read(source(range.address, range.length), range.length);
      if (bytes.size() != range.length) { ++s.unreadable; return; }
      owned.push_back(bytes);
      s.copied_bytes += bytes.size();
    }
    const double copy_ns = std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - started).count();
    auto guest_hash = [&](uint64_t& value) {
      value = seed;
      for (const auto& range : ranges)
        if (!HashGuestRange(source(range.address, range.length), range.length, value, TextureHash(), value)) return false;
      return true;
    };
    auto owned_hash = [&](uint64_t& value) {
      value = seed;
      for (auto bytes : owned)
        if (!HashGuestRange(bytes.data(), uint32_t(bytes.size()), value, TextureHash(), value)) return false;
      return true;
    };
    const auto result = ProbeTextureSource(expected, (s.selected & 1) != 0, guest_hash, owned_hash);
    if (!result.readable) { ++s.unreadable; return; }
    if (!result.stable) { ++s.changed; return; }
    ++s.stable;
    s.stable_bytes += TextureContentBytes(ranges);
    s.original_ns += original_ns;
    s.guest_ns += result.guest_ns;
    s.owned_ns += result.owned_ns;
    s.copy_ns += copy_ns;
    s.addresses.insert(ranges.front().address);
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(source(ranges.front().address, ranges.front().length), &info, sizeof(info)))
      s.guest_protect |= info.Protect;
    if (VirtualQuery(owned.front().data(), &info, sizeof(info))) s.owned_protect |= info.Protect;
  } catch (const std::exception& error) {
    s.done = true;
    REXLOG_WARN("native texture source probe failed: error={}", error.what());
  }
}
std::span<const uint8_t> ReadCommittedGuest(uint8_t* base, uint32_t address, uint32_t length) {
  ++capture_timings.reads;capture_timings.bytes+=length;
  const uint8_t* source=GuestSource(base,address,length);
  return source?checked_guest_reads.Read(source,length):std::span<const uint8_t>{};
}

// D3DDevice layout and the device pointer come from game_profile.h (Conan's
// values there, unconfirmed for Superman Returns).
constexpr const profile::DeviceLayout& kDev = profile::kDevice;
constexpr uint32_t kDevFetchConstants = kDev.fetch_constants;  // [32] x 24 bytes
constexpr uint32_t kDevVsConstants = kDev.vs_constants;        // 256 float4
constexpr uint32_t kDevPsConstants = kDev.ps_constants;        // 256 float4
constexpr uint32_t kDevVsBools = kDev.vs_bools;                // 4 dwords
constexpr uint32_t kDevPsBools = kDev.ps_bools;                // 4 dwords
constexpr uint32_t kDevVsLoops = kDev.vs_loops;                // 16 dwords
constexpr uint32_t kDevPsLoops = kDev.ps_loops;                // 16 dwords
constexpr uint32_t kDevShaderA = kDev.shader_a;
constexpr uint32_t kDevShaderB = kDev.shader_b;
constexpr uint32_t kDevVertexDecl = kDev.vertex_decl;
constexpr uint32_t kDevViewport = kDev.viewport;  // X, Y, W, H, MinZ, MaxZ (f32)
constexpr uint32_t kDevRenderTargets = kDev.render_targets;  // [4] surfaces
constexpr uint32_t kDevDepthStencil = kDev.depth_stencil;
// Vertex stream s uses the 8-byte vertex fetch constant at 0x778 - 8 * s
// (the top of the fetch constant file).
constexpr uint32_t kDevVertexFetch0 = kDev.fetch_constants + 0x2F8;
// XDK D3DDevice_Clear flags: D3DCLEAR_TARGET0..3 = bits 0-3, ZBUFFER 0x10,
// STENCIL 0x20 (the game passes 0xF = color only and 0x3F = everything).
constexpr uint32_t kClearZBuffer = 0x10;
constexpr uint32_t kClearStencil = 0x20;

// Register shadow -> Xenos register (section 8).
uint32_t RegShadowOffset(uint32_t reg_index) {
  for (const profile::RegisterShadowRange& r : profile::kRegisterShadow) {
    if (reg_index >= r.first && reg_index < r.first + r.count) {
      return r.offset + 4 * (reg_index - r.first);
    }
  }
  return 0;
}

// Guest addresses appear both as physical (device fetch slots) and as the
// 0xA0000000-0xFFFFFFFF physical views (texture objects, surfaces). Normalize
// to physical: low 29 bits, +4 KB for the 0xE0000000 view (XDK convention).
inline uint32_t GuestPhysical(uint32_t address) {
  return (address & 0x1FFFFFFFu) + (address >= 0xE0000000u ? 0x1000u : 0u);
}

// Guest memory captured with the worker command being executed (empty on the
// guest threads and in direct mode): reads of captured ranges see the guest
// state at capture time instead of the live memory.
thread_local const CaptureRange* t_capture_ranges = nullptr;
thread_local uint32_t t_capture_count = 0;
thread_local const uint8_t* t_capture_bytes = nullptr;
thread_local size_t t_capture_bytes_size = 0;

inline const uint8_t* GuestPtr(uint8_t* base, uint32_t addr, uint32_t len) {
  for (uint32_t i = t_capture_count; i-- > 0;) {
    const CaptureRange& r = t_capture_ranges[i];
    uint32_t d = addr - r.address;
    if (d < r.length && len <= r.length - d) return t_capture_bytes + r.offset + d;
  }
  return base + addr;
}
inline uint32_t Load32(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, GuestPtr(base, addr, 4), 4);
  return __builtin_bswap32(v);
}
inline uint32_t Load16(uint8_t* base, uint32_t addr) {
  uint16_t v;
  std::memcpy(&v, GuestPtr(base, addr, 2), 2);
  return __builtin_bswap16(v);
}
inline uint32_t Load8(uint8_t* base, uint32_t addr) { return *GuestPtr(base, addr, 1); }

// Conan read the device from a global (0x82C81A64). SR's global is unknown:
// with profile::kDevicePtrAddr == 0 the device is the r3 argument the D3D
// hooks saw last (the XDK has one device per process; NoteGuestDevice).
inline uint32_t GuestDevice(uint8_t* base) {
  if constexpr (profile::kDevicePtrAddr != 0) {
    return Load32(base, profile::kDevicePtrAddr);
  } else {
    return g_guest_device.load(std::memory_order_relaxed);
  }
}

// Texture fetch constant with RGB sign = GAMMA (dword 0 bits 2-7).
inline bool IsGammaFetch(const uint32_t fetch[6]) {
  return ((fetch[0] >> 2) & 3) == 3 && ((fetch[0] >> 4) & 3) == 3 && ((fetch[0] >> 6) & 3) == 3;
}
inline float LoadF32(uint8_t* base, uint32_t addr) {
  uint32_t v = Load32(base, addr);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
uint32_t LoadReg(uint8_t* base, uint32_t dev, uint32_t reg_index) {
  uint32_t off = RegShadowOffset(reg_index);
  return off ? Load32(base, dev + off) : 0;
}

// Xbox D3DDECLTYPE -> DXGI and the swap unit of its guest (big-endian) data.
struct DeclFormat {
  DXGI_FORMAT format;
  uint32_t size;
  uint32_t swap;  // bytes per swapped word
};
DXGI_FORMAT MapDeclHostFormat(uint32_t type) {
  switch (type) {
    case 0x2C83A4: return DXGI_FORMAT_R32_FLOAT;
    case 0x2C23A5: return DXGI_FORMAT_R32G32_FLOAT;
    case 0x2A23B9: return DXGI_FORMAT_R32G32B32_FLOAT;
    case 0x1A23A6: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case 0x182886: return DXGI_FORMAT_B8G8R8A8_UNORM;  // D3DCOLOR
    case 0x1A2286:
    case 0x1A2386: return DXGI_FORMAT_R8G8B8A8_UINT;
    case 0x1A2086:
    case 0x1A2186: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case 0x2C2359: return DXGI_FORMAT_R16G16_SINT;
    // USHORT2 is unnormalized. The VS restores integer coordinates from the
    // float-compatible UNORM input via srVertexInput and declaration metadata.
    case 0x2C2259: return DXGI_FORMAT_R16G16_UNORM;
    case 0x1A235A: return DXGI_FORMAT_R16G16B16A16_SNORM;
    case 0x2C2159: return DXGI_FORMAT_R16G16_SNORM;
    case 0x1A215A: return DXGI_FORMAT_R16G16B16A16_SNORM;
    case 0x2C2059: return DXGI_FORMAT_R16G16_UNORM;
    case 0x1A205A: return DXGI_FORMAT_R16G16B16A16_UNORM;
    case 0x2C82A1: return DXGI_FORMAT_R32_UINT;
    case 0x2A2187:  // DEC3N: signed normalized 10/10/10, decoded by the VS.
    case 0x2A2190:
    case 0x2A2390: return DXGI_FORMAT_R32_UINT;
    case 0x2C235F: return DXGI_FORMAT_R16G16_FLOAT;
    case 0x1A2360: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
  }
}

DeclFormat MapDeclType(uint32_t type) {
  auto encoding=graphics::guest::GetVertexEncoding(type);
  return {MapDeclHostFormat(type),encoding.size,encoding.swap};
}
const graphics::guest::VertexDeclaration& ReadVertexDeclarationInfo(uint8_t* base,uint32_t decl) {
  static const graphics::guest::VertexDeclaration empty;
  if(!decl) return empty;
  uint32_t count=std::min(Load32(base,decl+0x18),64u);
  if(!count) return empty;
  thread_local graphics::guest::VertexDeclarationCache cache;
  auto bytes=t_capture_count?graphics::guest::FindUnambiguousCapture(
      {t_capture_ranges,t_capture_count},{t_capture_bytes,t_capture_bytes_size},decl+0x34,count*12)
      :std::span<const uint8_t>(base+decl+0x34,count*12);
  if(!bytes.empty()) return cache.Decode(bytes);
  // Partial overlays must keep the scalar read precedence of the capture shim.
  thread_local graphics::guest::VertexDeclaration fallback;
  fallback.attributes.clear();
  fallback.streams_used = 0;
  fallback.normal_spec_bits = 0;
  for(uint32_t i=0;i<count;++i) {
    uint32_t a=decl+0x34+i*12,stream=Load16(base,a);
    if(stream==0xff) break;
    uint32_t offset=Load16(base,a+2),type=Load32(base,a+4);
    uint32_t usage=Load8(base,a+9),index=Load8(base,a+10);
    fallback.attributes.push_back({stream,offset,type,usage,index});
    if(stream<32) fallback.streams_used|=1u<<stream;
    if(usage==3 || usage==6 || usage==7) {
      if(type==0x2a2187) fallback.normal_spec_bits|=1u<<6;
      if(type==0x2a2190 || type==0x2a2390) fallback.normal_spec_bits|=1u;
    }
  }
  return fallback;
}
const std::vector<graphics::guest::VertexAttribute>& ReadVertexDeclaration(uint8_t* base,uint32_t decl) {
  return ReadVertexDeclarationInfo(base,decl).attributes;
}

const char* UsageSemantic(uint32_t usage) {
  switch (usage) {
    case 0: return "POSITION";
    case 1: return "BLENDWEIGHT";
    case 2: return "BLENDINDICES";
    case 3: return "NORMAL";
    case 4: return "PSIZE";
    case 5: return "TEXCOORD";
    case 6: return "TANGENT";
    case 7: return "BINORMAL";
    case 8: return "TESSFACTOR";
    case 10: return "COLOR";
    case 11: return "FOG";
    case 12: return "DEPTH";
    case 13: return "SAMPLE";
    default: return "TEXCOORD";
  }
}

// Semantic inputs (name + index) declared by a vertex shader: the ISG1 chunk
// of its DXBC container (u32 count, u32 offset, then 32-byte elements whose
// first fields are stream, name offset, index; the names follow). Empty when
// the blob has no such chunk, which turns the filtering off.

D3D12_BLEND MapBlend(xenos::BlendFactor f) {
  using BF = xenos::BlendFactor;
  switch (f) {
    case BF::kZero: return D3D12_BLEND_ZERO;
    case BF::kOne: return D3D12_BLEND_ONE;
    case BF::kSrcColor: return D3D12_BLEND_SRC_COLOR;
    case BF::kOneMinusSrcColor: return D3D12_BLEND_INV_SRC_COLOR;
    case BF::kSrcAlpha: return D3D12_BLEND_SRC_ALPHA;
    case BF::kOneMinusSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
    case BF::kDstColor: return D3D12_BLEND_DEST_COLOR;
    case BF::kOneMinusDstColor: return D3D12_BLEND_INV_DEST_COLOR;
    case BF::kDstAlpha: return D3D12_BLEND_DEST_ALPHA;
    case BF::kOneMinusDstAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
    case BF::kConstantColor: return D3D12_BLEND_BLEND_FACTOR;
    case BF::kOneMinusConstantColor: return D3D12_BLEND_INV_BLEND_FACTOR;
    case BF::kConstantAlpha: return D3D12_BLEND_BLEND_FACTOR;
    case BF::kOneMinusConstantAlpha: return D3D12_BLEND_INV_BLEND_FACTOR;
    case BF::kSrcAlphaSaturate: return D3D12_BLEND_SRC_ALPHA_SAT;
    default: return D3D12_BLEND_ONE;
  }
}
D3D12_BLEND MapBlendAlpha(xenos::BlendFactor f) {
  D3D12_BLEND b = MapBlend(f);
  switch (b) {
    case D3D12_BLEND_SRC_COLOR: return D3D12_BLEND_SRC_ALPHA;
    case D3D12_BLEND_INV_SRC_COLOR: return D3D12_BLEND_INV_SRC_ALPHA;
    case D3D12_BLEND_DEST_COLOR: return D3D12_BLEND_DEST_ALPHA;
    case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
    default: return b;
  }
}
D3D12_BLEND_OP MapBlendOp(xenos::BlendOp op) {
  switch (op) {
    case xenos::BlendOp::kSubtract: return D3D12_BLEND_OP_SUBTRACT;
    case xenos::BlendOp::kMin: return D3D12_BLEND_OP_MIN;
    case xenos::BlendOp::kMax: return D3D12_BLEND_OP_MAX;
    case xenos::BlendOp::kRevSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
    default: return D3D12_BLEND_OP_ADD;
  }
}
D3D12_TEXTURE_ADDRESS_MODE MapClamp(uint32_t clamp) {
  switch (clamp) {
    case 0: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case 1: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case 3:
    case 5:
    case 7: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
    case 6: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    default: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  }
}

}  // namespace

namespace {
// Shader pack embedded in superman_returns.exe (RCDATA 2, tools/shaders/pack_shaders.py);
// format and lookup in shader_pack.h.
const ShaderPackView& EmbeddedShaderPack() {
  static const ShaderPackView pack = [] {
    ShaderPackView p;
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(module, MAKEINTRESOURCEW(2), MAKEINTRESOURCEW(10));  // RT_RCDATA
    if (!res) return p;
    HGLOBAL handle = LoadResource(module, res);
    const uint8_t* data = handle ? static_cast<const uint8_t*>(LockResource(handle)) : nullptr;
    p.Parse(data, SizeofResource(module, res));
    return p;
  }();
  return pack;
}

// Default loose-file corpus: <repo>/artifacts/shaders/dxil for an executable
// in port/out/build/<preset>/ (same relative path as the kit).
std::filesystem::path DefaultShaderDir() {
  return (rex::filesystem::GetExecutableFolder() / ".." / ".." / ".." / ".." / "artifacts" /
          "shaders" / "dxil")
      .lexically_normal();
}
}  // namespace

namespace {

D3D12_COMPARISON_FUNC MapCompare(xenos::CompareFunction f) {
  switch (uint32_t(f)) {
    case 0: return D3D12_COMPARISON_FUNC_NEVER;
    case 1: return D3D12_COMPARISON_FUNC_LESS;
    case 2: return D3D12_COMPARISON_FUNC_EQUAL;
    case 3: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case 4: return D3D12_COMPARISON_FUNC_GREATER;
    case 5: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case 6: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    default: return D3D12_COMPARISON_FUNC_ALWAYS;
  }
}
D3D12_STENCIL_OP MapStencilOp(xenos::StencilOp op) {
  switch (uint32_t(op)) {
    case 0: return D3D12_STENCIL_OP_KEEP;
    case 1: return D3D12_STENCIL_OP_ZERO;
    case 2: return D3D12_STENCIL_OP_REPLACE;
    case 3: return D3D12_STENCIL_OP_INCR_SAT;
    case 4: return D3D12_STENCIL_OP_DECR_SAT;
    case 5: return D3D12_STENCIL_OP_INVERT;
    case 6: return D3D12_STENCIL_OP_INCR;
    default: return D3D12_STENCIL_OP_DECR;
  }
}

// Xenos ColorRenderTargetFormat -> host render target format.
DXGI_FORMAT MapColorRtFormat(uint32_t f) {
  switch (f) {
    case 0:   // 8_8_8_8
    case 1:   // 8_8_8_8_GAMMA
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case 2:   // 2_10_10_10
    case 10:  // 2_10_10_10_AS_10_10_10_10
      return DXGI_FORMAT_R10G10B10A2_UNORM;
    case 3:   // 2_10_10_10_FLOAT
    case 12:  // 2_10_10_10_FLOAT_AS_16_16_16_16
    case 7:   // 16_16_16_16_FLOAT
      return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case 4:   // 16_16
      return DXGI_FORMAT_R16G16_SNORM;
    case 5:   // 16_16_16_16 (Xenos fixed -32..32; approximated as float)
      return REXCVAR_GET(sr_native_fixed16_snorm) ? DXGI_FORMAT_R16G16B16A16_SNORM
                                               : DXGI_FORMAT_R16G16B16A16_FLOAT;
    case 6:   // 16_16_FLOAT
      return DXGI_FORMAT_R16G16_FLOAT;
    case 14:  // 32_FLOAT
      return DXGI_FORMAT_R32_FLOAT;
    case 15:  // 32_32_FLOAT
      return DXGI_FORMAT_R32G32_FLOAT;
    default:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
  }
}

inline uint64_t MakeKey(uint32_t a, uint32_t b) { return (uint64_t(a) << 32) | b; }

// Guest-pixel rectangles -> host pixels of a surface rendered at `scale`.
// Guest pixel coordinate -> host pixel coordinate of a surface rendered at
// `scale` (possibly fractional). Every edge goes through the same rounding, so
// adjacent regions (tiles, atlas cells) still meet exactly.
inline uint32_t HostPx(uint32_t v, float scale) {
  return uint32_t(std::lround(double(v) * double(scale)));
}
inline LONG HostPx(LONG v, float scale) { return LONG(std::lround(double(v) * double(scale))); }
inline D3D12_RECT ScaleRect(D3D12_RECT r, float scale) {
  return {HostPx(r.left, scale), HostPx(r.top, scale), HostPx(r.right, scale),
          HostPx(r.bottom, scale)};
}
inline std::vector<D3D12_RECT> ScaleRects(const std::vector<D3D12_RECT>& rs, float scale) {
  std::vector<D3D12_RECT> out;
  out.reserve(rs.size());
  for (const D3D12_RECT& r : rs) out.push_back(ScaleRect(r, scale));
  return out;
}

#include "shaders/blit_ps.h"
#include "shaders/blit_vs.h"
#include "shaders/fxaa_ps.h"
#include "shaders/ssao_apply_ms_ps.h"
#include "shaders/ssao_apply_ps.h"
#include "shaders/ssao_ms_ps.h"
#include "shaders/ssao_ps.h"
#include "shaders/depth_copy_ps.h"
#include "shaders/depth_copy_ms_ps.h"
#include "shaders/depth_copy_vs.h"
#include "shaders/edram_alias_ms_ps.h"
#include "shaders/edram_alias_ps.h"
#include "shaders/edram_alias_vs.h"

}  // namespace

bool Enabled() { return RendererActive(); }

// DC_LUT gamma ramp: from the native graphics system, or from the Xenos
// command processor in sr_native_ab_mode (the kit read it through a
// fork-only IGraphicsSystem::GetGammaRamp256).
bool GuestGammaRamp256(uint32_t* out) {
  if (INativeGraphicsSystem* system = ActiveNativeGraphicsSystem()) {
    return system->GetGammaRamp256(out);
  }
  return GetSrXenosGammaRamp256(out);
}

bool LogUnsupportedOnce(const char* category, uint64_t key, const char* detail) {
  static std::mutex mutex;
  static std::set<std::pair<std::string, uint64_t>> seen;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!seen.emplace(category, key).second) return false;
  }
  REXLOG_WARN("native: unsupported {} ({:X}){}{} - logged once; see docs/native-port-plan.md", category,
              key, detail ? ": " : "", detail ? detail : "");
  return true;
}

Renderer& Renderer::Get() {
  static Renderer renderer;
  return renderer;
}
Renderer::~Renderer() {ShutdownWorker();}
bool Renderer::InstallPacketSink(PacketSink sink,std::function<void()> cancel) {
  std::lock_guard<std::mutex> lock(front_mutex_);
  if(!sink || worker_stop_ || worker_mode_checked_ || packet_sink_) return false;
  packet_sink_=std::move(sink);cancel_packet_sink_=std::move(cancel);return true;
}
void Renderer::ShutdownWorker() {
  if(worker_stop_.exchange(true)) return;
  if(cancel_packet_sink_) cancel_packet_sink_();
  queue_cv_.notify_all();done_cv_.notify_all();
  {
    std::lock_guard<std::mutex> lock(front_mutex_);
    // OnSwap can now leave its wait and release this lock. Unsubmitted CPU
    // commands are discarded on shutdown; queued GPU work retires before UI.
    if(batch_) batch_->Clear();
  }
  if(worker_.joinable()) worker_.join();
  // The optional scene owns BLAS/TLAS and their source buffers. Retire all
  // queued work before releasing them, including shutdown outside a swap.
  if((rt_capture_scene_ || rt_shadow_scene_ || post_vertex_capture_ || motion_clips_ || temporal_motion_) && queue_ && fence_ && fence_event_ && device_ &&
     SUCCEEDED(device_->GetDeviceRemovedReason())) {
    const uint64_t value=next_fence_value_++;
    if(SUCCEEDED(queue_->Signal(fence_.Get(),value)) && fence_->GetCompletedValue()<value &&
       SUCCEEDED(fence_->SetEventOnCompletion(value,fence_event_)))
      WaitForSingleObject(fence_event_,INFINITE);
  }
  // No DLSS dispatch is recorded by the capability-only integration below.
  // A future scene Evaluate path must pass its submitted covering fence here.
  if(dlss_) { (void)dlss_->Shutdown(); dlss_.reset(); }
  for(auto& [key,entry]:textures_) entry.tracking.reset();
  resource_write_tracker_.Stop();
  packet_sink_={};cancel_packet_sink_={};
}

bool PackShaderLookup(uint64_t hash, bool vertex, const void** data, size_t* size);

bool Renderer::EnsureInitialized() {
  if (initialized_) {
    return true;
  }
  if (init_failed_) {
    return false;
  }
  init_failed_ = true;
  const auto cpu_options=SelectCpuBundle(REXCVAR_GET(sr_native_cpu_bundle), !packet_sink_,
      REXCVAR_GET(sr_native_pm4_bounded_reads), REXCVAR_GET(sr_native_shared_staging),
      REXCVAR_GET(sr_native_resolve_identity_cache),
      uint32_t(std::clamp(REXCVAR_GET(sr_native_buffer_srv_cache),0,8192)));
  complete_resolve_identity_=cpu_options.resolve_identity;
  stage_shared_constants_=cpu_options.shared_staging;
  const char* tracking_env=std::getenv("SR_D3D12_TEXTURE_WRITE_TRACKING");
  bool invalid_tracking_mode=false;
  texture_tracking_mode_=ResolveTextureTrackingMode(cpu_options.tracking_on,tracking_env?tracking_env:"",&invalid_tracking_mode);
  if(invalid_tracking_mode)
    REXLOG_WARN("native texture tracking: invalid mode; using off");
  REXLOG_INFO("native texture tracking: mode={}",texture_tracking_mode_==TextureTrackingMode::On?"on":texture_tracking_mode_==TextureTrackingMode::Compare?"compare":"off");
  const char* pm4_reads_profile=std::getenv("SR_NATIVE_PM4_READ_PROFILE");
  mirror_.bounded_alu_reads = cpu_options.bounded_reads;
  capture_mirror_.bounded_alu_reads = REXCVAR_GET(sr_native_pm4_bounded_reads);
  mirror_.alu_read_profile.enabled = !packet_sink_ && CpuProfiling() &&
      pm4_reads_profile && *pm4_reads_profile && *pm4_reads_profile!='0';
  REXLOG_INFO("native PM4 live read profiling: {}",mirror_.alu_read_profile.enabled?"on":"off");
  REXLOG_INFO("native PM4 live read mode: {}",mirror_.bounded_alu_reads?"bounded copy":"query validation");
  render_scale_ = render_scale::NativeScale(REXCVAR_GET(sr_native_render_scale),
                                           REXCVAR_GET(sr_render_scale));
  temporal_probe_ = !packet_sink_ && REXCVAR_GET(sr_native_temporal_probe);
  if (temporal_probe_) REXLOG_INFO("native temporal probe: enabled (diagnostics only)");
  camera_candidate_probe_enabled_ = !packet_sink_ && REXCVAR_GET(sr_native_camera_candidate_probe);
  if (camera_candidate_probe_enabled_) {
    REXLOG_INFO("native camera candidate probe: enabled (VS {:016X} c0; diagnostics only)",
                graphics::CameraCandidateProbe::kViewProjectionShader);
  }
  stacked_resolves_ = !packet_sink_ && REXCVAR_GET(sr_native_stacked_resolves);
  const auto srv_capacity=cpu_options.srv_capacity;
  if(!packet_sink_ && srv_capacity) buffer_srv_cache_.emplace(size_t(srv_capacity));
  if(packet_sink_ && REXCVAR_GET(sr_native_buffer_srv_cache))
    REXLOG_WARN("native: sr_native_buffer_srv_cache is D3D12-only; ignored for Vulkan capture");
  REXLOG_INFO("native buffer SRV cache: capacity={}",buffer_srv_cache_?srv_capacity:0);
  REXLOG_INFO("native CPU bundle: requested={} selected={} tracking={} bounded_reads={} shared_staging={} resolve_identity={} buffer_srvs={}",
      REXCVAR_GET(sr_native_cpu_bundle),cpu_options.tracking_on,cpu_options.tracking_on,
      mirror_.bounded_alu_reads,stage_shared_constants_,complete_resolve_identity_,srv_capacity);
  if(packet_sink_ && REXCVAR_GET(sr_native_stacked_resolves))
    REXLOG_WARN("native: sr_native_stacked_resolves is D3D12-only; ignored for Vulkan capture");
  shadow_scale_ = float(std::clamp(REXCVAR_GET(sr_native_shadow_quality), 1, 4));
  shadow_mul_ = float(std::clamp(REXCVAR_GET(sr_native_shadow_scale_mul), 0.25, 1.0));
  {
    int32_t m = REXCVAR_GET(sr_native_msaa_samples);
    msaa_samples_ = m >= 8 ? 8u : m >= 4 ? 4u : m >= 2 ? 2u : m == 1 ? 1u : 0u;
  }
  anisotropy_ = REXCVAR_GET(sr_native_anisotropic_filtering);
  full_scene_resolution_ = REXCVAR_GET(sr_native_full_scene_resolution);
  foliage_aa_ = REXCVAR_GET(sr_native_foliage_antialiasing);
  fxaa_ = REXCVAR_GET(sr_native_fxaa);
  smooth_effects_ = REXCVAR_GET(sr_native_smooth_effects);
  shadow_smoothing_ = REXCVAR_GET(sr_native_shadow_smoothing);
  soft_particles_ = REXCVAR_GET(sr_native_soft_particles);
  ssao_ = REXCVAR_GET(sr_native_ambient_occlusion);
  bloom_scale_ = float(std::clamp(REXCVAR_GET(sr_native_bloom_quality), 1, 4));
  // Options keyed on game passes (Conan's pass table). SR's roles are -1 in
  // game_profile.h until its passes are known: keep them off.
  auto require_pass = [](bool& option, int pass, const char* name) {
    if (option && pass < 0) {
      option = false;
      REXLOG_WARN("native: {} needs a render pass role missing from game_profile.h; disabled", name);
    }
  };
  require_pass(fxaa_, kPassRenderHud, "sr_native_fxaa");
  require_pass(ssao_, kPassRenderOpaque, "sr_native_ambient_occlusion");
  require_pass(soft_particles_, kPassRenderSorted, "sr_native_soft_particles");
  if (shadow_scale_ != 1.0f && kPassRenderShadowMaps < 0) {
    shadow_scale_ = 1.0f;
    REXLOG_WARN("native: sr_native_shadow_quality needs the shadow pass role; disabled");
  }
  output_width_ = render_scale::HostDimension(kOutputWidth, render_scale_);
  output_height_ = render_scale::HostDimension(kOutputHeight, render_scale_);
  REXLOG_INFO("native: render scale {} ({}x{}), shadow scale {}, msaa {}, anisotropy {}",
              render_scale_, output_width_, output_height_, shadow_scale_, msaa_samples_,
              anisotropy_);
  auto* kernel_state = REX_KERNEL_STATE();
  auto* graphics = kernel_state && kernel_state->emulator()
                       ? kernel_state->emulator()->graphics_system()
                       : nullptr;
  if (!graphics || !graphics->presenter()) {
    REXLOG_ERROR("native: no graphics system / presenter");
    return false;
  }
  presenter_ = static_cast<rex::ui::d3d12::D3D12Presenter*>(graphics->presenter());
  graphics_ = graphics;
  // The kit's SDK fork calls back from the Xenos CP after each swap (A/B
  // dumps). The stock SDK has no such hook; the project's own command
  // processor (sr_graphics_system.cpp, sr_native_ab_mode) does it instead.
  SetSrXenosSwapObserver([](uint64_t swap, const uint32_t* regs, uint32_t count) {
    Renderer::Get().OnGpuSwap(swap, regs, count);
  });
  provider_ = &presenter_->provider();
  device_ = provider_->GetDevice();
  graphics::d3d12::LogDeviceCapabilities(device_);
  queue_ = provider_->GetDirectQueue();
  if (REXCVAR_GET(sr_native_own_queue)) {
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (SUCCEEDED(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&own_queue_)))) {
      queue_ = own_queue_.Get();
    }
  }

  // The kit also had a mode where the Xenos plugin only synchronized
  // (gpu_null_draws, a fork-only cvar). Here there are two modes: the native
  // graphics system without Xenos (it reports occlusion queries as visible),
  // or sr_native_ab_mode with full Xenos rendering and presentation next to
  // the native renderer (same guest frame through both paths, A/B diffs).

  for (auto& allocator : allocators_) {
    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(&allocator)))) {
      REXLOG_ERROR("native: CreateCommandAllocator failed");
      return false;
    }
  }
  if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0].Get(),
                                        nullptr, IID_PPV_ARGS(&command_list_)))) {
    REXLOG_ERROR("native: CreateCommandList failed");
    return false;
  }
  command_list_->Close();
  if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
    return false;
  }
  fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);

  if (!CreateDrawResources()) {
    return false;
  }

  // With --d3d12_debug, route debug-layer messages into the log.
  ComPtr<ID3D12InfoQueue1> info_queue;
  if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
    DWORD cookie = 0;
    info_queue->RegisterMessageCallback(
        [](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id,
           LPCSTR description, void*) {
          static std::atomic<int> logged{0};
          if (severity <= D3D12_MESSAGE_SEVERITY_WARNING && logged.fetch_add(1) < 200) {
            REXLOG_ERROR("native: D3D12 [{}] id {}: {}", int(severity), int(id), description);
          }
        },
        D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie);
    REXLOG_INFO("native: D3D12 debug message callback registered");
  }

  // Device-removal watchdog: a GPU fault in native work would otherwise show
  // up only as a guest stall (Xenos CP blocked on a dead device).
  std::thread([device = device_]() {
    for (;;) {
      Sleep(1000);
      HRESULT reason = device->GetDeviceRemovedReason();
      if (reason != S_OK) {
        REXLOG_ERROR("native: D3D12 DEVICE REMOVED, reason {:08X}", uint32_t(reason));
        rex::FlushLogging();
        return;
      }
    }
  }).detach();

  {
    std::string where;
    ShaderCorpusAvailable(&where);
    REXLOG_INFO("native: renderer initialized, shaders from {}", where);
  }
  init_failed_ = false;
  graphics_options_epoch_=graphics::SharedGraphicsOptions()->BeginBackend(graphics::NativeApi::kD3D12);
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 ray_options{};
  ray_tracing_capable_=SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
      &ray_options,sizeof(ray_options))) && ray_options.RaytracingTier>=D3D12_RAYTRACING_TIER_1_1;
  D3D12_FEATURE_DATA_SHADER_MODEL ray_shader_model{D3D_SHADER_MODEL_6_5};
  ComPtr<ID3D12Device5> ray_device;
  ray_tracing_capable_=ray_tracing_capable_ && SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&ray_device))) &&
      SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&ray_shader_model,sizeof(ray_shader_model))) &&
      ray_shader_model.HighestShaderModel>=D3D_SHADER_MODEL_6_5;
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 wave_options{};
  rt_shadow_wave_capable_=SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1,
      &wave_options,sizeof(wave_options))) && wave_options.WaveOps;
#if SR_HAS_DLSS
  dlss_=std::make_unique<graphics::d3d12::DlssSuperResolution>();
  wchar_t executable[32768]{};
  const DWORD path_length=GetModuleFileNameW(nullptr,executable,DWORD(std::size(executable)));
  if(path_length && path_length<std::size(executable)) {
    const auto path=std::filesystem::path(executable).parent_path();
    const auto result=dlss_->Initialize(device_,path.c_str());
    REXLOG_INFO("native DLSS SDK initialization: result={} ngx={:08X} support={} (not applied)",
                uint32_t(result.error),result.ngxResult,uint32_t(dlss_->GetStatus().support));
    if(result) {
      graphics::d3d12::DlssOptimalSettings settings{};
      const auto optimal=dlss_->QueryOptimalSettings(output_width_,output_height_,settings);
      REXLOG_INFO("native DLSS Quality query: result={} ngx={:08X} output={}x{} render={}x{} (scene inputs pending; applied Off)",
                  uint32_t(optimal.error),optimal.ngxResult,settings.outputWidth,settings.outputHeight,
                  settings.renderWidth,settings.renderHeight);
    }
  }
#endif
  initialized_ = true;
  return true;
}

bool Renderer::CreateDrawResources() {
  // --- Main root signature: 3 root CBVs + bindless SRV/sampler tables. ---
  D3D12_DESCRIPTOR_RANGE srv_ranges[4] = {};
  for (uint32_t i = 0; i < 3; ++i) {
    srv_ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_ranges[i].NumDescriptors = kSrvHeapSize;
    srv_ranges[i].RegisterSpace = i;
  }
  D3D12_DESCRIPTOR_RANGE sampler_range{};
  sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  sampler_range.NumDescriptors = kSamplerHeapSize;
  sampler_range.RegisterSpace = 3;
  D3D12_ROOT_PARAMETER params[13] = {};
  for (uint32_t i = 0; i < 3; ++i) {
    params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[i].Descriptor.ShaderRegister = i;
    params[i].Descriptor.RegisterSpace = 4;
    params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[3 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3 + i].DescriptorTable.NumDescriptorRanges = 1;
    params[3 + i].DescriptorTable.pDescriptorRanges = &srv_ranges[i];
    params[3 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[6].DescriptorTable.NumDescriptorRanges = 1;
  params[6].DescriptorTable.pDescriptorRanges = &sampler_range;
  params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC rs_desc{};
  srv_ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srv_ranges[3].NumDescriptors = kSrvHeapSize;
  srv_ranges[3].RegisterSpace = 5;
  params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[7].DescriptorTable.NumDescriptorRanges = 1;
  params[7].DescriptorTable.pDescriptorRanges = &srv_ranges[3];
  params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  params[8].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[8].Descriptor={0,6};params[8].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  params[9].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[9].Descriptor={3,4};params[9].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  params[10].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
  params[10].Descriptor={0,6};params[10].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  params[11].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[11].Constants={4,4,1}; // b4,space4: original-PS pixel owner.
  params[11].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  rs_desc.NumParameters = 12;
  // Debug (sr_native_debug_rs_salt): an unused root-constant parameter makes every
  // pipeline new to the driver's shader cache - a cold first run on demand.
  if (int32_t salt = REXCVAR_GET(sr_native_debug_rs_salt); salt > 0) {
    params[12].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[12].Constants.ShaderRegister = 15;
    params[12].Constants.RegisterSpace = 7;
    params[12].Constants.Num32BitValues = uint32_t(std::min(salt, 32));
    params[12].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_desc.NumParameters = 13;
  }
  rs_desc.pParameters = params;
  rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                  D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
  ComPtr<ID3DBlob> blob, error;
  if (FAILED(D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
      FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                          IID_PPV_ARGS(&root_signature_)))) {
    REXLOG_ERROR("native: root signature creation failed: {}",
                 error ? static_cast<const char*>(error->GetBufferPointer()) : "?");
    return false;
  }

  // --- Blit root signature: SRV table t0 + static linear sampler. ---
  D3D12_DESCRIPTOR_RANGE blit_range{};
  blit_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  blit_range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER blit_param{};
  blit_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  blit_param.DescriptorTable.NumDescriptorRanges = 1;
  blit_param.DescriptorTable.pDescriptorRanges = &blit_range;
  blit_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC blit_sampler{};
  blit_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  blit_sampler.AddressU = blit_sampler.AddressV = blit_sampler.AddressW =
      D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  blit_sampler.MaxLOD = D3D12_FLOAT32_MAX;
  blit_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_PARAMETER blit_params[2] = {blit_param, {}};
  blit_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  blit_params[1].Descriptor.ShaderRegister = 0;
  blit_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC blit_rs{};
  blit_rs.NumParameters = 2;
  blit_rs.pParameters = blit_params;
  blit_rs.NumStaticSamplers = 1;
  blit_rs.pStaticSamplers = &blit_sampler;
  blob.Reset();
  error.Reset();
  if (FAILED(D3D12SerializeRootSignature(&blit_rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
      FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                          IID_PPV_ARGS(&blit_root_signature_)))) {
    return false;
  }
  D3D12_GRAPHICS_PIPELINE_STATE_DESC blit{};
  blit.pRootSignature = blit_root_signature_.Get();
  blit.VS = {kBlitVS, sizeof(kBlitVS)};
  blit.PS = {kBlitPS, sizeof(kBlitPS)};
  blit.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  blit.SampleMask = UINT_MAX;
  blit.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  blit.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  blit.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  blit.NumRenderTargets = 1;
  blit.RTVFormats[0] = rex::ui::d3d12::D3D12Presenter::kGuestOutputFormat;
  blit.SampleDesc.Count = 1;
  if (FAILED(device_->CreateGraphicsPipelineState(&blit, IID_PPV_ARGS(&blit_pipeline_)))) {
    REXLOG_ERROR("native: blit PSO creation failed");
    return false;
  }

  // --- Depth resolve copy: root constants (source offset) + SRV table t0. ---
  {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_DESCRIPTOR_RANGE range1 = range;
    range1.BaseShaderRegister = 1;
    D3D12_ROOT_PARAMETER dc_params[3] = {};
    dc_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    dc_params[0].Constants.Num32BitValues = 2;
    dc_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    dc_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    dc_params[1].DescriptorTable.NumDescriptorRanges = 1;
    dc_params[1].DescriptorTable.pDescriptorRanges = &range;
    dc_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    dc_params[2] = dc_params[1];
    dc_params[2].DescriptorTable.pDescriptorRanges = &range1;
    D3D12_ROOT_SIGNATURE_DESC dc_rs{};
    dc_rs.NumParameters = 3;
    dc_rs.pParameters = dc_params;
    blob.Reset();
    error.Reset();
    if (FAILED(D3D12SerializeRootSignature(&dc_rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&depth_copy_root_signature_)))) {
      return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC dc{};
    dc.pRootSignature = depth_copy_root_signature_.Get();
    dc.VS = {kDepthCopyVS, sizeof(kDepthCopyVS)};
    dc.PS = {kDepthCopyPS, sizeof(kDepthCopyPS)};
    dc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    dc.SampleMask = UINT_MAX;
    dc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    dc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    dc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    dc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    dc.NumRenderTargets = 2;
    dc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
    dc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
    dc.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&dc, IID_PPV_ARGS(&depth_copy_pipeline_)))) {
      REXLOG_ERROR("native: depth copy PSO creation failed");
      return false;
    }
    dc.PS = {kDepthCopyMsPS, sizeof(kDepthCopyMsPS)};
    if (FAILED(device_->CreateGraphicsPipelineState(&dc,
                                                    IID_PPV_ARGS(&depth_copy_ms_pipeline_)))) {
      REXLOG_ERROR("native: depth copy (MSAA) PSO creation failed");
      return false;
    }
  }

  {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 2;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 2;
    rs.pParameters = params;
    blob.Reset();
    error.Reset();
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&alias_root_signature_)))) {
      return false;
    }
  }

  // --- Descriptor heaps. ---
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = kSrvHeapSize;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&srv_heap_)))) {
    return false;
  }
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  heap_desc.NumDescriptors = kSamplerHeapSize;
  if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&sampler_heap_)))) {
    return false;
  }
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heap_desc.NumDescriptors = kRtvHeapSize;
  if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap_)))) {
    return false;
  }
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  heap_desc.NumDescriptors = kDsvHeapSize;
  if (FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&dsv_heap_)))) {
    return false;
  }
  // SRV slot 0: null 2D texture (unbound fetch slots sample zero).
  D3D12_SHADER_RESOURCE_VIEW_DESC null_srv{};
  null_srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  null_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  null_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  null_srv.Texture2D.MipLevels = 1;
  device_->CreateShaderResourceView(nullptr, &null_srv,
                                    srv_heap_->GetCPUDescriptorHandleForHeapStart());
  // Slots 1/2: null 3D and cube textures (the dimension must match the shader's heap view).
  null_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
  null_srv.Texture3D.MipLevels = 1;
  device_->CreateShaderResourceView(
      nullptr, &null_srv,
      provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(), 1));
  null_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
  null_srv.TextureCube.MipLevels = 1;
  device_->CreateShaderResourceView(
      nullptr, &null_srv,
      provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(), 2));

  // XDK sub_82107A30, missing-element branch 82107B00: swizzle 0x9250
  // supplies (0, 0, 0, 1). Host slot 16 is outside the 16 guest streams.
  {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = 16;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&default_vertex_buffer_))))
      return false;
    void* mapped = nullptr;
    D3D12_RANGE no_read{0, 0};
    if (FAILED(default_vertex_buffer_->Map(0, &no_read, &mapped))) return false;
    const float value[4] = {0, 0, 0, 1};
    std::memcpy(mapped, value, sizeof(value));
    default_vertex_buffer_->Unmap(0, nullptr);
  }
  // --- Output render target (presenter guest output format). ---
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = output_width_;
  desc.Height = output_height_;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = rex::ui::d3d12::D3D12Presenter::kGuestOutputFormat;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_CLEAR_VALUE clear{};
  clear.Format = desc.Format;
  for (uint32_t i = 0; i < kFramesInFlight; ++i) {
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                                IID_PPV_ARGS(&output_rts_[i])))) {
      return false;
    }
    output_rt_states_[i] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    output_rtvs_[i] = provider_->OffsetRTVDescriptor(
        rtv_heap_->GetCPUDescriptorHandleForHeapStart(), rtv_heap_next_++);
    device_->CreateRenderTargetView(output_rts_[i].Get(), nullptr, output_rtvs_[i]);
    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(&present_allocators_[i])))) {
      return false;
    }
  }
  if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        present_allocators_[0].Get(), nullptr,
                                        IID_PPV_ARGS(&present_list_))) ||
      FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&present_fence_)))) {
    return false;
  }
  present_list_->Close();

  // --- Per-frame upload rings. ---
  D3D12_HEAP_PROPERTIES upload_heap{};
  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC buf{};
  buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buf.Width = kUploadBytesPerFrame;
  buf.Height = 1;
  buf.DepthOrArraySize = 1;
  buf.MipLevels = 1;
  buf.SampleDesc.Count = 1;
  buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  for (uint32_t i = 0; i < kFramesInFlight; ++i) {
    if (FAILED(device_->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buf,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                IID_PPV_ARGS(&upload_buffers_[i])))) {
      return false;
    }
    D3D12_RANGE none{0, 0};
    upload_buffers_[i]->Map(0, &none, reinterpret_cast<void**>(&upload_cpu_[i]));
  }

  shader_dir_ = REXCVAR_GET(sr_native_shader_dir);
  if (shader_dir_.empty()) {
    shader_dir_ = DefaultShaderDir().string();
  }
  if (REXCVAR_GET(sr_native_debug_disable_driver_shader_cache)) {
    // Debug: simulate a first run (cold driver shader cache). Needs Windows
    // developer mode.
    ComPtr<ID3D12Device9> device9;
    HRESULT hr = device_->QueryInterface(IID_PPV_ARGS(&device9));
    if (SUCCEEDED(hr)) {
      hr = device9->ShaderCacheControl(D3D12_SHADER_CACHE_KIND_FLAG_IMPLICIT_DRIVER_MANAGED,
                                       D3D12_SHADER_CACHE_CONTROL_FLAG_DISABLE);
    }
    REXLOG_INFO("native: driver shader cache disabled: {:08X}", uint32_t(hr));
  }
  if (REXCVAR_GET(sr_native_pipeline_cache)) {
    // Sample count the renderer uses on the surfaces the game multisamples
    // (msaa_samples 0 = the game's own 4x), and whether foliage antialiasing
    // (alpha to coverage) can apply: the cache converts captured pipelines to
    // these settings (EXP-052).
    uint32_t msaa = msaa_samples_ ? msaa_samples_ : 4;
    pipeline_cache_.Start(device_, root_signature_.Get(), &PackShaderLookup, &UsageSemantic, msaa,
                          foliage_aa_ && msaa > 1);
  }
  return true;
}

void Renderer::Retire(ComPtr<ID3D12Resource> resource) {
  if (resource) {
    // Safe once the fence value of the frame currently being recorded completes.
    retired_.emplace_back(next_fence_value_, std::move(resource));
  }
}

uint32_t Renderer::AllocSrvIndex() {
  return AllocateDescriptor(retired_srvs_, srv_completed_fence_, srv_heap_next_, kSrvHeapSize,
                            [this] { return fence_->GetCompletedValue(); });
}

bool Renderer::Upload(size_t size, size_t alignment, UploadAlloc& out) {
  size_t offset = (upload_offset_ + alignment - 1) & ~(alignment - 1);
  if (offset + size <= kUploadBytesPerFrame) {
    upload_offset_ = offset + size;
    out.cpu = upload_cpu_[frame_index_] + offset;
    out.gpu = upload_buffers_[frame_index_]->GetGPUVirtualAddress() + offset;
    out.resource = upload_buffers_[frame_index_].Get();
    out.offset = offset;
    return true;
  }
  // Ring exhausted: continue in overflow pages, retired with this frame.
  offset = (overflow_offset_ + alignment - 1) & ~(alignment - 1);
  if (!overflow_page_ || offset + size > overflow_size_) {
    if (overflow_page_) Retire(overflow_page_);
    overflow_size_ = std::max<size_t>(size + alignment, 64u << 20);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = overflow_size_;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    overflow_page_.Reset();
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                IID_PPV_ARGS(&overflow_page_)))) {
      overflow_size_ = 0;
      return false;
    }
    D3D12_RANGE none{0, 0};
    overflow_page_->Map(0, &none, reinterpret_cast<void**>(&overflow_cpu_));
    ++overflow_pages_created_;
    offset = 0;
  }
  overflow_offset_ = offset + size;
  out.cpu = overflow_cpu_ + offset;
  out.gpu = overflow_page_->GetGPUVirtualAddress() + offset;
  out.resource = overflow_page_.Get();
  out.offset = offset;
  return true;
}

void Renderer::Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES& state,
                          D3D12_RESOURCE_STATES after) {
  if (state == after) {
    return;
  }
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = state;
  barrier.Transition.StateAfter = after;
  command_list_->ResourceBarrier(1, &barrier);
  state = after;
}

// ---------------------------------------------------------------------------
// Per-command GPU profile of one frame (sr_native_profile_frame_at_s).
// ---------------------------------------------------------------------------

bool Renderer::SkipGpuDraw() const {
  const int32_t format = REXCVAR_GET(sr_native_debug_nodraw_format);
  return format >= 0 && bound_rts_[0] && !bound_rts_[0]->depth &&
         int32_t(bound_rts_[0]->guest_format) == format;
}

void Renderer::ProfileBegin() {
  const double at = REXCVAR_GET(sr_native_profile_frame_at_s);
  if (at <= 0.0 || prof_done_ || prof_active_ || !frame_open_) return;
  if (compat::BenchElapsedMs() < at * 1000.0) return;
  if (!ts_frequency_) {
    if (FAILED(queue_->GetTimestampFrequency(&ts_frequency_))) {
      prof_done_ = true;
      return;
    }
  }
  if (!prof_heap_) {
    D3D12_QUERY_HEAP_DESC qd{};
    qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qd.Count = kProfMax;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = 8 * kProfMax;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateQueryHeap(&qd, IID_PPV_ARGS(&prof_heap_))) ||
        FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&prof_readback_)))) {
      prof_heap_.Reset();
      prof_done_ = true;
      return;
    }
  }
  prof_marks_.clear();
  prof_active_ = true;
  prof_frame_ = int64_t(frame_count_);
  command_list_->EndQuery(prof_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
  prof_marks_.push_back({255, false, 0, 0, std::string()});
  REXLOG_INFO("native profile: timing frame {}", frame_count_);
}

void Renderer::ProfileMark(Op op, bool drew) {
  if (!prof_active_ || prof_marks_.size() >= kProfMax - 1) return;
  const uint32_t index = uint32_t(prof_marks_.size());
  command_list_->EndQuery(prof_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
  const bool is_draw = op == Op::kDraw || op == Op::kDrawIndexed || op == Op::kDrawInline;
  std::string rt;
  if (is_draw && bound_rts_[0]) rt = SurfaceName(bound_rts_[0]);
  prof_marks_.push_back({uint8_t(op), is_draw && drew, is_draw ? current_vs_hash_ : 0,
                         is_draw ? current_ps_hash_ : 0, std::move(rt)});
}

void Renderer::ProfileEnd() {
  if (!prof_active_) return;
  const uint32_t n = uint32_t(prof_marks_.size());
  command_list_->EndQuery(prof_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, n);
  command_list_->ResolveQueryData(prof_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, n + 1,
                                  prof_readback_.Get(), 0);
  prof_marks_.push_back({254, false, 0, 0, std::string()});
}

void Renderer::ProfileReport() {
  if (!prof_active_) return;
  prof_active_ = false;
  prof_done_ = true;
  const uint64_t wait_value = frame_fence_values_[frame_index_];
  fence_->SetEventOnCompletion(wait_value, fence_event_);
  WaitForSingleObject(fence_event_, 5000);
  const uint32_t n = uint32_t(prof_marks_.size());
  const uint64_t* t = nullptr;
  D3D12_RANGE range{0, size_t(8) * n};
  if (n < 3 || FAILED(prof_readback_->Map(0, &range, reinterpret_cast<void**>(const_cast<uint64_t**>(&t))))) {
    return;
  }
  const double to_ms = 1000.0 / double(ts_frequency_);
  REXLOG_INFO("native profile: {} timestamps, frequency {} raw t[0..3] {} {} {} {} last {}", n, ts_frequency_, t[0],
              t[1], t[2], t[3], t[n - 1]);
  struct Acc {
    double ms = 0.0;
    uint32_t count = 0;
    uint32_t drawn = 0;
  };
  std::map<std::pair<uint64_t, uint64_t>, Acc> by_pair;
  std::map<std::string, Acc> by_rt, by_op;
  static const char* names[] = {"draw", "drawidx", "drawinline", "resolve", "begintiling",
                                "endtiling", "swap", "passend", "ring", "clear"};
  double total = 0.0;
  for (uint32_t i = 1; i + 1 < n; ++i) {
    const double dt = t[i] >= t[i - 1] ? double(t[i] - t[i - 1]) * to_ms : 0.0;
    const ProfMark& m = prof_marks_[i];
    total += dt;
    const bool is_draw = m.op <= 2;
    if (is_draw) {
      Acc& a = by_pair[{m.vs, m.ps}];
      a.ms += dt;
      ++a.count;
      a.drawn += m.drew ? 1 : 0;
      Acc& r = by_rt[m.rt.empty() ? "-" : m.rt];
      r.ms += dt;
      ++r.count;
    }
    Acc& o = by_op[names[m.op % 10]];
    o.ms += dt;
    ++o.count;
  }
  const double frame_ms = t[n - 1] >= t[0] ? double(t[n - 1] - t[0]) * to_ms : 0.0;
  REXLOG_INFO("native profile: frame {} GPU {:.2f} ms over {} commands (sum of command deltas {:.2f} ms)",
              prof_frame_, frame_ms, n - 2, total);
  for (const auto& [name, a] : by_op) {
    REXLOG_INFO("native profile: op {:12} {:4} cmds {:7.3f} ms ({:4.1f}%)", name, a.count, a.ms,
                100.0 * a.ms / std::max(total, 1e-9));
  }
  for (const auto& [name, a] : by_rt) {
    REXLOG_INFO("native profile: rt {:24} {:4} draws {:7.3f} ms ({:4.1f}%)", name, a.count, a.ms,
                100.0 * a.ms / std::max(total, 1e-9));
  }
  std::vector<std::pair<std::pair<uint64_t, uint64_t>, Acc>> pairs(by_pair.begin(), by_pair.end());
  std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
  for (size_t i = 0; i < pairs.size() && i < 30; ++i) {
    const auto& [key, a] = pairs[i];
    REXLOG_INFO("native profile: vs {:016X} ps {:016X} {:4} draws ({:4} drawn) {:7.3f} ms ({:4.1f}%) avg {:6.1f} us",
                key.first, key.second, a.count, a.drawn, a.ms, 100.0 * a.ms / std::max(total, 1e-9),
                a.ms * 1000.0 / std::max<uint32_t>(a.count, 1));
  }
  prof_readback_->Unmap(0, nullptr);
}

void Renderer::BeginFrameTimestamp() {
  // Query resolves copy GPU timestamps into CPU-visible memory. Avoid that
  // work during ordinary gameplay; diagnostics can opt back in at runtime.
  if (!REXCVAR_GET(sr_native_gpu_timestamps) &&
      !REXCVAR_GET(sr_native_gpu_pass_timing) && !CpuProfiling()) {
    ts_open_ = false;
    return;
  }
  if (!ts_heap_) {
    D3D12_QUERY_HEAP_DESC qd{};
    qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qd.Count = kTsPerFrame * kFramesInFlight;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = 8 * kTsPerFrame * kFramesInFlight;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(queue_->GetTimestampFrequency(&ts_frequency_)) ||
        FAILED(device_->CreateQueryHeap(&qd, IID_PPV_ARGS(&ts_heap_))) ||
        FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&ts_readback_)))) {
      ts_heap_.Reset();
      return;
    }
    ts_readback_->Map(0, nullptr, reinterpret_cast<void**>(const_cast<uint64_t**>(&ts_cpu_)));
    ts_last_log_ = std::chrono::steady_clock::now();
  }
  // This frame slot's previous timestamps are complete (BeginFrame waited on
  // its fence).
  if (ts_pending_[frame_index_] && ts_cpu_ && ts_frequency_) {
    const uint64_t* t = ts_cpu_ + kTsPerFrame * frame_index_;
    uint32_t n = ts_count_[frame_index_];
    if (n >= 2 && t[n - 1] > t[0]) {
      ts_accum_ms_ += double(t[n - 1] - t[0]) * 1000.0 / double(ts_frequency_);
      graphics::FrameTimeline::Global().RecordBusy(
          graphics::TimelineStage::kGpu, ts_frame_[frame_index_],
          uint64_t(double(t[n - 1] - t[0]) * 1e9 / double(ts_frequency_)));
      {
        const graphics::GpuFrameTicks split =
            graphics::SplitGpuFrame(t[0], t[n - 1], ts_have_prev_, ts_prev_end_, ~0ull);
        ts_prev_end_ = t[n - 1];
        ts_have_prev_ = true;
        graphics::FrameTimeline::Global().RecordBusy(graphics::TimelineStage::kGpuReal, ts_frame_[frame_index_],
            uint64_t(double(split.real) * 1e9 / double(ts_frequency_)));
        graphics::FrameTimeline::Global().RecordBusy(graphics::TimelineStage::kGpuIdle, ts_frame_[frame_index_],
            uint64_t(double(split.idle) * 1e9 / double(ts_frequency_)));
      }
      ++ts_frames_;
      for (uint32_t i = 0; i + 1 < n; ++i) {
        if (t[i + 1] >= t[i]) {
          ts_pass_ms_[ts_pass_[frame_index_][i] & 31] +=
              double(t[i + 1] - t[i]) * 1000.0 / double(ts_frequency_);
        }
      }
    }
    ts_pending_[frame_index_] = false;
  }
  auto now = std::chrono::steady_clock::now();
  if (ts_frames_ && now - ts_last_log_ > std::chrono::seconds(5)) {
    REXLOG_INFO("native: GPU time {:.3f} ms/frame ({} frames)", ts_accum_ms_ / double(ts_frames_),
                ts_frames_);
    if (REXCVAR_GET(sr_native_gpu_pass_timing)) {
      std::string split;
      for (int pass = 0; pass < 32; ++pass) {
        if (ts_pass_ms_[pass] / double(ts_frames_) >= 0.005) {
          split += fmt::format(" p{}:{:.3f}", pass, ts_pass_ms_[pass] / double(ts_frames_));
        }
      }
      REXLOG_INFO("native: GPU ms/frame per pass:{}", split);
    }
    std::fill(std::begin(ts_pass_ms_), std::end(ts_pass_ms_), 0.0);
    ts_accum_ms_ = 0.0;
    ts_frames_ = 0;
    ts_last_log_ = now;
  }
  ts_count_[frame_index_] = 0;
  ts_last_pass_ = -1;
  ts_open_ = true;
  PassTimestamp(g_current_pass);
}

void Renderer::PassTimestamp(int pass) {
  if (!ts_heap_ || !ts_open_ || pass == ts_last_pass_) return;
  uint32_t& n = ts_count_[frame_index_];
  if (n >= kTsPerFrame - 1) return;  // keep the last slot for the frame end
  ts_last_pass_ = pass;
  ts_pass_[frame_index_][n] = uint8_t(pass);
  command_list_->EndQuery(ts_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                          kTsPerFrame * frame_index_ + n);
  ++n;
}

void Renderer::EndFrameTimestamp() {
  if (!ts_heap_ || !ts_open_) return;
  ts_open_ = false;
  uint32_t& n = ts_count_[frame_index_];
  command_list_->EndQuery(ts_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                          kTsPerFrame * frame_index_ + n);
  ++n;
  command_list_->ResolveQueryData(ts_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                  kTsPerFrame * frame_index_, n, ts_readback_.Get(),
                                  8 * kTsPerFrame * frame_index_);
  ts_frame_[frame_index_] = swap_number_;
  ts_pending_[frame_index_] = true;
}

bool Renderer::BeginFrame() {
  if (frame_open_) {
    return true;
  }
  if (!EnsureInitialized()) {
    return false;
  }
  uint64_t wait_value = frame_fence_values_[frame_index_];
  auto wait_start = std::chrono::steady_clock::now();
  struct WaitTimer {
    std::chrono::steady_clock::time_point start;
    uint64_t* total;
    ~WaitTimer() {
      *total += uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count());
    }
  } wait_timer{wait_start, &stats_.gpu_wait_us};
  if (wait_value && fence_->GetCompletedValue() < wait_value) {
    graphics::TimelineBlockScope gpu_blocked;
    fence_->SetEventOnCompletion(wait_value, fence_event_);
    if (WaitForSingleObject(fence_event_, 2000) != WAIT_OBJECT_0) {
      HRESULT reason = device_->GetDeviceRemovedReason();
      REXLOG_ERROR(
          "native: GPU fence timeout (frame {}), device removed reason {:08X}, waiting for {} "
          "completed {} next {} upload_used {}",
          frame_count_, uint32_t(reason), wait_value, fence_->GetCompletedValue(),
          next_fence_value_, upload_offset_);
      ReportHang();
      rex::FlushLogging();
      WaitForSingleObject(fence_event_, INFINITE);
    }
  }
  if (present_fence_ && present_fence_values_[frame_index_] &&
      present_fence_->GetCompletedValue() < present_fence_values_[frame_index_]) {
    graphics::TimelineBlockScope present_blocked;
    present_fence_->SetEventOnCompletion(present_fence_values_[frame_index_], nullptr);
  }
  allocators_[frame_index_]->Reset();
  command_list_->Reset(allocators_[frame_index_].Get(), nullptr);
  BeginFrameTimestamp();
  upload_peak_ = std::max(upload_peak_, upload_offset_);
  upload_offset_ = 0;
  cb_vs_gpu_ = cb_ps_gpu_ = 0;
  shared_constants_cache_.Reset();
  if (overflow_page_) {
    Retire(overflow_page_);
    overflow_page_.Reset();
    overflow_offset_ = overflow_size_ = 0;
  }
  uint64_t completed = fence_->GetCompletedValue();
  if (completed != UINT64_MAX) srv_completed_fence_ = completed;
  retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                [completed](const auto& r) { return r.first <= completed; }),
                 retired_.end());
  frame_open_ = true;
  BeginRayShadows();
  BeginMotionCapture();
  post_vertex_projection_valid_=post_vertex_camera_conflict_=false;
  if(post_vertex_capture_) post_vertex_capture_->ReportCompleted(frame_index_);
  ReportCapturedRayProbe(frame_index_);
  const int32_t capture_swap=REXCVAR_GET(sr_native_rt_capture_swap);
  const auto& capture_trigger=REXCVAR_GET(sr_native_rt_capture_trigger_path);
  std::error_code capture_error;
  const bool trigger_ready=!post_vertex_done_ && !capture_trigger.empty() &&
      std::filesystem::exists(std::filesystem::u8path(capture_trigger),capture_error) && !capture_error;
  post_vertex_diagnostic_=!packet_sink_ && !post_vertex_done_ &&
      (trigger_ready || (capture_swap>0 && (swap_number_==uint64_t(capture_swap) ||
                                          swap_number_+1==uint64_t(capture_swap))));
  post_vertex_active_=post_vertex_diagnostic_ || (!packet_sink_ && rt_shadow_requested_);
  if(post_vertex_diagnostic_) rt_shadow_requested_=false;
  if(post_vertex_active_) {
    post_vertex_depth_snapshot_.Reset();
    post_vertex_color_snapshot_.Reset();
    post_vertex_caster_valid_=post_vertex_receiver_valid_=post_vertex_light_conflict_=false;
    if(!post_vertex_capture_) {
      post_vertex_capture_=std::make_unique<graphics::d3d12::PostVertexCapture>();
      if(!post_vertex_capture_->Initialize(device_)) {
        REXLOG_ERROR("native: post-VS capture resource allocation failed");
        post_vertex_capture_.reset();
        post_vertex_active_=false;
      }
    }
    if(post_vertex_active_) post_vertex_active_=post_vertex_capture_->Begin(command_list_.Get(),frame_index_);
  }
  if (temporal_probe_) temporal_.Begin(frame_count_ + 1, output_width_, output_height_);
  if (camera_candidate_probe_enabled_) camera_candidate_probe_.Begin(frame_count_ + 1);
  const int32_t probe_swap = REXCVAR_GET(sr_native_surface_probe_swap);
  surface_probe_ = !packet_sink_ && ((motion_capture_active_ && motion_capture_phase_==2) ||
                   (!surface_probe_done_ && probe_swap > 0 &&
                    (swap_number_ == uint64_t(probe_swap) ||
                     swap_number_ + 1 == uint64_t(probe_swap))));
  if (surface_probe_) {
    surface_sequence_.Reset();
    geometry_probe_.Reset(frame_count_ + 1, swap_number_);
  }
  frame_state_bound_ = false;
  fixed_state_cached_ = false;
  bound_vertex_views_mask_ = 0;
  bound_rt_count_ = 0;
  bound_ds_ = nullptr;
  std::memset(bound_rts_, 0, sizeof(bound_rts_));
  ProfileBegin();
  return true;
}

Renderer::HostSurface* Renderer::GetSurface(uint8_t* base, uint32_t surface_object, bool depth,
                                            uint32_t min_width, uint32_t min_height) {
  CpuTimer cpu_timer(cpu_timings.surfaces_ns);
  if (!surface_object) {
    return nullptr;
  }
  uint32_t surface_info = Load32(base, surface_object + 0x18);
  uint32_t color_depth_info = Load32(base, surface_object + 0x1C);
  uint32_t size_bits = Load32(base, surface_object + 0x24);
  const auto geometry = graphics::guest::DecodeSurfaceGeometry(
      surface_info, color_depth_info, size_bits, depth);
  uint32_t width = geometry.width;
  uint32_t height = geometry.height;
  if (tiling_active_) {
    width = std::max(width, tiling_width_);
    height = std::max(height, tiling_height_);
  }
  width = std::max(width, min_width);
  height = std::max(height, min_height);
  uint32_t format = geometry.format;
  if (!depth && format == 3 && REXCVAR_GET(sr_native_merge_7e3_surfaces)) format = 12;
  // Guest surfaces alias by EDRAM location: different surface objects with the
  // same EDRAM base (e.g. the depth pre-pass and the opaque pass depth) share
  // contents on Xenos, so the host RT is keyed by EDRAM base + format + size,
  // not by the surface object (EXP-013). Depth keeps no format distinction.
  uint32_t edram_base = geometry.edram_base;
  uint32_t guest_msaa = geometry.guest_msaa;
  uint32_t samples = REXCVAR_GET(sr_native_msaa) ? (guest_msaa == 2 ? 4u : guest_msaa == 1 ? 2u : 1u)
                                              : 1u;
  // msaa_samples setting: applies to the surfaces the game multisamples.
  if (samples > 1 && msaa_samples_) samples = msaa_samples_;
  uint32_t samples_log2 = samples >= 8 ? 3 : samples >= 4 ? 2 : samples >= 2 ? 1 : 0;
  uint64_t key = MakeKey((edram_base << 16) | (depth ? 0x8000u : format) | (samples_log2 << 12),
                         (width << 16) ^ height);
  auto it = surfaces_.find(key);
  if (it != surfaces_.end()) {
    return &it->second;
  }
  HostSurface& s = surfaces_[key];
  s.edram_base = edram_base;
  s.edram_tiles = geometry.edram_tiles;
  s.guest_format = depth ? 0xFFu : format;
  s.width = width;
  s.height = height;
  s.depth = depth;
  // Shadow map targets (square, drawn by "Render Shadow Maps") follow the
  // shadow quality setting, everything else the render resolution. The scene
  // depth is also first touched during that pass (clears), hence the shape
  // test. Reduction targets (the 1x576 / 1x1 luminance chain) stay 1:1: their
  // draws address single texels, and scaling them adds nothing visible.
  if (width < 16 || height < 16) {
    s.scale = 1.0f;
  } else if (g_current_pass == kPassRenderShadowMaps && width == height) {
    s.scale = shadow_scale_;
    s.shadow = true;
  } else if (kPassRenderShadowMaps < 0 && shadow_mul_ != 1.0f && width == height && width >= 512 &&
             (depth || format == 4)) {
    // The profile has no shadow pass role: the square 640x640 16_16 target and
    // its depth are the shadow map.
    s.scale = shadow_mul_;
    s.shadow = true;
  } else {
    s.scale = render_scale_;
    // Graphics options (EXP-045). The game renders its 3D scene at 1024x576 and
    // stretches it to the 1280x720 output (pass "Upscale"): full_scene_resolution
    // renders every surface except the output-sized ones 1280/1024 larger, so
    // the scene reaches the output 1:1. Small post-process targets (the
    // 256x144 bloom chain) get bloom_quality on top.
    bool output_sized = width == kOutputWidth && height == kOutputHeight;
    if (!output_sized && full_scene_resolution_) {
      s.scale *= float(kOutputWidth) / float(profile::kSceneWidth);
    }
    if (!output_sized && width <= 512 && height <= 512) s.scale *= bloom_scale_;
  }
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = render_scale::HostDimension(width, s.scale);
  desc.Height = render_scale::HostDimension(height, s.scale);
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = samples;
  s.samples = samples;
  D3D12_CLEAR_VALUE clear{};
  if (depth) {
    s.resource_format = DXGI_FORMAT_R24G8_TYPELESS;
    s.view_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    s.srv_format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    clear.Format = s.view_format;
    clear.DepthStencil.Depth = 1.0f;
    s.state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  } else {
    s.resource_format = MapColorRtFormat(format);
    s.view_format = s.resource_format;
    s.srv_format = s.resource_format;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    clear.Format = s.view_format;
    s.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
  }
  desc.Format = s.resource_format;
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, s.state, &clear,
                                              IID_PPV_ARGS(&s.resource)))) {
    REXLOG_ERROR("native: surface creation failed {}x{} fmt {}", width, height, format);
    surfaces_.erase(key);
    return nullptr;
  }
  if (depth) {
    s.view = provider_->OffsetDSVDescriptor(dsv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                            dsv_heap_next_++ % kDsvHeapSize);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = s.view_format;
    dsv.ViewDimension =
        samples > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(s.resource.Get(), &dsv, s.view);
    MaybeFinishMotionBeforeDepthWrite(&s,true,"surface initial clear");
    command_list_->ClearDepthStencilView(s.view, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                         1.0f, 0, 0, nullptr);
  } else {
    s.view = provider_->OffsetRTVDescriptor(rtv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                            rtv_heap_next_++ % kRtvHeapSize);
    device_->CreateRenderTargetView(s.resource.Get(), nullptr, s.view);
    float zero[4] = {};
    command_list_->ClearRenderTargetView(s.view, zero, 0, nullptr);
  }
  REXLOG_INFO("native: surface {:08X} {}x{} x{} {} fmt {} samples {}", surface_object, width,
              height, s.scale, depth ? "depth" : "color", format, samples);
  return &s;
}

std::string Renderer::SurfaceName(const HostSurface* s) const {
  if (!s) return "-";
  return fmt::format("{}@{}:{}x{}", s->guest_format == 0xFF ? "Z" : fmt::format("f{}", s->guest_format),
                     s->edram_base, s->width, s->height);
}

namespace {
// EDRAM 32bpp encoding kind of a guest color format (edram_alias.hlsl), -1 if
// reinterpretation is not modeled.
int EdramAliasKind(uint32_t guest_format) {
  switch (guest_format) {
    case 0:
    case 1:
      return 0;  // 8_8_8_8 (gamma variant stores the same bits)
    case 2:
    case 10:
      return 1;  // 2_10_10_10
    case 3:
    case 12:
      return 2;  // 2_10_10_10_FLOAT (7e3)
    default:
      return -1;
  }
}
}  // namespace

namespace {
// {pixel shader hash, PS constant register of g_mProjectionToWorld}
struct ProjectionReg {
  uint64_t hash;
  uint32_t reg;
};
constexpr ProjectionReg kProjectionRegs[] = {
    // Sentinel so the table is never empty (register 256 is rejected below).
    {0ull, 256},
#include "projection_regs.inc"
};

// Inverse of a 4x4 matrix (row-major, 16 floats); false if singular.
bool Invert4x4(const float m[16], float out[16]) {
  double a[4][8];
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      a[r][c] = m[r * 4 + c];
      a[r][c + 4] = r == c ? 1.0 : 0.0;
    }
  }
  for (int c = 0; c < 4; ++c) {
    int pivot = c;
    for (int r = c + 1; r < 4; ++r) {
      if (std::abs(a[r][c]) > std::abs(a[pivot][c])) pivot = r;
    }
    if (std::abs(a[pivot][c]) < 1e-12) return false;
    if (pivot != c) std::swap(a[pivot], a[c]);
    double inv = 1.0 / a[c][c];
    for (int k = 0; k < 8; ++k) a[c][k] *= inv;
    for (int r = 0; r < 4; ++r) {
      if (r == c) continue;
      double f = a[r][c];
      for (int k = 0; k < 8; ++k) a[r][k] -= f * a[c][k];
    }
  }
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) out[r * 4 + c] = float(a[r][c + 4]);
  }
  return true;
}
}  // namespace

void Renderer::CaptureCamera() {
  // The current pixel shader declares g_mProjectionToWorld (catalog
  // reflection): its 4 rows are this frame's inverse view-projection.
  const ProjectionReg* end = std::end(kProjectionRegs);
  const ProjectionReg* it = std::lower_bound(
      std::begin(kProjectionRegs), end, current_ps_hash_,
      [](const ProjectionReg& e, uint64_t h) { return e.hash < h; });
  if (it == end || it->hash != current_ps_hash_ || it->reg + 4 > 256) return;
  const float* c = reinterpret_cast<const float*>(mirror_.regs() + Pm4Mirror::kAluConstantBase +
                                                  1024 + it->reg * 4);
  for (int i = 0; i < 16; ++i) {
    if (!std::isfinite(c[i])) return;
  }
  std::memcpy(inv_view_proj_, c, sizeof(inv_view_proj_));
  camera_valid_ = true;
  if (temporal_probe_) temporal_.CaptureCamera(std::span<const float, 16>(inv_view_proj_, 16));
}

void Renderer::ApplySsao() {
  HostSurface& rt = *scene_rt_;
  HostSurface& ds = *scene_ds_;
  if (rt.depth || !ds.depth || rt.scale != ds.scale || rt.samples != ds.samples) return;
  float view_proj[16];
  if (!Invert4x4(inv_view_proj_, view_proj)) return;
  const uint32_t w = HostPx(rt.width, rt.scale), h = HostPx(rt.height, rt.scale);
  const bool ms = ds.samples > 1;
  if (!ssao_root_signature_) {
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    for (uint32_t i = 0; i < 2; ++i) {
      ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
      ranges[i].NumDescriptors = 1;
      ranges[i].BaseShaderRegister = i;
    }
    D3D12_ROOT_PARAMETER params[3] = {};
    for (uint32_t i = 0; i < 2; ++i) {
      params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      params[i].DescriptorTable.NumDescriptorRanges = 1;
      params[i].DescriptorTable.pDescriptorRanges = &ranges[i];
      params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.Num32BitValues = 40;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 3;
    desc.pParameters = params;
    ComPtr<ID3DBlob> blob, error;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&ssao_root_signature_)))) {
      ssao_ = false;
      return;
    }
  }
  auto pipeline = [&](bool apply) -> ID3D12PipelineState* {
    uint64_t key = (uint64_t(REXCVAR_GET(sr_native_ssao_debug)) << 41) | (uint64_t(apply) << 40) |
                   (uint64_t(ms) << 39) | (uint64_t(rt.samples) << 32) |
                   uint32_t(rt.view_format);
    auto& pso = ssao_pipelines_[key];
    if (!pso) {
      D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
      desc.pRootSignature = ssao_root_signature_.Get();
      desc.VS = {kBlitVS, sizeof(kBlitVS)};
      if (apply) {
        desc.PS = ms ? D3D12_SHADER_BYTECODE{kSsaoApplyMsPS, sizeof(kSsaoApplyMsPS)}
                     : D3D12_SHADER_BYTECODE{kSsaoApplyPS, sizeof(kSsaoApplyPS)};
        auto& b = desc.BlendState.RenderTarget[0];
        b.BlendEnable = !REXCVAR_GET(sr_native_ssao_debug);  // debug: show the AO term itself
        b.SrcBlend = D3D12_BLEND_ZERO;  // scene *= ao
        b.DestBlend = D3D12_BLEND_SRC_COLOR;
        b.BlendOp = D3D12_BLEND_OP_ADD;
        b.SrcBlendAlpha = D3D12_BLEND_ZERO;
        b.DestBlendAlpha = D3D12_BLEND_ONE;
        b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN |
                                  D3D12_COLOR_WRITE_ENABLE_BLUE;
        desc.RTVFormats[0] = rt.view_format;
        desc.SampleDesc.Count = rt.samples;
      } else {
        desc.PS = ms ? D3D12_SHADER_BYTECODE{kSsaoMsPS, sizeof(kSsaoMsPS)}
                     : D3D12_SHADER_BYTECODE{kSsaoPS, sizeof(kSsaoPS)};
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
      }
      desc.SampleMask = UINT_MAX;
      desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
      desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      desc.NumRenderTargets = 1;
      if (FAILED(device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)))) pso.Reset();
    }
    return pso.Get();
  };
  ID3D12PipelineState* ao_pso = pipeline(false);
  ID3D12PipelineState* apply_pso = pipeline(true);
  if (!ao_pso || !apply_pso) return;
  // AO target (R8, one value per pixel).
  if (!ssao_ao_ || ssao_ao_->GetDesc().Width != w || ssao_ao_->GetDesc().Height != h) {
    Retire(ssao_ao_);
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = w;
    desc.Height = h;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ssao_ao_state_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, ssao_ao_state_,
                                                nullptr, IID_PPV_ARGS(&ssao_ao_)))) {
      ssao_ao_.Reset();
      return;
    }
    if (!ssao_ao_rtv_.ptr) {
      ssao_ao_rtv_ = provider_->OffsetRTVDescriptor(rtv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                                    rtv_heap_next_++ % kRtvHeapSize);
    }
    device_->CreateRenderTargetView(ssao_ao_.Get(), nullptr, ssao_ao_rtv_);
    if (!ssao_ao_srv_) ssao_ao_srv_ = AllocSrvIndex();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(
        ssao_ao_.Get(), &srv,
        provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                        ssao_ao_srv_));
  }
  // Depth SRV (the same view the depth-resolve copies use).
  if (!ds.srv_index) {
    ds.srv_index = AllocSrvIndex();
    if (!ds.srv_index) return;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srv.ViewDimension = ms ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(
        ds.resource.Get(), &srv,
        provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                        ds.srv_index));
  }
  struct {
    float inv_view_proj[16];
    float view_proj[16];
    float size[2];
    float radius, intensity, fade, pad[3];
  } constants{};
  std::memcpy(constants.inv_view_proj, inv_view_proj_, sizeof(inv_view_proj_));
  std::memcpy(constants.view_proj, view_proj, sizeof(view_proj));
  constants.size[0] = float(w);
  constants.size[1] = float(h);
  constants.radius = float(REXCVAR_GET(sr_native_ssao_radius));
  constants.intensity = float(std::clamp(REXCVAR_GET(sr_native_ssao_intensity), 0.0, 1.0));
  constants.fade = float(REXCVAR_GET(sr_native_ssao_fade_distance));
  static_assert(sizeof(constants) == 40 * 4);

  auto gpu_srv = [&](uint32_t index) {
    return provider_->OffsetViewDescriptor(srv_heap_->GetGPUDescriptorHandleForHeapStart(), index);
  };
  Transition(ds.resource.Get(), ds.state,
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(ssao_ao_.Get(), ssao_ao_state_, D3D12_RESOURCE_STATE_RENDER_TARGET);
  ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), sampler_heap_.Get()};
  command_list_->SetDescriptorHeaps(2, heaps);
  command_list_->SetGraphicsRootSignature(ssao_root_signature_.Get());
  command_list_->SetGraphicsRootDescriptorTable(0, gpu_srv(ds.srv_index));
  command_list_->SetGraphicsRootDescriptorTable(1, gpu_srv(ssao_ao_srv_));
  command_list_->SetGraphicsRoot32BitConstants(2, 40, &constants, 0);
  D3D12_VIEWPORT vp{0.0f, 0.0f, float(w), float(h), 0.0f, 1.0f};
  D3D12_RECT sc{0, 0, LONG(w), LONG(h)};
  command_list_->RSSetViewports(1, &vp);
  command_list_->RSSetScissorRects(1, &sc);
  command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  // 1) AO term. The AO SRV (table 1) is bound but unused here: bind a null
  // view for the target being written.
  command_list_->SetGraphicsRootDescriptorTable(1, gpu_srv(0));
  command_list_->SetPipelineState(ao_pso);
  command_list_->OMSetRenderTargets(1, &ssao_ao_rtv_, FALSE, nullptr);
  command_list_->DrawInstanced(3, 1, 0, 0);
  // 2) Blur + multiply into the HDR scene.
  Transition(ssao_ao_.Get(), ssao_ao_state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  Transition(rt.resource.Get(), rt.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
  command_list_->SetGraphicsRootDescriptorTable(1, gpu_srv(ssao_ao_srv_));
  command_list_->SetPipelineState(apply_pso);
  command_list_->OMSetRenderTargets(1, &rt.view, FALSE, nullptr);
  command_list_->DrawInstanced(3, 1, 0, 0);
  Breadcrumb("ssao");
  frame_state_bound_ = false;
  fixed_state_cached_ = false;
  bound_vertex_views_mask_ = 0;
  bound_rt_count_ = 0;
  bound_ds_ = nullptr;
  std::memset(bound_rts_, 0, sizeof(bound_rts_));
}

void Renderer::ApplyFxaa(HostSurface& target) {
  if (!target.resource || target.depth) return;
  if (!fxaa_root_signature_) {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &range;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.Num32BitValues = 2;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 2;
    desc.pParameters = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    ComPtr<ID3DBlob> blob, error;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&fxaa_root_signature_)))) {
      fxaa_ = false;
      return;
    }
  }
  auto& pso = fxaa_pipelines_[uint32_t(target.view_format)];
  if (!pso) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = fxaa_root_signature_.Get();
    desc.VS = {kBlitVS, sizeof(kBlitVS)};
    desc.PS = {kFxaaPS, sizeof(kFxaaPS)};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = target.view_format;
    desc.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)))) {
      pso.Reset();
      return;
    }
  }
  // Source copy (the pass reads its own target).
  D3D12_RESOURCE_DESC rd = target.resource->GetDesc();
  if (!fxaa_temp_ || fxaa_temp_->GetDesc().Width != rd.Width ||
      fxaa_temp_->GetDesc().Height != rd.Height || fxaa_temp_->GetDesc().Format != rd.Format) {
    Retire(fxaa_temp_);
    D3D12_RESOURCE_DESC td = rd;
    td.Flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    fxaa_temp_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &td, fxaa_temp_state_,
                                                nullptr, IID_PPV_ARGS(&fxaa_temp_)))) {
      fxaa_temp_.Reset();
      return;
    }
    if (!fxaa_srv_) fxaa_srv_ = AllocSrvIndex();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = target.srv_format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(
        fxaa_temp_.Get(), &srv,
        provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(), fxaa_srv_));
  }
  Transition(target.resource.Get(), target.state, D3D12_RESOURCE_STATE_COPY_SOURCE);
  Transition(fxaa_temp_.Get(), fxaa_temp_state_, D3D12_RESOURCE_STATE_COPY_DEST);
  command_list_->CopyResource(fxaa_temp_.Get(), target.resource.Get());
  Transition(fxaa_temp_.Get(), fxaa_temp_state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  Transition(target.resource.Get(), target.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
  ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), sampler_heap_.Get()};
  command_list_->SetDescriptorHeaps(2, heaps);
  command_list_->SetGraphicsRootSignature(fxaa_root_signature_.Get());
  command_list_->SetGraphicsRootDescriptorTable(
      0, provider_->OffsetViewDescriptor(srv_heap_->GetGPUDescriptorHandleForHeapStart(), fxaa_srv_));
  float rcp[2] = {1.0f / float(rd.Width), 1.0f / float(rd.Height)};
  command_list_->SetGraphicsRoot32BitConstants(1, 2, rcp, 0);
  command_list_->SetPipelineState(pso.Get());
  command_list_->OMSetRenderTargets(1, &target.view, FALSE, nullptr);
  D3D12_VIEWPORT vp{0.0f, 0.0f, float(rd.Width), float(rd.Height), 0.0f, 1.0f};
  D3D12_RECT sc{0, 0, LONG(rd.Width), LONG(rd.Height)};
  command_list_->RSSetViewports(1, &vp);
  command_list_->RSSetScissorRects(1, &sc);
  command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list_->DrawInstanced(3, 1, 0, 0);
  // The guest state (root signature, heaps, targets) must be rebound.
  frame_state_bound_ = false;
  fixed_state_cached_ = false;
  bound_vertex_views_mask_ = 0;
  bound_rt_count_ = 0;
  bound_ds_ = nullptr;
  std::memset(bound_rts_, 0, sizeof(bound_rts_));
}

bool Renderer::ReinterpretSurface(HostSurface& dst, HostSurface& src) {
  int src_kind = EdramAliasKind(src.guest_format), dst_kind = EdramAliasKind(dst.guest_format);
  if (src_kind < 0 || dst_kind < 0 || src.width != dst.width || src.height != dst.height ||
      src.samples != dst.samples || src.scale != dst.scale) {
    return false;
  }
  auto& pso = alias_pipelines_[uint32_t(dst.view_format) | (dst.samples << 16)];
  if (!pso) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = alias_root_signature_.Get();
    desc.VS = {kEdramAliasVS, sizeof(kEdramAliasVS)};
    if (dst.samples > 1) {
      desc.PS = {kEdramAliasMsPS, sizeof(kEdramAliasMsPS)};
    } else {
      desc.PS = {kEdramAliasPS, sizeof(kEdramAliasPS)};
    }
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = dst.view_format;
    desc.SampleDesc.Count = dst.samples;
    if (FAILED(device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)))) {
      pso.Reset();
      return false;
    }
  }
  if (!src.srv_index) {
    src.srv_index = AllocSrvIndex();
    if (!src.srv_index) return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = src.srv_format;
    srv.ViewDimension =
        src.samples > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (src.samples <= 1) srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(
        src.resource.Get(), &srv,
        provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                        src.srv_index));
  }
  Transition(src.resource.Get(), src.state,
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(dst.resource.Get(), dst.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
  ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), sampler_heap_.Get()};
  command_list_->SetDescriptorHeaps(2, heaps);
  command_list_->SetGraphicsRootSignature(alias_root_signature_.Get());
  uint32_t kinds[2] = {uint32_t(src_kind), uint32_t(dst_kind)};
  command_list_->SetGraphicsRoot32BitConstants(0, 2, kinds, 0);
  command_list_->SetGraphicsRootDescriptorTable(
      1, provider_->OffsetViewDescriptor(srv_heap_->GetGPUDescriptorHandleForHeapStart(),
                                         src.srv_index));
  command_list_->SetPipelineState(pso.Get());
  command_list_->OMSetRenderTargets(1, &dst.view, FALSE, nullptr);
  D3D12_VIEWPORT vp{0.0f, 0.0f, float(HostPx(dst.width, dst.scale)),
                    float(HostPx(dst.height, dst.scale)), 0.0f, 1.0f};
  D3D12_RECT sc{0, 0, LONG(HostPx(dst.width, dst.scale)), LONG(HostPx(dst.height, dst.scale))};
  command_list_->RSSetViewports(1, &vp);
  command_list_->RSSetScissorRects(1, &sc);
  command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  if (!REXCVAR_GET(sr_native_debug_skip_reinterpret_draw)) command_list_->DrawInstanced(3, 1, 0, 0);
  ++stats_.edram_reinterprets;
  Breadcrumb("edram reinterpret");
  // Guest state must be rebound (root signature, heaps, targets).
  frame_state_bound_ = false;
  fixed_state_cached_ = false;
  bound_vertex_views_mask_ = 0;
  bound_rt_count_ = 0;
  bound_ds_ = nullptr;
  std::memset(bound_rts_, 0, sizeof(bound_rts_));
  return true;
}

void Renderer::ResolveEdramAliasing(HostSurface* s) {
  if (!s || !s->edram_tiles || !REXCVAR_GET(sr_native_edram_aliasing)) return;
  auto alias_surface=[](HostSurface& surface) {
    return graphics::guest::AliasSurface{uint64_t(reinterpret_cast<uintptr_t>(&surface)),surface.depth,
      {surface.width,surface.height,surface.edram_base,surface.guest_format,0,surface.edram_tiles},surface.last_write,surface.scale};
  };
  std::vector<graphics::guest::AliasSurface> aliases;aliases.reserve(surfaces_.size());
  for(auto& [key,other]:surfaces_) aliases.push_back(alias_surface(other));
  std::array<uint64_t,5> bound{};
  for(uint32_t i=0;i<5;++i) bound[i]=uint64_t(reinterpret_cast<uintptr_t>(bind_set_[i]));
  auto plan=graphics::guest::PlanEdramAlias(alias_surface(*s),aliases,bound);
  uint64_t newest=plan.newest_other;
  auto* newest_same=reinterpret_cast<HostSurface*>(uintptr_t(plan.source));
  if (trace_state_ == 1) {
    REXLOG_INFO("trace edram aliasing {}: newest same {} ({}) newest other {} own {}",
                SurfaceName(s), newest_same ? SurfaceName(newest_same) : "-",
                newest_same ? newest_same->last_write : 0, newest, s->last_write);
  }
  if (newest_same && newest_same->last_write > newest &&
      REXCVAR_GET(sr_native_edram_reinterpret)) {
    if (REXCVAR_GET(sr_native_hdr_from_ldr_black) && newest_same->guest_format == 0 &&
        s->guest_format == 12 && !s->depth) {
      float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
      Transition(s->resource.Get(), s->state, D3D12_RESOURCE_STATE_RENDER_TARGET);
      command_list_->ClearRenderTargetView(s->view, black, 0, nullptr);
      s->last_write = ++edram_write_seq_;
      return;
    }
    bool ok = ReinterpretSurface(*s, *newest_same);
    if (watch_frame_ >= 0 && !watch_done_) {
      REXLOG_INFO("native watch: edram reinterpret {} <- {} ok {} (guest fmt {} <- {}) at draw {}",
                  SurfaceName(s), SurfaceName(newest_same), ok, s->guest_format,
                  newest_same->guest_format, draw_calls_in_frame_);
    }
    if (trace_state_ == 1)
      REXLOG_INFO("trace edram reinterpret {} <- {}: {} (kinds {} {} fmt {} {})", SurfaceName(s),
                  SurfaceName(newest_same), ok, newest_same->guest_format, s->guest_format,
                  int(newest_same->srv_format), int(s->view_format));
    if (ok) {
      ++stats_.edram_alias_clears;
      s->last_write = ++edram_write_seq_;
      return;
    }
  }
  if (!newest) return;
  if (watch_frame_ >= 0 && !watch_done_) {
    REXLOG_INFO("native watch: edram alias clear {} at draw {}", SurfaceName(s), draw_calls_in_frame_);
  }
  if (s->depth) {
    MaybeFinishMotionBeforeDepthWrite(s,true,"EDRAM alias depth clear");
    Transition(s->resource.Get(), s->state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    command_list_->ClearDepthStencilView(s->view, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                         1.0f, 0, 0, nullptr);
  } else {
    // Undefined EDRAM contents: black. Alpha 1 by default - EDRAM left behind
    // by the previous frame's opaque images reads as alpha ~1 (e.g. the p9
    // decal buffer's alpha gates terrain specular; 0 made the beach white).
    float a = float(REXCVAR_GET(sr_native_edram_alias_alpha));
    float value[4] = {0.0f, 0.0f, 0.0f, a};
    Transition(s->resource.Get(), s->state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    command_list_->ClearRenderTargetView(s->view, value, 0, nullptr);
  }
  ++stats_.edram_alias_clears;
  Breadcrumb("edram alias clear");
  s->last_write = ++edram_write_seq_;
}

bool Renderer::BindRenderTargets(uint8_t* base, uint32_t dev, DXGI_FORMAT rtv_formats[4],
                                 uint32_t& rt_count, DXGI_FORMAT& dsv_format,
                                 const graphics::SurfaceDrawMetadata& draw_metadata) {
  CpuTimer cpu_timer(cpu_timings.render_targets_ns);
  HostSurface* rts[4] = {};
  rt_count = 0;
  // Only RTs the draw can write: stale MRT slots left bound by the guest (with
  // a zero write mask) would clip D3D12 rendering to the smallest bound target.
  uint32_t color_mask = LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_COLOR_MASK);
  for (uint32_t i = 0; i < 4; ++i) {
    uint32_t surface = Load32(base, dev + kDevRenderTargets + 4 * i);
    if (i && !((color_mask >> (4 * i)) & 0xF)) surface = 0;
    if (surface) {
      rts[i] = GetSurface(base, surface, false);
      if (rts[i]) rt_count = i + 1;
    }
  }
  // D3D12 renders only the intersection of all bound targets; Xenos does not
  // care when the guest pairs a larger RT with a smaller depth buffer (e.g.
  // the 1280x720 upscale target with the 1024x576 scene depth), so use a depth
  // surface covering RT0 in that case.
  HostSurface* ds = GetSurface(base, Load32(base, dev + kDevDepthStencil), true,
                               rts[0] ? rts[0]->width : 0, rts[0] ? rts[0]->height : 0);
  if (!rt_count && !ds) {
    return false;
  }
  bound_samples_ = rts[0] ? rts[0]->samples : ds ? ds->samples : 1;
  bound_scale_ = rts[0] ? rts[0]->scale : ds ? ds->scale : 1.0f;
  for (uint32_t i = 1; i < 4; ++i) {
    if (rts[i] && (rts[i]->samples != bound_samples_ || rts[i]->scale != bound_scale_)) {
      rts[i] = nullptr;
    }
  }
  if (ds && ds->scale != bound_scale_) {
    static int logged = 0;
    if (logged++ < 8) {
      REXLOG_WARN("native: depth surface scale ({}) differs from RT ({}), depth unbound",
                  ds->scale, bound_scale_);
    }
    ds = nullptr;
  }
  if (ds && ds->samples != bound_samples_) {
    static int logged = 0;
    if (logged++ < 8) {
      REXLOG_WARN("native: depth surface MSAA ({}) differs from RT ({}), depth unbound",
                  ds->samples, bound_samples_);
    }
    ds = nullptr;
  }
  if (surface_probe_) {
    graphics::SurfaceSignature sig{};
    if (rts[0]) {
      sig.color_edram = rts[0]->edram_base;
      sig.color_width = rts[0]->width;
      sig.color_height = rts[0]->height;
      sig.color_format = rts[0]->guest_format;
    }
    if (ds) {
      sig.depth_edram = ds->edram_base;
      sig.depth_width = ds->width;
      sig.depth_height = ds->height;
    }
    surface_sequence_.ObserveDraw(sig, draw_metadata);
  }
  // Experiment: EDRAM-aliased depth surfaces (e.g. the 256x144 bloom depth at
  // base 448, never cleared by the game) start from depth 0 each frame.
  if (ds && REXCVAR_GET(sr_native_exp_alias_depth_base) >= 0 &&
      ds->edram_base == uint32_t(REXCVAR_GET(sr_native_exp_alias_depth_base)) &&
      alias_clear_frame_ != frame_count_) {
    alias_clear_frame_ = frame_count_;
    MaybeFinishMotionBeforeDepthWrite(ds,true,"experimental alias depth clear");
    Transition(ds->resource.Get(), ds->state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    command_list_->ClearDepthStencilView(ds->view, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                         float(REXCVAR_GET(sr_native_exp_alias_depth_value)), 0, 0,
                                         nullptr);
  }
  // Aliasing is checked against surfaces outside this bind set; the set is
  // then marked as the latest EDRAM writer.
  bool same_bind = ds == bind_set_[4];
  for (uint32_t i = 0; i < 4 && same_bind; ++i) same_bind = rts[i] == bind_set_[i];
  if (g_current_pass == kPassUpscale && rts[0] && rts[0]->samples == 1) upscale_rt_ = rts[0];
  if (g_current_pass == kPassRenderOpaque && rts[0] && ds) {
    scene_rt_ = rts[0];
    scene_ds_ = ds;
    if (temporal_probe_) {
      temporal_.ObserveScene(reinterpret_cast<uintptr_t>(rts[0]->resource.Get()),
                             reinterpret_cast<uintptr_t>(ds->resource.Get()));
    }
  }
  // A larger depth attachment is legal. End the captured resource epoch on
  // its actual pair transition, independent of dimensions and OM cache resets.
  if(!same_bind && motion_capture_active_ && !motion_scene_finished_ && motion_captured_depth_ &&
     bind_set_[4] && bind_set_[4]->resource.Get()==motion_captured_depth_.Get() &&
     !graphics::MatchesRetainedMotionCapturePair(
        {reinterpret_cast<uintptr_t>(motion_captured_color_.Get()),reinterpret_cast<uintptr_t>(motion_captured_depth_.Get())},
        {rts[0]?reinterpret_cast<uintptr_t>(rts[0]->resource.Get()):0,ds?reinterpret_cast<uintptr_t>(ds->resource.Get()):0}))
    FinishMotionScene(bind_set_[4]);
  if (!same_bind) {
    if(post_vertex_diagnostic_ && post_vertex_active_ && !post_vertex_depth_snapshot_ && bind_set_[0] && bind_set_[4] &&
       bind_set_[0]->guest_format==12 && bind_set_[0]->width==bind_set_[4]->width &&
       bind_set_[0]->height==bind_set_[4]->height && post_vertex_capture_ &&
       !post_vertex_capture_->ranges().empty()) SnapshotPostVertexDepth();
    // Only a new bind set can observe other surfaces' EDRAM writes.
    bind_set_[0] = rts[0]; bind_set_[1] = rts[1]; bind_set_[2] = rts[2]; bind_set_[3] = rts[3];
    bind_set_[4] = ds;
    for (uint32_t i = 0; i < 4; ++i) ResolveEdramAliasing(rts[i]);
    ResolveEdramAliasing(ds);
    uint64_t seq = ++edram_write_seq_;
    for (uint32_t i = 0; i < 4; ++i) if (rts[i]) rts[i]->last_write = seq;
    if (ds) ds->last_write = seq;
  }
  if(rt_shadow_requested_ && !rt_shadow_attempted_ && rts[0] && ds &&
     rts[0]->guest_format==12 && post_vertex_capture_ && !post_vertex_capture_->ranges().empty())
    BuildRayShadowScene();
  rt_count = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    if (rts[i]) rt_count = i + 1;
  }
  for (uint32_t i = 0; i < 4; ++i) {
    rtv_formats[i] = rts[i] ? rts[i]->view_format : DXGI_FORMAT_UNKNOWN;
    if (rts[i]) Transition(rts[i]->resource.Get(), rts[i]->state, D3D12_RESOURCE_STATE_RENDER_TARGET);
  }
  ConfigureMotionCoverage(rts[0],ds,draw_metadata.vs_hash,draw_metadata.ps_hash,rt_count==1);
  if(motion_coverage_draw_active_) {rtv_formats[1]=DXGI_FORMAT_R32_UINT;rt_count=2;}
  dsv_format = ds ? ds->view_format : DXGI_FORMAT_UNKNOWN;
  if (ds) Transition(ds->resource.Get(), ds->state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  bool changed = rt_count != bound_rt_count_ || ds != bound_ds_ ||
      motion_coverage_bound_!=motion_coverage_draw_active_;
  for (uint32_t i = 0; i < 4 && !changed; ++i) changed = rts[i] != bound_rts_[i];
  if (changed) {
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[4] = {};
    for (uint32_t i = 0; i < rt_count; ++i) {
      // Gaps are not allowed with a contiguous RTV array; use the first bound
      // RT's view for gaps (write mask 0 in that case is the guest's intent).
      rtvs[i] = motion_coverage_draw_active_ && i==1 ? MotionCoverageView() :
          rts[i] ? rts[i]->view : rts[0] ? rts[0]->view : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }
    command_list_->OMSetRenderTargets(rt_count, rt_count ? rtvs : nullptr, FALSE,
                                      ds ? &ds->view : nullptr);
    if (trace_state_ == 1) {
      REXLOG_INFO("trace p{} after {} draws: bind RT [{} {} {} {}] DS {}", g_current_pass,
                  trace_draws_, SurfaceName(rts[0]), SurfaceName(rts[1]), SurfaceName(rts[2]),
                  SurfaceName(rts[3]), SurfaceName(ds));
      trace_draws_ = 0;
    }
    std::memcpy(bound_rts_, rts, sizeof(rts));
    bound_rt_count_ = rt_count;
    bound_ds_ = ds;
    motion_coverage_bound_=motion_coverage_draw_active_;
  }
  return true;
}


// Thread-safe lookup into the embedded shader pack (pipeline precompilation).
// Order everywhere: sr_native_shader_dir (explicit loose files), the
// pre-shader library (sr_native_preshaders), the embedded pack, the default
// loose-file directory.
bool PackShaderLookup(uint64_t hash, bool vertex, const void** data, size_t* size) {
  if (!REXCVAR_GET(sr_native_shader_dir).empty()) return false;
  if (const ShaderLibrary* lib = PreShaderLibrary()) {
    if (const PreShader* s = lib->Find(hash, vertex)) {
      *data = s->dxil.data();
      *size = s->dxil.size();
      return true;
    }
    // Browser-generated libraries cover only shaders found in DATA/*.AST.
    // A miss must still consult the embedded pack and the runtime cache.
  }
  const ShaderPackView& pack = EmbeddedShaderPack();
  ShaderPackEntry e;
  if (pack.count() && !REXCVAR_GET(sr_native_ignore_pack) && pack.Find(hash, vertex, e)) {
    *data = pack.base() + e.offset;
    *size = e.size;
    return true;
  }
  // Shaders translated on this machine in an earlier run.
  static std::mutex cached_mutex;
  static std::unordered_map<uint64_t, std::vector<uint8_t>> cached;
  std::lock_guard<std::mutex> lock(cached_mutex);
  auto& bytes = cached[hash ^ (vertex ? 0x9E3779B97F4A7C15ull : 0)];
  if (bytes.empty() && !ShaderTranslator::Get().LoadCached(hash, vertex, bytes)) return false;
  *data = bytes.data();
  *size = bytes.size();
  return true;
}

bool ShaderCorpusAvailable(std::string* where) {
  const ShaderPackView& pack = EmbeddedShaderPack();
  const std::string dir_cvar = REXCVAR_GET(sr_native_shader_dir);
  std::string lib_where;
  if (dir_cvar.empty()) {
    if (const ShaderLibrary* lib = PreShaderLibrary(&lib_where)) {
      if (where) *where = fmt::format("pre-shaders {}, {} shaders", lib_where, lib->size());
      return true;
    }
  }
  if (pack.count() && dir_cvar.empty() && !REXCVAR_GET(sr_native_ignore_pack)) {
    if (where) *where = fmt::format("embedded pack, {} shaders", pack.count());
    return true;
  }
  if (dir_cvar.empty() && ShaderTranslator::Get().Enabled()) {
    if (where) *where = "runtime shader translation (shader_tools/, cache in shader_cache/)";
    return true;
  }
  const std::filesystem::path dir = dir_cvar.empty() ? DefaultShaderDir() : dir_cvar;
  if (where) *where = "sr_native_shader_dir " + dir.string();
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.path().extension() == ".dxil") return true;
  }
  return false;
}

const std::vector<uint8_t>* Renderer::LoadShader(uint64_t hash, bool vertex) {
  shader_pending_ = false;
  auto it = shader_bytecode_.find(hash);
  if (it != shader_bytecode_.end()) {
    return it->second.get();
  }
  char name[64];
  std::snprintf(name, sizeof(name), "%016llX.%s.dxil", (unsigned long long)hash,
                vertex ? "vs" : "ps");
  std::unique_ptr<std::vector<uint8_t>> data;
  const ShaderLibrary* lib =
      REXCVAR_GET(sr_native_shader_dir).empty() ? PreShaderLibrary() : nullptr;
  if (lib) {
    if (const PreShader* s = lib->Find(hash, vertex)) {
      data = std::make_unique<std::vector<uint8_t>>(s->dxil);
      auto* result = data.get();
      shader_bytecode_[hash] = std::move(data);
      return result;
    }
    // A partial library is a cache, not a complete shader corpus. Let the
    // runtime translator supply missing shaders; pending results stay open.
  }
  // Embedded pack unless sr_native_shader_dir points at loose files.
  const ShaderPackView& pack = EmbeddedShaderPack();
  if (pack.count() && REXCVAR_GET(sr_native_shader_dir).empty() &&
      !REXCVAR_GET(sr_native_ignore_pack)) {
    ShaderPackEntry e;
    if (pack.Find(hash, vertex, e)) {
      data = std::make_unique<std::vector<uint8_t>>(pack.base() + e.offset,
                                                    pack.base() + e.offset + e.size);
    }
  } else {
    std::ifstream f(std::filesystem::path(shader_dir_) / name, std::ios::binary);
    if (f) {
      data = std::make_unique<std::vector<uint8_t>>((std::istreambuf_iterator<char>(f)),
                                                    std::istreambuf_iterator<char>());
    }
  }
  if (!data && REXCVAR_GET(sr_native_shader_dir).empty()) {
    // Not shipped with the build: translate it on this machine (or take it from
    // the cache of an earlier run). A translation still running leaves the
    // answer open: nothing is cached, the draw is skipped and asked again.
    std::vector<uint8_t> dxil;
    switch (ShaderTranslator::Get().Fetch(hash, vertex, dxil)) {
      case TranslateState::kReady:
        data = std::make_unique<std::vector<uint8_t>>(std::move(dxil));
        break;
      case TranslateState::kPending:
        shader_pending_ = true;
        ++stats_.shader_pending_draws;
        return nullptr;
      default:
        break;
    }
  }
  if (!data) {
    REXLOG_WARN("native: missing shader {}", name);
    LogUnsupportedOnce("shader missing from the corpus", hash, name);
  }
  auto* result = data.get();
  shader_bytecode_[hash] = std::move(data);
  return result;
}

ID3D12PipelineState* Renderer::GetPipeline(uint8_t* base, uint32_t dev, uint64_t vs_hash,
                                           uint64_t ps_hash, uint32_t decl,
                                           D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type,
                                           const DXGI_FORMAT rtv_formats[4], uint32_t rt_count,
                                           DXGI_FORMAT dsv_format,
                                           D3D12_INDEX_BUFFER_STRIP_CUT_VALUE strip_cut) {
  CpuTimer cpu_timer(cpu_timings.pipeline_ns);
  const bool ray_variant=rt_shadow_draw_active_ && !rt_shadow_shader_.empty();
  const bool coverage_variant=motion_coverage_draw_active_ && motion_coverage_shader_;
  const bool special_variant=ray_variant || coverage_variant;
  if(coverage_variant && REXCVAR_GET(sr_native_debug_motion_coverage_pso_failure)) return nullptr;
  reg::RB_BLENDCONTROL blend{LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_BLENDCONTROL0)};
  reg::PA_SU_SC_MODE_CNTL mode{LoadReg(base, dev, rex::graphics::XE_GPU_REG_PA_SU_SC_MODE_CNTL)};
  reg::RB_DEPTHCONTROL depth{LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_DEPTHCONTROL)};
  uint32_t color_mask = LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_COLOR_MASK);
  uint32_t stencil_ref_mask = LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_STENCILREFMASK);
  uint32_t decl_count = decl ? Load32(base, decl + 0x18) : 0;
  uint64_t decl_hash = decl ? XXH3_64bits(GuestPtr(base, decl + 0x34, decl_count * 12), size_t(decl_count) * 12) : 0;
  // Polygon offset (decals over coplanar terrain). PA_SU_POLY_OFFSET_* are not
  // in the XDK register shadow: read them from the PM4 mirror (same choice of
  // face as Xenia's GetPreferredFacePolygonOffset).
  float poly_scale = 0.0f, poly_offset = 0.0f;
  {
    auto regf = [&](uint32_t r) {
      uint32_t v = mirror_.reg(r);
      float f;
      std::memcpy(&f, &v, 4);
      return f;
    };
    bool polygonal = topology_type == D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    if (polygonal) {
      if (mode.poly_offset_front_enable && !mode.cull_front) {
        poly_scale = regf(rex::graphics::XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE);
        poly_offset = regf(rex::graphics::XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
      }
      if (mode.poly_offset_back_enable && !mode.cull_back && !poly_scale && !poly_offset) {
        poly_scale = regf(rex::graphics::XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_SCALE);
        poly_offset = regf(rex::graphics::XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_OFFSET);
      }
    } else if (mode.poly_offset_para_enable) {
      poly_scale = regf(rex::graphics::XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE);
      poly_offset = regf(rex::graphics::XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
    }
    if (!std::isfinite(poly_scale)) poly_scale = 0.0f;
    if (!std::isfinite(poly_offset)) poly_offset = 0.0f;
  }
  int32_t depth_bias = rex::graphics::draw_util::GetD3D10IntegerPolygonOffset(
      rex::graphics::xenos::DepthRenderTargetFormat::kD24S8, poly_offset);
  float slope_bias = poly_scale * rex::graphics::xenos::kPolygonOffsetScaleSubpixelUnit;
  if (trace_state_ == 1) {
    REXLOG_INFO("trace   pso mode {:08X} poly scale {} offset {} bias {} depthctl {:08X} refmask {:08X} "
                "samples {} a2c {}",
                mode.value, poly_scale, poly_offset, depth_bias, depth.value, stencil_ref_mask,
                bound_samples_, (LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_COLORCONTROL) >> 4) & 1);
  }
  current_motion_raster_.depth_bias=depth_bias;current_motion_raster_.slope_bias=slope_bias;
  current_motion_raster_.cull=mode.cull_front && !mode.cull_back ? D3D12_CULL_MODE_FRONT :
      mode.cull_back && !mode.cull_front ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
  current_motion_raster_.front_ccw=(mode.face ? false : true) ^ bool(REXCVAR_GET(sr_native_flip_winding));
  const uint32_t motion_color=LoadReg(base,dev,rex::graphics::XE_GPU_REG_RB_COLORCONTROL);
  current_motion_raster_.shader=vs_hash;current_motion_raster_.depth_control=depth.value;
  current_motion_raster_.blend_control=blend.value;current_motion_raster_.color_control=motion_color;
  current_motion_raster_.color_mask=color_mask;current_motion_raster_.stencil_enabled=depth.stencil_enable;
  current_motion_raster_.z_write=depth.z_write_enable;
  current_motion_raster_.coverage_id=coverage_variant ? motion_coverage_draw_id_ : 0;
  current_motion_raster_.opaque=graphics::CanReplayMotionRasterCoverage(bound_samples_,depth.z_enable,
      depth.z_write_enable,depth.stencil_enable,REXCVAR_GET(sr_native_debug_no_ztest),
      blend.value,motion_color,current_motion_raster_.coverage_id);
  uint32_t slope_bits;
  std::memcpy(&slope_bits, &slope_bias, 4);

  // Alpha to mask (RB_COLORCONTROL bit 4): MSAA coverage from alpha (foliage).
  bool alpha_to_coverage =
      (bound_samples_ > 1 && REXCVAR_GET(sr_native_alpha_to_coverage) &&
       ((LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_COLORCONTROL) >> 4) & 1)) ||
      AlphaTestToCoverage(base, dev);
  uint64_t key_parts[] = {vs_hash ^ (uint64_t(bound_samples_) << 60) ^
                              (uint64_t(alpha_to_coverage) << 59),
                          (uint64_t(uint32_t(depth_bias)) << 32) | slope_bits,
                          ps_hash ^ (ray_variant ? 0x5352525453480001ull : 0) ^
                              (coverage_variant ? 0x5352434F56450001ull : 0),
                          decl_hash,
                          blend.value,
                          uint64_t(mode.value & 0x7) | (uint64_t(color_mask & 0xFFFF) << 8) |
                              (uint64_t(topology_type) << 40),
                          depth.value | (uint64_t(stencil_ref_mask & 0xFFFF00) << 32),
                          uint64_t(rtv_formats[0]) | (uint64_t(rtv_formats[1]) << 8) |
                              (uint64_t(rtv_formats[2]) << 16) | (uint64_t(rtv_formats[3]) << 24) |
                              (uint64_t(rt_count) << 32) | (uint64_t(dsv_format) << 40) |
                              (uint64_t(strip_cut) << 56)};
  uint64_t key = XXH3_64bits(key_parts, sizeof(key_parts));
  if (!async_pipelines_.empty()) {
    auto ap = async_pipelines_.find(key);
    if (ap != async_pipelines_.end()) {
      ComPtr<ID3D12PipelineState> ready;
      switch (pipeline_cache_.TryTake(key, ready)) {
        case PipelineCache::Poll::kReady:
          async_pipelines_.erase(ap);
          pipelines_[key] = ready;
          return ready.Get();
        case PipelineCache::Poll::kFailed:
          async_pipelines_.erase(ap);
          pipelines_[key] = nullptr;
          return nullptr;
        default:
          ++stats_.pso_async_skipped_draws;
          return nullptr;
      }
    }
  }
  auto it = pipelines_.find(key);
  if (it != pipelines_.end()) {
    return it->second.Get();
  }
  pipelines_[key] = nullptr;
  {
    // Precompiled at startup (or compiling right now) by the pipeline cache.
    ComPtr<ID3D12PipelineState> precompiled;
    if (!special_variant && pipeline_cache_.Take(key, precompiled)) {
      pipelines_[key] = precompiled;
      ++stats_.pso_precompiled_hits;
      return precompiled.Get();
    }
  }

  const auto* vs = LoadShader(vs_hash, true);
  bool pending = shader_pending_;
  // ps_hash == 0: depth-only draw (the game binds no pixel shader).
  const auto* ps = coverage_variant ? motion_coverage_shader_ :
      ray_variant ? &rt_shadow_shader_ : ps_hash ? LoadShader(ps_hash, false) : nullptr;
  pending = pending || shader_pending_;
  if (!vs || (ps_hash && !ps)) {
    if (pending) pipelines_.erase(key);  // ask again once the translation finished
    return nullptr;
  }

  std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
  PsoRecord record;
  // The game's declarations describe every attribute of the mesh; a vertex
  // shader that reads fewer must not be given the rest (D3D12 rejects the
  // pipeline otherwise).
  const std::vector<std::pair<std::string, uint32_t>> vs_inputs = VertexShaderInputs(*vs);
  for (uint32_t i = 0; i < decl_count; ++i) {
    uint32_t e = decl + 0x34 + 12 * i;
    uint32_t stream = Load16(base, e);
    uint32_t offset = Load16(base, e + 2);
    uint32_t type = Load32(base, e + 4);
    uint32_t usage = Load8(base, e + 9);
    uint32_t usage_index = Load8(base, e + 10);
    if (stream == 0xFF) break;
    DeclFormat fmt = MapDeclType(type);
    if (fmt.format == DXGI_FORMAT_UNKNOWN) {
      LogUnsupportedOnce("vertex element type", type, "not in MapDeclType (the element is dropped)");
      continue;
    }
    if (!vs_inputs.empty() &&
        std::find(vs_inputs.begin(), vs_inputs.end(),
                  std::make_pair(std::string(UsageSemantic(usage)), usage_index)) == vs_inputs.end()) {
      continue;
    }
    D3D12_INPUT_ELEMENT_DESC d{};
    d.SemanticName = UsageSemantic(usage);
    d.SemanticIndex = usage_index;
    d.Format = fmt.format;
    d.InputSlot = stream;
    d.AlignedByteOffset = offset;
    d.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    elements.push_back(d);
    if (record.element_count < PsoRecord::kMaxElements) {
      auto& re = record.elements[record.element_count++];
      re.usage = uint8_t(usage);
      re.usage_index = uint8_t(usage_index);
      re.slot = uint8_t(stream);
      re.format = uint32_t(fmt.format);
      re.offset = offset;
    }
  }

  // The XDK patches absent attributes to constants. A zero-stride host
  // buffer supplies the same value regardless of the draw's vertex index.
  const auto float_inputs = VertexShaderInputs(*vs, true);
  for (const auto& input : float_inputs) {
    if (std::any_of(elements.begin(), elements.end(), [&](const auto& e) {
          return input.first == e.SemanticName && input.second == e.SemanticIndex;
        })) continue;
    uint32_t usage = 0;
    while (usage < 14 && input.first != UsageSemantic(usage)) ++usage;
    if (usage == 14) continue;
    D3D12_INPUT_ELEMENT_DESC e{};
    e.SemanticName = UsageSemantic(usage);
    e.SemanticIndex = input.second;
    e.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    e.InputSlot = 16;
    e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    elements.push_back(e);
    if (record.element_count < PsoRecord::kMaxElements) {
      auto& re = record.elements[record.element_count++];
      re.usage = uint8_t(usage);
      re.usage_index = uint8_t(input.second);
      re.slot = 16;
      re.format = uint32_t(e.Format);
    }
  }
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
  desc.pRootSignature = root_signature_.Get();
  desc.VS = {vs->data(), vs->size()};
  if (ps) {
    desc.PS = {ps->data(), ps->size()};
  }
  desc.InputLayout = {elements.data(), uint32_t(elements.size())};
  D3D12_RENDER_TARGET_BLEND_DESC rt{};
  rt.SrcBlend = MapBlend(blend.color_srcblend);
  rt.DestBlend = MapBlend(blend.color_destblend);
  rt.BlendOp = MapBlendOp(blend.color_comb_fcn);
  rt.SrcBlendAlpha = MapBlendAlpha(blend.alpha_srcblend);
  rt.DestBlendAlpha = MapBlendAlpha(blend.alpha_destblend);
  rt.BlendOpAlpha = MapBlendOp(blend.alpha_comb_fcn);
  rt.BlendEnable = !(rt.SrcBlend == D3D12_BLEND_ONE && rt.DestBlend == D3D12_BLEND_ZERO &&
                     rt.BlendOp == D3D12_BLEND_OP_ADD && rt.SrcBlendAlpha == D3D12_BLEND_ONE &&
                     rt.DestBlendAlpha == D3D12_BLEND_ZERO &&
                     rt.BlendOpAlpha == D3D12_BLEND_OP_ADD);
  rt.LogicOp = D3D12_LOGIC_OP_NOOP;
  desc.BlendState.IndependentBlendEnable = TRUE;
  desc.BlendState.AlphaToCoverageEnable = alpha_to_coverage;
  for (uint32_t i = 0; i < 4; ++i) {
    desc.BlendState.RenderTarget[i] = rt;
    desc.BlendState.RenderTarget[i].RenderTargetWriteMask = uint8_t((color_mask >> (4 * i)) & 0xF);
    // Integer/float formats that can't blend.
    if (rtv_formats[i] == DXGI_FORMAT_R32_FLOAT || rtv_formats[i] == DXGI_FORMAT_R32G32_FLOAT) {
      desc.BlendState.RenderTarget[i].BlendEnable = FALSE;
    }
  }
  if(coverage_variant) {
    auto& owner=desc.BlendState.RenderTarget[1];
    owner.BlendEnable=FALSE;owner.LogicOpEnable=FALSE;owner.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_RED;
  }
  desc.SampleMask = UINT_MAX;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  if (mode.cull_front && !mode.cull_back) {
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
  } else if (mode.cull_back && !mode.cull_front) {
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  } else {
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  }
  desc.RasterizerState.FrontCounterClockwise =
      (mode.face ? FALSE : TRUE) ^ (REXCVAR_GET(sr_native_flip_winding) ? TRUE : FALSE);
  desc.RasterizerState.DepthClipEnable = TRUE;
  desc.RasterizerState.DepthBias = depth_bias;
  desc.RasterizerState.SlopeScaledDepthBias = slope_bias;
  if (dsv_format != DXGI_FORMAT_UNKNOWN) {
    auto& ds = desc.DepthStencilState;
    ds.DepthEnable = depth.z_enable && !REXCVAR_GET(sr_native_debug_no_ztest);
    ds.DepthWriteMask = depth.z_write_enable ? D3D12_DEPTH_WRITE_MASK_ALL
                                             : D3D12_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = MapCompare(depth.zfunc);
    ds.StencilEnable = depth.stencil_enable;
    ds.StencilReadMask = uint8_t(stencil_ref_mask >> 8);
    ds.StencilWriteMask = uint8_t(stencil_ref_mask >> 16);
    ds.FrontFace.StencilFunc = MapCompare(depth.stencilfunc);
    ds.FrontFace.StencilFailOp = MapStencilOp(depth.stencilfail);
    ds.FrontFace.StencilPassOp = MapStencilOp(depth.stencilzpass);
    ds.FrontFace.StencilDepthFailOp = MapStencilOp(depth.stencilzfail);
    if (depth.backface_enable) {
      ds.BackFace.StencilFunc = MapCompare(depth.stencilfunc_bf);
      ds.BackFace.StencilFailOp = MapStencilOp(depth.stencilfail_bf);
      ds.BackFace.StencilPassOp = MapStencilOp(depth.stencilzpass_bf);
      ds.BackFace.StencilDepthFailOp = MapStencilOp(depth.stencilzfail_bf);
    } else {
      ds.BackFace = ds.FrontFace;
    }
    desc.DSVFormat = dsv_format;
  }
  desc.PrimitiveTopologyType = topology_type;
  desc.IBStripCutValue = strip_cut;
  desc.NumRenderTargets = rt_count;
  for (uint32_t i = 0; i < rt_count; ++i) {
    desc.RTVFormats[i] = rtv_formats[i] != DXGI_FORMAT_UNKNOWN ? rtv_formats[i] : rtv_formats[0];
  }
  desc.SampleDesc.Count = bound_samples_;
  auto fill_record = [&] {
    record.key = key;
    record.vs_hash = vs_hash;
    record.ps_hash = ps_hash;
    record.blend = desc.BlendState;
    record.raster = desc.RasterizerState;
    record.depth_stencil = desc.DepthStencilState;
    for (uint32_t i = 0; i < 8; ++i) record.rtv_formats[i] = desc.RTVFormats[i];
    record.dsv_format = desc.DSVFormat;
    record.num_render_targets = desc.NumRenderTargets;
    record.sample_count = desc.SampleDesc.Count;
    record.topology_type = uint32_t(desc.PrimitiveTopologyType);
    record.strip_cut = uint32_t(desc.IBStripCutValue);
  };
  if (!special_variant && elements.size() <= PsoRecord::kMaxElements) {
    // Same description precompiled under another key: a settings variant
    // generated from a pipeline captured with other MSAA/foliage settings, or
    // guest state bits that do not change the host pipeline (EXP-052).
    fill_record();
    ComPtr<ID3D12PipelineState> variant;
    if (pipeline_cache_.TakeByDesc(record, variant)) {
      pipeline_cache_.Record(record);
      ++stats_.pso_precompiled_hits;
      pipelines_[key] = variant;
      return variant.Get();
    }
  }
  // Asynchronous compilation (EXP-053) only in the passes the game redraws from
  // scratch every frame: a skipped draw there reappears as soon as its
  // pipeline is ready. Everything that may carry state across frames (post
  // process, the luminance/exposure chain, copies, HUD, menus) still waits: a
  // skipped one-time draw can break the image permanently (EXP-005).
  if (!special_variant && REXCVAR_GET(sr_native_async_pipelines) && REXCVAR_GET(sr_native_pipeline_cache) &&
      elements.size() <= PsoRecord::kMaxElements) {
    int p = g_current_pass;
    // Conan: shadow maps, reflections, depth, decals, opaque (+character),
    // skybox, ocean, sorted. SR: from game_profile.h (none known yet).
    bool redrawn = false;
    for (int r : profile::kRedrawnPasses) redrawn |= r >= 0 && p == r;
    if (redrawn) {
      pipeline_cache_.Record(record);
      pipeline_cache_.CompileAsync(record);
      pipelines_.erase(key);
      async_pipelines_.insert(key);
      ++stats_.pso_async_requests;
      ++stats_.pso_async_skipped_draws;
      REXLOG_INFO("native: PSO compiling asynchronously (pass {} vs {:016X} ps {:016X})", p,
                  vs_hash, ps_hash);
      return nullptr;
    }
  }
  ComPtr<ID3D12PipelineState> pso;
  auto create_start = std::chrono::steady_clock::now();
  HRESULT hr = device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
  if (FAILED(hr)) {
    std::string layout;
    for (const auto& e : elements) {
      layout += fmt::format(" {}{}:s{}+{}:f{}", e.SemanticName, e.SemanticIndex, e.InputSlot,
                            e.AlignedByteOffset, uint32_t(e.Format));
    }
    std::string targets;
    for (uint32_t i = 0; i < rt_count; ++i) targets += fmt::format(" {}", uint32_t(rtv_formats[i]));
    REXLOG_ERROR("native: PSO creation failed ({:08X}) vs={:016X} ps={:016X} layout:{} rtv:{} dsv {} "
                 "samples {}", uint32_t(hr), vs_hash, ps_hash, layout, targets,
                 uint32_t(dsv_format), desc.SampleDesc.Count);
    return nullptr;
  }
  {
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                          create_start).count();
    stats_.pso_create_ms += ms;
    REXLOG_INFO("native: PSO #{} created in {:.2f} ms (pass {} vs {:016X} ps {:016X})",
                stats_.pso_created + 1, ms, g_current_pass, vs_hash, ps_hash);
    std::string layout;
    for (const auto& e : elements) {
      layout += fmt::format(" {}{}:s{}+{}:f{}", e.SemanticName, e.SemanticIndex, e.InputSlot,
                            e.AlignedByteOffset, uint32_t(e.Format));
    }
    std::string raw;
    for (uint32_t i = 0; i < decl_count; ++i) {
      uint32_t e = decl + 0x34 + 12 * i;
      raw += fmt::format(" u{}.{}:s{}+{}:t{:X}", Load8(base, e + 9), Load8(base, e + 10),
                         Load16(base, e), Load16(base, e + 2), Load32(base, e + 4));
    }
    REXLOG_INFO("native: PSO layout vs {:016X} host:{} | guest decl:{}", vs_hash, layout, raw);
    {
      const auto& b0 = desc.BlendState.RenderTarget[0];
      REXLOG_INFO("native: PSO state vs {:016X} ps {:016X}: blend {} src {} dst {} op {} alpha-src {} alpha-dst {} "
                  "mask {:X} a2c {} depth {} write {} func {} cull {} rtv0 {} samples {}", vs_hash, ps_hash,
                  b0.BlendEnable ? 1 : 0, int(b0.SrcBlend), int(b0.DestBlend), int(b0.BlendOp),
                  int(b0.SrcBlendAlpha), int(b0.DestBlendAlpha), unsigned(b0.RenderTargetWriteMask),
                  desc.BlendState.AlphaToCoverageEnable ? 1 : 0, desc.DepthStencilState.DepthEnable ? 1 : 0,
                  int(desc.DepthStencilState.DepthWriteMask), int(desc.DepthStencilState.DepthFunc),
                  int(desc.RasterizerState.CullMode), int(desc.RTVFormats[0]), desc.SampleDesc.Count);
    }
  }
  // Remember it for the next start's precompilation.
  if (!special_variant && elements.size() <= PsoRecord::kMaxElements) {
    pipeline_cache_.Record(record);
  }
  ++stats_.pso_created;
  pipelines_[key] = pso;
  return pso.Get();
}

uint32_t Renderer::GetTextureSrvIndex(uint8_t* base, const uint32_t fetch[6]) {
  CpuTimer cpu_timer(cpu_timings.texture_lookup_ns);
  // Textures produced by resolves live on the host GPU only.
  uint32_t base_address = GuestPhysical(fetch[1] & 0xFFFFF000u);
  auto latest = resolve_latest_.find(base_address);
  auto rit = latest != resolve_latest_.end() ? resolve_textures_.find(latest->second)
                                             : resolve_textures_.end();
  if (rit != resolve_textures_.end()) {
    ResolveTexture& rt = rit->second;
    if(rt.stacked_volume) {
      const auto plan=PlanStackedResolve(std::span<const uint32_t,6>(fetch,6),0,0);
      if(!plan || plan->width!=rt.width || plan->height!=rt.height || plan->layers!=rt.layers) {
        LogUnsupportedOnce("stacked resolve binding",base_address,"fetch does not match the host volume");
        return 0;
      }
    }
    xenos::xe_gpu_texture_fetch_t f;
    std::memcpy(&f, fetch, sizeof(f));
    // Component mapping from the fetch swizzle (source components clamped to
    // the resolve format's channel count happen implicitly - missing
    // channels read 0/1 in D3D12).
    uint32_t mapping[4];
    static const uint32_t kSwapRB[4] = {2, 1, 0, 3};
    for (int i = 0; i < 4; ++i) {
      uint32_t s = (f.swizzle >> (3 * i)) & 7;
      if (s < 4 && rt.swap_rb) s = kSwapRB[s];
      mapping[i] = s < 4 ? s
                         : (s == 4 ? D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0
                                   : D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
      if (rt.srv_format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
          rt.srv_format == DXGI_FORMAT_R32_FLOAT) {
        if (mapping[i] < 4) mapping[i] = 0;  // single-channel: replicate
      }
    }
    uint32_t encoded = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(mapping[0], mapping[1], mapping[2],
                                                               mapping[3]);
    // A depth resolve fetched as k_8_8_8_8 reads the raw D24S8 bytes.
    if (rt.raw_resource && (fetch[1] & 0x3F) == uint32_t(xenos::TextureFormat::k_8_8_8_8)) {
      uint32_t raw_mapping[4];
      for (int i = 0; i < 4; ++i) {
        uint32_t s = (f.swizzle >> (3 * i)) & 7;
        raw_mapping[i] = s < 4 ? s
                               : (s == 4 ? D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0
                                         : D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
      }
      uint32_t raw_encoded = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
          raw_mapping[0], raw_mapping[1], raw_mapping[2], raw_mapping[3]);
      auto rit2 = rt.raw_srv_by_mapping.find(raw_encoded);
      if (rit2 != rt.raw_srv_by_mapping.end()) return rit2->second;
      uint32_t index = AllocSrvIndex();
      D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
      srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv.Shader4ComponentMapping = raw_encoded;
      srv.Texture2D.MipLevels = 1;
      device_->CreateShaderResourceView(
          rt.raw_resource.Get(), &srv,
          provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(), index));
      rt.raw_srv_by_mapping[raw_encoded] = index;
      return index;
    }
    auto sit = rt.srv_by_mapping.find(encoded);
    if (sit != rt.srv_by_mapping.end()) {
      return sit->second;
    }
    uint32_t index = AllocSrvIndex();
    if(!index && rt.stacked_volume) return 0;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = rt.srv_format;
    srv.ViewDimension = rt.stacked_volume?D3D12_SRV_DIMENSION_TEXTURE3D:D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = encoded;
    if(rt.stacked_volume) srv.Texture3D.MipLevels=1;
    else srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(
        rt.resource.Get(), &srv,
        provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(), index));
    rt.srv_by_mapping[encoded] = index;
    return index;
  }

  // Sign fields (dword 0 bits 2-9) are part of the key: gamma changes the view.
  uint64_t key_parts[4] = {fetch[1] | (uint64_t((fetch[0] >> 2) & 0xFF) << 32), fetch[2],
                           fetch[3] & 0x1FFE, fetch[5] & 0xFFFFFE00u};
  uint64_t key = texture_tracking_mode_==TextureTrackingMode::On ? XXH3_64bits(fetch,6*sizeof(uint32_t)) : XXH3_64bits(key_parts, sizeof(key_parts));
  auto it = textures_.find(key);
  if(it!=textures_.end() && texture_tracking_mode_==TextureTrackingMode::On &&
      !TextureDescriptorMatches(it->second.descriptor,std::span<const uint32_t,6>(fetch,6))) {
    // A hash bucket is not descriptor identity. Retire even a colliding key.
    Retire(it->second.resource);RetireSrvIndex(it->second.srv_index);
    textures_.erase(it);it=textures_.end();
  }
  if (it != textures_.end()) {
    TextureEntry& entry = it->second;
    if (entry.checked_frame != frame_count_ && !entry.guest_ranges.empty()) {
      entry.checked_frame = frame_count_;
      // With write watches: guest physical writes trigger a check immediately.
      // Dynamic movie and UI textures (up to 4MB) are periodically revalidated
      // because guest virtual CPU writes (video codecs) bypass physical write watches.
      // Larger unwatched textures are checked every 30 frames.
      const auto written_since = [this](uint32_t address, uint32_t size, uint32_t seq) {
        return TextureWrittenSince(address, size, seq);
      };
      const auto hash_content = [&entry] {
        return HashTextureContent(entry.guest_ranges, [](uint32_t address) {
          return REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(address);
        }, TextureHash());
      };
      bool reused_by_tracking = false;
      if(texture_tracking_mode_==TextureTrackingMode::On && entry.tracking) {
        auto& tracking=*entry.tracking;
        const auto query_start=CpuProfiling()?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        resource_write_tracker_.Sample(tracking.watch,tracking.current);
        if(CpuProfiling()) tracking_query_ns_+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-query_start).count();
        reused_by_tracking=CanReuseTexture(tracking.upload,tracking.current,entry.resource?entry.resource_version:0);
      }
      CpuTimer watch_timer(cpu_timings.texture_watch_ns);
      const bool written = !reused_by_tracking && texture_watch_ &&
          (TextureWatchScanCaching() ? [&] { bool reused=false; return entry.watch_scan.Written(0, completed_texture_writes_.load(std::memory_order_acquire), entry.watch_seq, [&] { return TextureContentWritten(entry.guest_ranges, entry.watch_seq, written_since); }, reused); }() : TextureContentWritten(entry.guest_ranges, entry.watch_seq, written_since));
      watch_timer.Stop();
      // Preserve the existing base-level threshold; adding mip coverage must
      // not move an existing per-frame texture into the slower policy.
      bool check = entry.hash_backoff.ShouldValidate(entry.guest_size,texture_watch_,written,
          frame_count_,entry.hashed_frame,REXCVAR_GET(sr_native_texture_hash_backoff));
      if(check && reused_by_tracking) { check=false;++tracking_skipped_checks_;tracking_skipped_bytes_+=entry.guest_hash_bytes; }
      if (REXCVAR_GET(sr_native_debug_texture_format) >= 0 &&
          int32_t(fetch[1] & 0x3F) == REXCVAR_GET(sr_native_debug_texture_format)) {
        static int logged = 0;
        uint64_t h = hash_content();
        if (logged < 400 && (check || h != entry.content_hash)) {
          ++logged;
          REXLOG_INFO("native dbg tex check {:08X} frame {} written {} hash {} now {} seq {} global {}",
                      entry.guest_base, frame_count_, check, entry.content_hash, h, entry.watch_seq,
                      write_seq_.load());
        }
      }
      if (check) {
        auto* tracking=entry.tracking.get();
        const bool tracking_timed=texture_tracking_mode_==TextureTrackingMode::Compare || CpuProfiling();
        const auto tracking_started=tracking && tracking_timed?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        if(tracking) {
          resource_write_tracker_.Sample(tracking->watch,tracking->before);
          if(tracking_timed) tracking_query_ns_+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-tracking_started).count();
        }
        entry.hashed_frame = frame_count_;
        // A clean watch is still armed. Reprotect only after a physical write
        // notification has unwatched its pages. Keep the per-frame hash above:
        // virtual aliases can change movie/UI bytes without that notification.
        // ArmTextureWatch takes its sequence before protecting/reading, so a
        // write racing with this hash remains newer than entry.watch_seq.
        if (written) {
          CpuTimer rearm_timer(cpu_timings.texture_watch_ns);
          entry.watch_seq = write_seq_.load(std::memory_order_acquire);
          for (const auto& range : entry.guest_ranges)
            ArmTextureWatch(range.address, range.length);
          if (CpuProfiling()) ++cpu_timings.texture_watch_rearms;
        }
        const bool probe = TextureSourceProbing() &&
            SelectTextureSourceProbe("d3d12", frame_count_, entry.guest_hash_bytes);
        CpuTimer hash_timer(cpu_timings.texture_hash_ns);
        const auto probe_start = probe ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        uint64_t hash = hash_content();
        const double probe_ns = probe ? std::chrono::duration<double, std::nano>(
            std::chrono::steady_clock::now() - probe_start).count() : 0;
        hash_timer.Stop();
        if(texture_tracking_mode_!=TextureTrackingMode::Off) {
          ++tracking_checks_;tracking_bytes_+=entry.guest_hash_bytes;
          if(!tracking) ++tracking_unknown_;
        }
        if(tracking) {
          const auto tracking_finished=tracking_timed?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
          resource_write_tracker_.Sample(tracking->watch,tracking->current);
          if(tracking_timed) tracking_query_ns_+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-tracking_finished).count();
          bool unknown=false,invalidated=false,active=false;
          for(const auto stamp:tracking->current) {
            unknown|=stamp.coverage==RexTrackingCoverage::Unknown;
            invalidated|=stamp.coverage==RexTrackingCoverage::Invalidated;
            active|=stamp.active_writers!=0;
          }
          tracking_unknown_+=unknown;tracking_invalidated_+=invalidated;tracking_active_+=active;
          if(tracking->proof.version && hash==tracking->proof.hash && !SameCoveredContent(tracking->proof.baseline,tracking->current)) ++tracking_unchanged_lost_;
          const bool stable=SameCoveredContent(tracking->before,tracking->current);
          if(stable && SameCoveredContent(tracking->proof.baseline,tracking->current))
            tracking_eligible_bytes_+=entry.guest_hash_bytes;
          if(!stable) ++tracking_races_;
          if(AuditAndInvalidateTextureProof(*tracking,hash)) {
            ++tracking_mismatches_;
            REXLOG_ERROR("native texture tracking: coverage mismatch base={:08X}; allocation disqualified",entry.guest_base);
          }

        }
        if (probe) MeasureTextureSource(entry.guest_ranges,
            [](uint32_t address, uint32_t) { return REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(address); },
            0, hash, probe_ns);
        if(CpuProfiling()) {cpu_timings.texture_hash_bytes+=entry.guest_hash_bytes;++cpu_timings.texture_hash_checks;}
        if(TextureAuditing()) {
          ObserveTextureValidationForUnlockAudit(fetch,entry.guest_ranges);
          // A write may have arrived during hashing, after the pre-hash test
          // or rearm. Include it before classifying an unobserved change.
          const bool notified=written || (texture_watch_ &&
              TextureContentWritten(entry.guest_ranges,entry.watch_seq,written_since));
          AuditTextureHash("d3d12",frame_count_,entry.guest_base,entry.guest_hash_bytes,
              entry.guest_format,hash!=entry.content_hash,notified,texture_watch_);
        }
        if (TextureHashReplayEnabled())
          MaybeReplayTextureHash(std::span<const uint32_t, 6>(fetch, 6), frame_count_, "d3d12");
        entry.hash_backoff.OnHashed(frame_count_,hash==entry.content_hash,
            texture_watch_ && !written && hash!=entry.content_hash);
        if (hash != entry.content_hash) {
          if (CpuProfiling()) ++cpu_timings.texture_hash_changes;
          ++stats_.textures_reloaded;
          Retire(entry.resource);
          RetireSrvIndex(entry.srv_index);
          entry.resource.Reset();
          entry.srv_index = 0;
          if (!CreateTexture(fetch, entry)) {
            textures_.erase(it);
            return 0;
          }
        }
      }
    }
    ReportTextureTracking();
    return entry.srv_index;
  }
  TextureEntry& entry = textures_[key];
  if (!CreateTexture(fetch, entry)) {
    textures_.erase(key);
    return 0;
  }
  return entry.srv_index;
}

void Renderer::RetireSrvIndex(uint32_t index) {
  if (index > 2) retired_srvs_.emplace_back(next_fence_value_, index);
}

std::pair<uint32_t, uint32_t> Renderer::OnPhysicalWrite(void* context, uint32_t start,
                                                        uint32_t length, bool exact_range) {
  auto* self = static_cast<Renderer*>(context);
  uint32_t seq = self->write_seq_.fetch_add(1, std::memory_order_relaxed) + 1;
  uint32_t first = start >> 12;
  uint32_t last = std::min((uint64_t(start) + std::max(length, 1u) - 1) >> 12, uint64_t(0x1FFFF));
  for (uint32_t p = first; p <= last; ++p) {
    self->page_write_seq_[p].store(seq, std::memory_order_release);
  }
  self->completed_texture_writes_.fetch_add(1,std::memory_order_release);
  // Only the written pages are unwatched (and marked above).
  return {start, length};
}

uint32_t Renderer::ArmTextureWatch(uint32_t base, uint32_t size) {
  // Sequence taken before protecting: any later write is newer.
  uint32_t seq = write_seq_.load(std::memory_order_acquire);
  REX_KERNEL_MEMORY()->EnablePhysicalMemoryAccessCallbacks(base & 0x1FFFFFFF, size, true, false);
  return seq;
}

bool Renderer::TextureWrittenSince(uint32_t base, uint32_t size, uint32_t seq) const {
  uint32_t first = (base & 0x1FFFFFFF) >> 12;
  uint32_t last = std::min(((base & 0x1FFFFFFF) + size - 1) >> 12, 0x1FFFFu);
  for (uint32_t p = first; p <= last; ++p) {
    if (int32_t(page_write_seq_[p].load(std::memory_order_acquire) - seq) > 0) return true;
  }
  return false;
}

void Renderer::ReportTextureTracking() {
  if(texture_tracking_mode_!=TextureTrackingMode::Off && frame_count_>=tracking_report_frame_+120) {
    tracking_report_frame_=frame_count_;
    const auto counters=resource_write_tracker_.Stats();
    REXLOG_INFO("native texture tracking: frame={} checks={} bytes={} eligible_bytes={} candidates={} uncovered_checks={} mismatches={} watches={} guest_faults={} physical_faults={} io_begin={} io_end={} protection_failures={} coverage_losses={} skipped_checks={} skipped_bytes={} query_ns={} unknown_checks={} invalidated_checks={} active_checks={} unchanged_lost_checks={}",
      frame_count_,tracking_checks_,tracking_bytes_,tracking_eligible_bytes_,tracking_candidates_,tracking_races_,tracking_mismatches_,counters.active_watches,counters.guest_faults,counters.physical_faults,counters.io_begin,counters.io_end,counters.protection_failures,counters.coverage_losses,tracking_skipped_checks_,tracking_skipped_bytes_,tracking_query_ns_,tracking_unknown_,tracking_invalidated_,tracking_active_,tracking_unchanged_lost_);
  }
}

bool Renderer::CaptureTextureProof(const uint32_t fetch[6],TextureEntry& entry,DecodedTexture& decoded_out) {
  // One bounded session attempt per frame. A pending pre-session I/O may finish later.
  if(texture_tracking_start_frame_!=frame_count_) {
    texture_tracking_start_frame_=frame_count_;
    if(!resource_write_tracker_.Start(*REX_KERNEL_MEMORY()) && frame_count_<2)
      REXLOG_WARN("native texture tracking: session unavailable status={}",uint32_t(resource_write_tracker_.last_status()));
  }
  std::vector<graphics::guest::TextureRange> ranges;
  std::string error;
  if(!graphics::guest::DescribeTextureRanges(std::span<const uint32_t,6>(fetch,6),ranges,error)) return false;
  std::vector<RexTrackingRange> watched;
  for(const auto r:ranges) watched.push_back({r.address,r.length});
  auto candidate=std::make_unique<TextureTrackingEntry>();
  candidate->owner=&resource_write_tracker_;
  candidate->watch=resource_write_tracker_.Watch(watched);
  if(!candidate->watch) return false;
  candidate->before.resize(ranges.size());candidate->current.resize(ranges.size());
  if(texture_tracking_version_==UINT64_MAX) return false;
  candidate->version=++texture_tracking_version_;
  entry.tracking=std::move(candidate);
  auto& tracked=*entry.tracking;
  resource_write_tracker_.Sample(tracked.watch,tracked.before);
  if(!SameCoveredContent(tracked.before,tracked.before)) return false;
  std::shared_ptr<const graphics::guest::TextureCapture> snapshot;
  if(!graphics::guest::CaptureTexture(std::span<const uint32_t,6>(fetch,6),tracked.version,
      [](uint32_t address,uint32_t length)->std::span<const uint8_t> {
        return {REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(address-0xa0000000u),length};
      },snapshot,error)) return false;
  resource_write_tracker_.Sample(tracked.watch,tracked.current);
  if(!SameCoveredContent(tracked.before,tracked.current)) return false;
  DecodedTexture decoded_candidate;
  if(!DecodeTexture(*snapshot,decoded_candidate)) return false;
  const auto hash=HashTextureContent(snapshot->ranges,[&](uint32_t address) {
    for(const auto range:snapshot->ranges) if(range.address==address)
      return snapshot->memory.Read(0xa0000000u+address,range.length).data();
    return static_cast<const uint8_t*>(nullptr);
  },TextureHash());
  resource_write_tracker_.Sample(tracked.watch,tracked.current);
  if(!tracked.proof.Publish(tracked.version,hash,tracked.before,tracked.current,tracked.current)) return false;
  ++tracking_candidates_; decoded_out=std::move(decoded_candidate); return true;
}

uint32_t Renderer::CreateTexture(const uint32_t fetch[6], TextureEntry& entry) {
  entry.tracking.reset();entry.resource_version=0;
  // Watch before reading: arm, hash, then decode. A guest write after arming
  // (e.g. a streaming loader filling the texture while the worker decodes it)
  // is always seen by the next revalidation.
  std::vector<graphics::guest::TextureRange> ranges;
  std::string range_error;
  if (!graphics::guest::DescribeTextureRanges(std::span<const uint32_t, 6>(fetch, 6),
                                             ranges, range_error)) return 0;
  const uint32_t pre_seq = write_seq_.load(std::memory_order_acquire);
  if (texture_watch_)
    for (const auto& range : ranges) ArmTextureWatch(range.address, range.length);
  // Hash before decode even without watches. Hashing only afterwards could
  // accept a newer hash while keeping pixels decoded before a concurrent write.
  uint64_t pre_hash = HashTextureContent(ranges, [](uint32_t address) {
    return REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(address);
  }, TextureHash());
  DecodedTexture decoded;
  const char* reason = nullptr;
  bool decoded_ok = DecodeTexture(fetch, decoded, &reason);
  if (decoded_ok && REXCVAR_GET(sr_native_debug_texture_format) >= 0 &&
      int32_t(decoded.guest_format) == REXCVAR_GET(sr_native_debug_texture_format)) {
    REXLOG_INFO("native dbg texture fmt {} base {:08X} {}x{} dxgi {} mips {} fetch {:08X} {:08X} {:08X} {:08X}",
                decoded.guest_format, decoded.base_address, decoded.width, decoded.height,
                int(decoded.format), decoded.mip_levels, fetch[0], fetch[1], fetch[2], fetch[3]);
  }
  bool dump_by_format = REXCVAR_GET(sr_native_debug_texture_format) >= 0 &&
                        int32_t(decoded.guest_format) == REXCVAR_GET(sr_native_debug_texture_format) &&
                        decoded.width <= 512 && !REXCVAR_GET(sr_native_dump_dir).empty();
  if (decoded_ok && (dump_by_format || (!REXCVAR_GET(sr_native_dump_texture_addr).empty() &&
      REXCVAR_GET(sr_native_dump_texture_addr).find(fmt::format("{:08X}", decoded.base_address)) !=
          std::string::npos))) {
    std::string path = REXCVAR_GET(sr_native_dump_dir) + fmt::format("/tex_{:08X}_{}.dds",
                                                                  decoded.base_address, frame_count_);
    WriteDds(decoded, path.c_str());
    REXLOG_INFO("native: dumped texture {:08X} fmt {} endian {} -> {}", decoded.base_address,
                decoded.guest_format, (fetch[1] >> 6) & 3, path);
  }
  bool cube = decoded.dimension == uint32_t(xenos::DataDimension::kCube);
  bool volume = decoded.dimension == uint32_t(xenos::DataDimension::k3D);
  if (decoded_ok && !cube && !volume && decoded.depth != 1) {
    decoded_ok = false;
    reason = "stacked 2D array";
  }
  if (!decoded_ok) {
    LogUnsupportedOnce("texture", fetch[1] & 0x3F, reason);
    static std::set<uint64_t> logged;
    uint64_t k = (uint64_t(fetch[1]) << 32) | fetch[5];
    if (logged.size() < 200 && logged.insert(k).second) {
      REXLOG_INFO("native: texture decode failed ({}): pass {} fetch {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} dim {} fmt {}",
                  reason ? reason : "?", g_current_pass, fetch[0], fetch[1], fetch[2], fetch[3],
                  fetch[4], fetch[5], (fetch[5] >> 9) & 3, fetch[1] & 0x3F);
    }
    return 0;
  }
  if(texture_tracking_mode_!=TextureTrackingMode::Off) {
    DecodedTexture snapshot_decoded;
    if(CaptureTextureProof(fetch,entry,snapshot_decoded) && texture_tracking_mode_==TextureTrackingMode::On) {
      decoded=std::move(snapshot_decoded);pre_hash=entry.tracking->proof.hash;
    }
  }
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = volume ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  bool block_compressed = decoded.format >= DXGI_FORMAT_BC1_TYPELESS &&
                          decoded.format <= DXGI_FORMAT_BC5_SNORM;
  desc.Width = block_compressed ? (decoded.width + 3) & ~3u : decoded.width;
  desc.Height = block_compressed ? (decoded.height + 3) & ~3u : decoded.height;
  desc.DepthOrArraySize = uint16_t(decoded.depth);
  desc.MipLevels = uint16_t(decoded.mip_levels);
  // Xenos gamma textures (sign = GAMMA on RGB) are linearized when sampled
  // (piecewise-linear ~2.2 curve): sample them through an sRGB view.
  bool gamma = IsGammaFetch(fetch) && REXCVAR_GET(sr_native_texture_gamma) == 1;
  DXGI_FORMAT srv_format = decoded.format;
  DXGI_FORMAT resource_format = decoded.format;
  if (gamma) {
    switch (decoded.format) {
      case DXGI_FORMAT_R8G8B8A8_UNORM:
        resource_format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        srv_format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        break;
      case DXGI_FORMAT_B8G8R8A8_UNORM:
        resource_format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
        srv_format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        break;
      case DXGI_FORMAT_BC1_UNORM:
        resource_format = DXGI_FORMAT_BC1_TYPELESS;
        srv_format = DXGI_FORMAT_BC1_UNORM_SRGB;
        break;
      case DXGI_FORMAT_BC2_UNORM:
        resource_format = DXGI_FORMAT_BC2_TYPELESS;
        srv_format = DXGI_FORMAT_BC2_UNORM_SRGB;
        break;
      case DXGI_FORMAT_BC3_UNORM:
        resource_format = DXGI_FORMAT_BC3_TYPELESS;
        srv_format = DXGI_FORMAT_BC3_UNORM_SRGB;
        break;
      default:
        break;
    }
  }
  desc.Format = resource_format;
  desc.SampleDesc.Count = 1;
  if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&entry.resource)))) {
    return 0;
  }
  uint32_t subresources = decoded.mip_levels * (volume ? 1 : decoded.depth);
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subresources);
  std::vector<UINT> rows(subresources);
  std::vector<UINT64> row_sizes(subresources);
  UINT64 total = 0;
  device_->GetCopyableFootprints(&desc, 0, subresources, 0, layouts.data(), rows.data(),
                                 row_sizes.data(), &total);
  UploadAlloc upload;
  if (!Upload(size_t(total), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, upload)) {
    entry.resource.Reset();
    return 0;
  }
  UINT64 upload_base = upload.offset;
  if (volume) {
    // One subresource; the footprint holds all depth slices.
    for (uint32_t z = 0; z < decoded.depth; ++z) {
      const auto& level = decoded.levels[z];
      uint8_t* dst = upload.cpu + layouts[0].Offset +
                     size_t(z) * layouts[0].Footprint.RowPitch * rows[0];
      uint32_t copy_rows = std::min<uint32_t>(rows[0], level.rows);
      size_t copy_bytes = std::min<size_t>(size_t(row_sizes[0]), level.row_pitch);
      for (uint32_t r = 0; r < copy_rows; ++r) {
        std::memcpy(dst + size_t(r) * layouts[0].Footprint.RowPitch,
                    decoded.data.data() + level.offset + size_t(r) * level.row_pitch, copy_bytes);
      }
    }
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload.resource;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = layouts[0];
    src.PlacedFootprint.Offset += upload_base;
    D3D12_TEXTURE_COPY_LOCATION dst_loc{};
    dst_loc.pResource = entry.resource.Get();
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;
    command_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &src, nullptr);
  }
  for (uint32_t mip = 0; mip < decoded.mip_levels && !volume; ++mip) {
    for (uint32_t slice = 0; slice < decoded.depth; ++slice) {
      // Decoder order: mip-major, slice-minor. D3D12 subresource: mip + slice * mips.
      const auto& level = decoded.levels[size_t(mip) * decoded.depth + slice];
      uint32_t sub = mip + slice * decoded.mip_levels;
      uint8_t* dst = upload.cpu + layouts[sub].Offset;
      uint32_t copy_rows = std::min<uint32_t>(rows[sub], level.rows);
      size_t copy_bytes = std::min<size_t>(size_t(row_sizes[sub]), level.row_pitch);
      for (uint32_t r = 0; r < copy_rows; ++r) {
        std::memcpy(dst + size_t(r) * layouts[sub].Footprint.RowPitch,
                    decoded.data.data() + level.offset + size_t(r) * level.row_pitch, copy_bytes);
      }
      D3D12_TEXTURE_COPY_LOCATION src{};
      src.pResource = upload.resource;
      src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      src.PlacedFootprint = layouts[sub];
      src.PlacedFootprint.Offset += upload_base;
      D3D12_TEXTURE_COPY_LOCATION dst_loc{};
      dst_loc.pResource = entry.resource.Get();
      dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      dst_loc.SubresourceIndex = sub;
      command_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &src, nullptr);
    }
  }
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
  Transition(entry.resource.Get(), state,
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = srv_format;
  srv.Shader4ComponentMapping = decoded.component_mapping;
  if (cube) {
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srv.TextureCube.MipLevels = decoded.mip_levels;
  } else if (volume) {
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srv.Texture3D.MipLevels = decoded.mip_levels;
  } else {
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = decoded.mip_levels;
  }
  entry.srv_index = AllocSrvIndex();
  if (!entry.srv_index) {
    Retire(entry.resource);
    entry.resource.Reset();
    return 0;
  }
  device_->CreateShaderResourceView(
      entry.resource.Get(), &srv,
      provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                      entry.srv_index));
  std::copy_n(fetch,6,entry.descriptor.begin());
  entry.guest_base = decoded.base_address;
  entry.guest_size = decoded.base_size;
  entry.guest_format = decoded.guest_format;
  entry.guest_ranges = std::move(ranges);
  entry.guest_hash_bytes = TextureContentBytes(entry.guest_ranges);
  entry.watch_seq = pre_seq;
  entry.content_hash = pre_hash;
  entry.checked_frame = entry.hashed_frame = frame_count_;
  entry.hash_backoff.OnHashed(frame_count_,false);
  if(texture_tracking_mode_==TextureTrackingMode::On && entry.tracking) {
    auto& tracking=*entry.tracking;
    // GPU upload and pre_hash used this exact owned decode. Publish only after
    // all resource/SRV operations succeed; later writes fail the next query.
    resource_write_tracker_.Sample(tracking.watch,tracking.current);
    if(tracking.proof.version && SameCoveredContent(tracking.proof.baseline,tracking.current)) {
      entry.resource_version=tracking.version;
      tracking.upload={tracking.watch,tracking.proof.baseline,tracking.version};
    }
  }
  ++stats_.textures_created;
  return entry.srv_index;
}

static uint64_t SamplerKey(const uint32_t fetch[6], bool force_linear, uint32_t bias_q) {
  return (uint64_t(fetch[0]) & 0x7FFC00) | (uint64_t(fetch[3] & 0x3FF80000) << 32) |
         (uint64_t(force_linear) << 63) | (uint64_t(bias_q) << 1);
}

uint32_t Renderer::GetSamplerIndex(const uint32_t fetch[6], bool force_linear, float lod_bias) {
  CpuTimer cpu_timer(cpu_timings.sampler_ns);
  xenos::xe_gpu_texture_fetch_t f;
  std::memcpy(&f, fetch, sizeof(f));
  uint32_t bias_q = uint32_t(std::clamp(lod_bias, 0.0f, 7.9f) * 16.0f);  // 1/16 steps
  uint64_t key = SamplerKey(fetch, force_linear, bias_q);
  auto it = samplers_.find(key);
  if (it != samplers_.end()) {
    return it->second;
  }
  D3D12_SAMPLER_DESC desc{};
  bool mag_linear = uint32_t(f.mag_filter) == 1 || force_linear;
  bool min_linear = uint32_t(f.min_filter) == 1 || force_linear;
  bool mip_linear = uint32_t(f.mip_filter) == 1;
  uint32_t aniso = uint32_t(f.aniso_filter);
  uint32_t max_aniso = aniso > 1 ? std::min(16u, 1u << (aniso - 1)) : 1u;
  // anisotropic_filtering setting: overrides filtered (linear min/mag)
  // textures; point-sampled lookups stay point-sampled.
  if (anisotropy_ >= 1 && (min_linear || mag_linear || aniso > 1)) {
    max_aniso = uint32_t(std::min(anisotropy_, 16));
  }
  if (max_aniso > 1) {
    desc.Filter = D3D12_FILTER_ANISOTROPIC;
    desc.MaxAnisotropy = max_aniso;
  } else if (aniso > 1) {
    // Game asked for anisotropy, the setting turned it off: trilinear.
    desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    desc.MaxAnisotropy = 1;
  } else {
    desc.Filter = D3D12_ENCODE_BASIC_FILTER(
        min_linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
        mag_linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
        mip_linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
        D3D12_FILTER_REDUCTION_TYPE_STANDARD);
    desc.MaxAnisotropy = 1;
  }
  desc.AddressU = MapClamp(uint32_t(f.clamp_x));
  desc.AddressV = MapClamp(uint32_t(f.clamp_y));
  desc.AddressW = MapClamp(uint32_t(f.clamp_z));
  desc.MaxLOD = D3D12_FLOAT32_MAX;
  desc.MipLODBias = float(bias_q) / 16.0f;
  desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  uint32_t index = sampler_heap_next_ < kSamplerHeapSize ? sampler_heap_next_++ : 0;
  device_->CreateSampler(&desc, provider_->OffsetSamplerDescriptor(
                                    sampler_heap_->GetCPUDescriptorHandleForHeapStart(), index));
  samplers_[key] = index;
  return index;
}

void Renderer::OnGpuSwap(uint64_t swap_number, const uint32_t* regs, uint32_t count) {
  if (!REXCVAR_GET(sr_native_ab_mode) || REXCVAR_GET(sr_native_dump_swap) <= 0 ||
      swap_number != uint64_t(REXCVAR_GET(sr_native_dump_swap))) {
    return;
  }
  std::vector<std::array<uint32_t, 6>> fetches;
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Mirror vs Xenos register file at the same guest swap.
    if (!mirror_snapshot_.empty() && regs) {
      struct Range { const char* name; uint32_t first, last; };
      const Range ranges[] = {{"vs consts", 0x4000, 0x4400}, {"ps consts", 0x4400, 0x4800},
                              {"fetch", 0x4800, 0x4900}, {"bool/loop", 0x4900, 0x4928},
                              {"regs", 0x2000, 0x3000}};
      for (const Range& r : ranges) {
        uint32_t diffs = 0, unwritten = 0;
        std::string first;
        for (uint32_t i = r.first; i < r.last && i < count; ++i) {
          if (mirror_snapshot_[i] == regs[i]) continue;
          ++diffs;
          if (!mirror_.written(i)) ++unwritten;
          if (diffs <= 12) first += fmt::format(" {:04X}:{:08X}/{:08X}", i, mirror_snapshot_[i], regs[i]);
        }
        REXLOG_INFO("native: A/B regs {}: {} differ ({} never written by mirror){}", r.name, diffs,
                    unwritten, first);
      }
    }
    for (auto& [key, rt] : resolve_textures_) {
      std::array<uint32_t, 6> f;
      std::memcpy(f.data(), rt.guest_fetch, sizeof(rt.guest_fetch));
      if (f[1]) fetches.push_back(f);
    }
  }
  std::string dir = REXCVAR_GET(sr_native_dump_dir);
  int written = 0;
  for (auto& f : fetches) {
    DecodedTexture t;
    const char* why = nullptr;
    if (!DecodeTexture(f.data(), t, &why) || t.levels.empty()) {
      REXLOG_INFO("native: xenos dump skip {:08X}: {}", f[1], why ? why : "?");
      continue;
    }
    const auto& l = t.levels[0];
    {
      char raw_name[128];
      std::snprintf(raw_name, sizeof(raw_name), "%s/xenosmem_%08X_gf%u_%ux%u.bin", dir.c_str(),
                    GuestPhysical(f[1] & 0xFFFFF000u), f[1] & 0x3F, t.width, t.height);
      if (std::FILE* file = std::fopen(raw_name, "wb")) {
        std::fwrite(f.data(), 4, 6, file);
        std::fwrite(REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(t.base_address), 1,
                    t.base_size, file);
        std::fclose(file);
      }
    }
    char name[128];
    std::snprintf(name, sizeof(name), "%s/xenos_%08X_gf%u_%ux%u.raw", dir.c_str(),
                  GuestPhysical(f[1] & 0xFFFFF000u), f[1] & 0x3F, t.width, t.height);
    if (std::FILE* file = std::fopen(name, "wb")) {
      uint32_t header[4] = {t.width, t.height, uint32_t(t.format), l.row_pitch};
      std::fwrite(header, 4, 4, file);
      std::fwrite(t.data.data() + l.offset, 1, size_t(l.row_pitch) * l.rows, file);
      std::fclose(file);
      ++written;
    }
  }
  REXLOG_INFO("native: dumped {} Xenos resolve results at GPU swap {}", written, swap_number);
}

void Renderer::Breadcrumb(const char* what) {
  // Debug only: a MARKER_OUT WriteBufferImmediate after every draw makes the GPU
  // drain its pipeline ~600 times per frame.
  if (!REXCVAR_GET(sr_native_gpu_breadcrumbs)) return;
  uint32_t seq = ++crumb_seq_;
  CrumbDesc& d = crumbs_[seq & 4095];
  d.what = what;
  d.prim = crumb_prim_;
  d.count = crumb_count_;
  d.seq = seq;
  d.pass = g_current_pass;
  d.vs = current_vs_hash_;
  d.ps = current_ps_hash_;
  d.frame = frame_count_;
  if (!crumb_buffer_) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 4096;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&crumb_buffer_)))) {
      return;
    }
    void* p = nullptr;
    crumb_buffer_->Map(0, nullptr, &p);
    crumb_cpu_ = static_cast<volatile uint32_t*>(p);
  }
  if (!crumb_list_ && FAILED(command_list_.As(&crumb_list_))) return;
  D3D12_WRITEBUFFERIMMEDIATE_PARAMETER param{crumb_buffer_->GetGPUVirtualAddress(), seq};
  D3D12_WRITEBUFFERIMMEDIATE_MODE mode = D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT;
  crumb_list_->WriteBufferImmediate(1, &param, &mode);
}

void Renderer::ReportHang() {
  if (!crumb_cpu_) return;
  uint32_t done = crumb_cpu_[0];
  REXLOG_ERROR("native: GPU hang diagnostics: last completed draw #{} of {} recorded", done,
               crumb_seq_);
  for (uint32_t k = done; k <= done + 4 && k <= crumb_seq_; ++k) {
    const CrumbDesc& d = crumbs_[k & 4095];
    if (d.seq != k) continue;
    REXLOG_ERROR("native:   {} {} #{} frame {} pass {} prim {} count {} vs {:016X} ps {:016X}",
                 k == done ? "done" : "pending", d.what, k, d.frame, d.pass, d.prim, d.count, d.vs,
                 d.ps);
  }
  rex::FlushLogging();
  for (int i = 0; i < 6; ++i) {
    Sleep(500);
    REXLOG_ERROR("native: hang watch {}: crumb {} fence {} removed {:08X}", i, crumb_cpu_[0],
                 fence_->GetCompletedValue(), uint32_t(device_->GetDeviceRemovedReason()));
  }
}

uint32_t Renderer::GpuConstant(uint8_t* base, uint32_t dev, uint32_t reg,
                               uint32_t shadow_offset, bool use_mirror) const {
  if (use_mirror && mirror_.written(reg)) return mirror_.reg(reg);
  return Load32(base, dev + shadow_offset);
}

void Renderer::NoteRingConstants(bool pixel, uint32_t start, uint32_t count, uint32_t ring_ptr) {
  if (REXCVAR_GET(sr_native_pm4_mirror)) return;  // the mirror parses the packets itself
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (trace_state_ == 1) {
    REXLOG_INFO("trace p{} ring consts {} start {} count {} ptr {:08X}", g_current_pass,
                pixel ? "ps" : "vs", start, count, ring_ptr);
  }
  if (ring_ptr && count && start + count <= 256) {
    pending_ring_constants_.push_back({pixel, start, count, ring_ptr});
  }
}

void Renderer::ApplyLoadAluConstants(uint8_t* base, uint32_t dev, uint32_t table,
                                     uint32_t data) {
  if (REXCVAR_GET(sr_native_pm4_mirror)) return;  // the mirror parses the packet itself
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  // Earlier ring writes precede this load on the GPU.
  FlushRingConstants(base, dev);
  uint32_t rel = Load32(base, table + 20);
  if (!rel || !data) return;
  uint32_t header = table + rel;
  uint32_t p = header + 20;
  uint32_t end = p + Load32(base, header + 16);
  while (p < end) {
    uint32_t reg = Load16(base, p);
    uint32_t dwords = Load16(base, p + 2);
    if (!dwords) break;
    uint32_t offset = Load32(base, p + 4);
    p += 8;
    // VS constants 0-255 and PS constants 256-511 are contiguous in the shadow.
    if (reg * 4 + dwords <= 512 * 4) {
      // The packet carries the physical address (0xE0000000+ views are offset by 4 KB).
      const uint8_t* src =
          REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(GuestPhysical(data + offset));
      std::memcpy(base + dev + kDevVsConstants + reg * 16, src, dwords * 4);
    }
  }
}

void Renderer::FlushRingConstants(uint8_t* base, uint32_t dev) {
  if (!REXCVAR_GET(sr_native_ring_constants) || REXCVAR_GET(sr_native_pm4_mirror)) {
    pending_ring_constants_.clear();
    return;
  }
  // Keep the device's cached constants equal to what the GPU would receive
  // (the XDK contract for GpuBeginShaderConstantF4's cached pointer).
  for (const RingConstants& rc : pending_ring_constants_) {
    uint32_t dst = dev + (rc.pixel ? kDevPsConstants : kDevVsConstants) + rc.start * 16;
    std::memcpy(base + dst, base + rc.ring_ptr, size_t(rc.count) * 16);
  }
  pending_ring_constants_.clear();
}

bool Renderer::UploadConstants(uint8_t* base, uint32_t dev) {
  CpuTimer cpu_timer(cpu_timings.constants_ns);
  CpuTimer float_timer(cpu_timings.constant_float_ns);
  if(CpuProfiling()) ++cpu_timings.constant_draws;
  const bool detail=ConstantDetailProfiling();
  auto& d=constant_detail_timings;
  if(detail) {++d.draws; d.ring_requests+=pending_ring_constants_.size();}
  {
    CpuTimer timer(d.flush_ns,detail);
    FlushRingConstants(base, dev);
  }
  const bool cache_shared = SharedConstantsCaching();
  UploadAlloc shared;
  if (!cache_shared) {
    CpuTimer timer(d.shared_alloc_ns,detail);
    if (!Upload(4096, 256, shared)) {
      if(detail) ++d.allocation_failures;
      return false;
    }
  }
  bool use_mirror = REXCVAR_GET(sr_native_pm4_mirror);
  if(detail && use_mirror) ++d.mirror_draws;
  // Debug: sr_native_debug_ps_const=<ps hash hex>:<register>:<x>,<y>,<z>,<w>
  static const struct Override {
    uint64_t hash = 0;
    uint32_t reg = 0;
    float v[4] = {};
  } ps_override = [] {
    Override o;
    std::string spec = REXCVAR_GET(sr_native_debug_ps_const);
    unsigned long long h = 0;
    if (!spec.empty() &&
        std::sscanf(spec.c_str(), "%llx:%u:%f,%f,%f,%f", &h, &o.reg, &o.v[0], &o.v[1], &o.v[2],
                    &o.v[3]) == 6) {
      o.hash = h;
    }
    return o;
  }();
  bool override_ps = ps_override.hash && ps_override.hash == current_ps_hash_ && ps_override.reg < 256;
  // NaN constants become 0: the game leaves uninitialized lanes (e.g. light
  // c64.w) that Xenos multiplies away (0 * NaN = 0 in its ALU); HLSL keeps the
  // NaN (same as the Xenos-path E041 sanitizer). Source: the PM4 mirror (GPU
  // truth, host order) or the byte-swapped device shadow.
  auto fill = [&](uint32_t mirror_base, uint32_t shadow_offset, uint32_t* dst) {
    const bool pixel=mirror_base!=Pm4Mirror::kAluConstantBase;
    CpuTimer capture_timer(pixel?d.ps_capture_ns:d.vs_capture_ns,detail);
    if(use_mirror) {
      const auto& captured=mirror_.FloatConstants(pixel);
      capture_timer.Stop();
      CpuTimer write_timer(pixel?d.ps_write_ns:d.vs_write_ns,detail);
      std::memcpy(dst,captured.data(),sizeof(captured));
      return;
    }
    const uint32_t* src = use_mirror ? mirror_.regs() + mirror_base
        : reinterpret_cast<const uint32_t*>(base + dev + shadow_offset);
    std::array<uint32_t,1024> captured;
    std::string error;
    graphics::guest::CaptureFloatConstants({src,1024}, !use_mirror, captured, error);
    capture_timer.Stop();
    CpuTimer write_timer(pixel?d.ps_write_ns:d.vs_write_ns,detail);
    std::memcpy(dst,captured.data(),sizeof(captured));
  };
  if (!use_mirror || cb_vs_version_ != mirror_.vs_version || !cb_vs_gpu_) {
    UploadAlloc vs;
    {
      CpuTimer timer(d.vs_alloc_ns,detail);
      if (!Upload(4096, 256, vs)) {
        if(detail) ++d.allocation_failures;
        return false;
      }
    }
    {
      CpuTimer timer(d.vs_copy_ns,detail);
      fill(Pm4Mirror::kAluConstantBase, kDevVsConstants, reinterpret_cast<uint32_t*>(vs.cpu));
    }
    if(detail) {++d.vs_uploads; d.vs_bytes+=4096;}
    cb_vs_gpu_ = vs.gpu;
    cb_vs_version_ = use_mirror ? mirror_.vs_version : 0;
  } else if(detail) ++d.vs_reuses;
  if (!REXCVAR_GET(sr_native_debug_vs).empty() && use_mirror &&
      current_vs_hash_ == std::strtoull(REXCVAR_GET(sr_native_debug_vs).c_str(), nullptr, 16)) {
    static int logged = 0;
    if (logged < 12) {
      ++logged;
      const float* c = reinterpret_cast<const float*>(mirror_.regs() + Pm4Mirror::kAluConstantBase);
      std::string m;
      for (uint32_t r = 0; r < 11; ++r) {
        m += fmt::format(" c{}=({:.3f} {:.3f} {:.3f} {:.3f})", r, c[r * 4], c[r * 4 + 1], c[r * 4 + 2], c[r * 4 + 3]);
      }
      std::string sh;
      for (uint32_t r = 0; r < 11; ++r) {
        float f[4];
        for (uint32_t k = 0; k < 4; ++k) {
          uint32_t w = Load32(base, dev + kDevVsConstants + r * 16 + 4 * k);
          std::memcpy(&f[k], &w, 4);
        }
        sh += fmt::format(" c{}=({:.3f} {:.3f} {:.3f} {:.3f})", r, f[0], f[1], f[2], f[3]);
      }
      REXLOG_INFO("native dbg vs consts v{}:{} | shadow:{}", mirror_.vs_version, m, sh);
    }
  }
  if (camera_candidate_probe_enabled_ &&
      current_vs_hash_ == graphics::CameraCandidateProbe::kViewProjectionShader) {
    // The mirror tracks the actual VS bank in host byte order, including
    // constants submitted by the worker capture path. Copy only c0..c3.
    const auto& words = mirror_.FloatConstants(false);
    std::array<float, 16> matrix{};
    std::memcpy(matrix.data(), words.data(), sizeof(matrix));
    camera_candidate_probe_.Observe(current_vs_hash_, matrix);
  }
  if (!REXCVAR_GET(sr_native_debug_ps).empty() && use_mirror &&
      current_ps_hash_ == std::strtoull(REXCVAR_GET(sr_native_debug_ps).c_str(), nullptr, 16)) {
    static int logged = 0;
    if (logged < 6) {
      ++logged;
      const float* c = reinterpret_cast<const float*>(mirror_.regs() + Pm4Mirror::kAluConstantBase + 1024);
      std::string m;
      for (uint32_t r = 0; r < 41; ++r) {
        m += fmt::format(" c{}=({:.3g} {:.3g} {:.3g} {:.3g})", r, c[r * 4], c[r * 4 + 1], c[r * 4 + 2], c[r * 4 + 3]);
      }
      REXLOG_INFO("native dbg ps consts (vs {:016X}):{}", current_vs_hash_, m);
    }
  }
  if (!use_mirror || override_ps || cb_ps_version_ != mirror_.ps_version || !cb_ps_gpu_) {
    UploadAlloc ps;
    {
      CpuTimer timer(d.ps_alloc_ns,detail);
      if (!Upload(4096, 256, ps)) {
        if(detail) ++d.allocation_failures;
        return false;
      }
    }
    uint32_t* ps_dst = reinterpret_cast<uint32_t*>(ps.cpu);
    {
      CpuTimer timer(d.ps_copy_ns,detail);
      fill(Pm4Mirror::kAluConstantBase + 1024, kDevPsConstants, ps_dst);
      if (override_ps) std::memcpy(ps_dst + ps_override.reg * 4, ps_override.v, 16);
    }
    if(detail) {++d.ps_uploads; d.ps_bytes+=4096;}
    cb_ps_gpu_ = ps.gpu;
    cb_ps_version_ = (use_mirror && !override_ps) ? mirror_.ps_version : 0;
  } else if(detail) ++d.ps_reuses;
  float_timer.Stop();
  CpuTimer setup_timer(d.setup_ns,detail);
  // Assemble all derived state in ordinary CPU memory. Upload only after the
  // final bytes are known; matching PM4 versions alone would miss derived SRVs,
  // scale, alpha testing and vertex metadata.
  SharedConstantBlock shared_data;
  static_assert(512 + sizeof(vertex_fetch_) == sizeof(shared_data));
  uint32_t* s = (cache_shared || stage_shared_constants_) ? shared_data.data() : reinterpret_cast<uint32_t*>(shared.cpu);
  std::memset(s, 0, 512);
  // Also describe IA inputs for inline draws: they don't pass through
  // BindVertexStreams. The shader needs the type to undo USHORT2 UNORM.
  for (auto& meta : vertex_fetch_) meta[3] = 0;
  uint32_t input_decl = Load32(base, dev + kDevVertexDecl);
  const auto& decl_info = ReadVertexDeclarationInfo(base, input_decl);
  for (const auto& attribute : decl_info.attributes) {
    if (attribute.usage < 14 && attribute.index < 16)
      vertex_fetch_[attribute.usage * 16 + attribute.index][3] = attribute.type;
  }
  const uint32_t decl_normal_spec = decl_info.normal_spec_bits;
  std::memcpy(s + 128, vertex_fetch_, sizeof(vertex_fetch_));
  setup_timer.Stop();
  CpuTimer slots_timer(cpu_timings.constant_slots_ns);
  for (uint32_t slot = 0; slot < 16; ++slot) {
    uint32_t fetch[6]{};
    fetch[0] = GpuConstant(base, dev, Pm4Mirror::kFetchConstantBase + 6 * slot,
                           kDevFetchConstants + 24 * slot, use_mirror);
    // Empty slots have no texture or sampler. Their three SRV dimensions use
    // the same null descriptors regardless of the remaining fetch words.
    // Motion capture still needs fetch[1] for the first three slots.
    if (!IsTextureBound(fetch[0]) && !(motion_capture_active_ && slot < 3)) {
      s[16 + slot] = 1;
      s[32 + slot] = 2;
      continue;
    }
    for (uint32_t d = 1; d < 6; ++d) {
      fetch[d] = GpuConstant(base, dev, Pm4Mirror::kFetchConstantBase + 6 * slot + d,
                             kDevFetchConstants + 24 * slot + 4 * d, use_mirror);
    }
    uint32_t srv = 0, sampler = 0;
    if (IsTextureBound(fetch[0])) {
      SlotCache& sc = slot_cache_[slot];
      // Same fetch as the previous draw in this frame: the SRV cannot have
      // changed (resolves invalidate the cache below).
      if (sc.frame == frame_count_ && !std::memcmp(sc.fetch, fetch, sizeof(sc.fetch))) {
        if(CpuProfiling()) ++cpu_timings.slot_hits;
        srv = sc.srv;
      } else {
        if(CpuProfiling()) ++cpu_timings.slot_misses;
        srv = GetTextureSrvIndex(base, fetch);
        // Resolve textures rendered at render_scale / shadow_quality: bits
        // 29-30 carry scale - 1 so the shader keeps texel offsets and filter
        // weights in guest texels (TEX_SCALE in the XenosRecomp patch).
        auto latest = resolve_latest_.find(GuestPhysical(fetch[1] & 0xFFFFF000u));
        if (srv > 2 && latest != resolve_latest_.end()) {
          auto rt = resolve_textures_.find(latest->second);
          // Not for the shadow atlas: its receivers run the whole PCF kernel
          // on the host texel grid (g_ShadowAtlasTexelScale scales the baked
          // tap offsets instead, EXP-041).
          if (rt != resolve_textures_.end() && rt->second.scale != 1.0f &&
              (!rt->second.shadow || REXCVAR_GET(sr_native_shadow_pcf_mode) == 1)) {
            // Bits 16-27: scale in 8.8 fixed point (TEX_SCALE in the
            // XenosRecomp patch); SRV indices use bits 0-14.
            uint32_t fixed = uint32_t(std::lround(double(rt->second.scale) * 256.0));
            srv |= std::min(fixed, 0xFFFu) << 16;
            // Point fetches of an upscaled color target (post-process chains:
            // glow, bloom downsamples) sample linearly: at the guest texel
            // positions the game samples, bilinear averages exactly the host
            // texels covering that guest texel. Point sampling picked one of
            // them and turned small bright details into square/striped
            // patterns (EXP-045). Depth resolves (R32F) stay point-sampled.
            sc.smooth = rt->second.format != DXGI_FORMAT_R32_FLOAT;
          } else {
            sc.smooth = false;
          }
        } else {
          sc.smooth = false;
        }
        std::memcpy(sc.fetch, fetch, sizeof(sc.fetch));
        sc.srv = srv;
        sc.frame = frame_count_;
      }
      float sampler_bias = 0.0f;
      // Smooth effects: small textures of blended draws (particles, glows,
      // light halos) magnify with a cubic B-spline above the original
      // resolution (bit 28, tfetch2D in the XenosRecomp patch).
      // Their mip level is biased by log2(scale) so they keep the texel
      // density - and the soft look - they had at the original resolution:
      // the finest mips of those wispy textures read as streaks when magnified.
      if (smooth_effects_ && bound_scale_ > 1.01f && srv > 2 && !(srv & 0xFFF0000u) &&
          (fetch[2] & 0x1FFF) < 256 && ((fetch[2] >> 13) & 0x1FFF) < 256) {
        uint32_t blend = LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_BLENDCONTROL0);
        if ((blend & 0x1FFF) != 0x0001) {
          srv |= 0x10000000u;
          sampler_bias = std::log2(bound_scale_);
        }
      }
      const uint32_t bias_q = uint32_t(std::clamp(sampler_bias, 0.0f, 7.9f) * 16.0f);
      const uint64_t sampler_key = SamplerKey(fetch, sc.smooth, bias_q);
      if (sc.sampler_valid && sc.sampler_key == sampler_key) {
        sampler = sc.sampler_index;
      } else {
        sampler = GetSamplerIndex(fetch, sc.smooth, sampler_bias);
        sc.sampler_key = sampler_key;
        sc.sampler_index = sampler;
        sc.sampler_valid = true;
      }
      // PWL gamma in the shader (XenosRecomp CONAN_RECOMP tfetch*).
      if (srv > 2 && REXCVAR_GET(sr_native_texture_gamma) == 2 && IsGammaFetch(fetch)) {
        srv |= 0x80000000u;
      }
    }
    if(motion_capture_active_ && slot<3) RecordMotionColorBinding(slot,fetch,srv);
    if (trace_state_ == 1 && (fetch[0] & 3) == uint32_t(xenos::FetchConstantType::kTexture)) {
      uint32_t addr = GuestPhysical(fetch[1] & 0xFFFFF000u);
      auto latest = resolve_latest_.find(addr);
      std::string src = "texture";
      if (latest != resolve_latest_.end()) {
        auto rt = resolve_textures_.find(latest->second);
        if (rt != resolve_textures_.end()) {
          src = fmt::format("resolve {}x{} dxgi {}", rt->second.width, rt->second.height,
                            int(rt->second.srv_format));
        }
      }
      if (src == "texture") {
        for (auto& [k, te] : textures_) {
          if ((te.srv_index | 0x80000000u) == (srv | 0x80000000u) && te.resource) {
            D3D12_RESOURCE_DESC rd = te.resource->GetDesc();
            src = fmt::format("texture {}x{}x{} dxgi {} mips {} guest {:08X}+{:X}", rd.Width, rd.Height,
                              rd.DepthOrArraySize, int(rd.Format), rd.MipLevels, te.guest_base,
                              te.guest_size);
            break;
          }
        }
      }
      REXLOG_INFO("trace   slot {} fetch {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} fmt {} srv {:X} ({})",
                  slot, fetch[0], fetch[1], fetch[2], fetch[3], fetch[4], fetch[5], fetch[1] & 0x3F,
                  srv, src);
    }
    // Each dimension reads its own descriptor heap view; the others get nulls.
    bool resolved_volume=false;
    if(stacked_resolves_ && IsStacked2DFetch(fetch)) {
      auto latest=resolve_latest_.find(GuestPhysical(fetch[1]&0xFFFFF000u));
      if(latest!=resolve_latest_.end()) {
        auto resource=resolve_textures_.find(latest->second);
        resolved_volume=resource!=resolve_textures_.end() && resource->second.resource && resource->second.stacked_volume;
      }
    }
    uint32_t dimension = ResolvedFetchDimension(fetch,resolved_volume);
    if(trace_state_==1 && IsStacked2DFetch(fetch))
      REXLOG_INFO("trace stacked binding slot={} volume={} dimension={} srv={} heap3d={}",slot,resolved_volume,dimension,srv,dimension==2 && srv?srv:1);
    s[slot] = dimension <= 1 ? srv : 0;
    s[16 + slot] = dimension == 2 && srv ? srv : 1;
    s[32 + slot] = dimension == 3 && srv ? srv : 2;
    s[48 + slot] = sampler;
  }
  slots_timer.Stop();
  CpuTimer derived_timer(d.derived_ns,detail);
  for (uint32_t i = 0; i < 4; ++i) {
    const uint32_t vs_bool=GpuConstant(base, dev, Pm4Mirror::kBoolConstantBase + i, kDevVsBools + 4 * i, use_mirror);
    s[64 + i] = vs_bool;
    if(motion_capture_active_) motion_uploaded_vs_bools_[i]=vs_bool;
    s[68 + i] = GpuConstant(base, dev, Pm4Mirror::kBoolConstantBase + 4 + i, kDevPsBools + 4 * i, use_mirror);
  }
  s[72] = 0;
  // g_HalfPixelOffset (c18.yz): Xenos D3D pixel centers (PA_SU_VTX_CNTL
  // pix_center = 0) are emulated like Xenia, by moving the viewport half a
  // pixel right/down (the VS adds g_HalfPixelOffset * w to the position).
  float half_pixel[2] = {0.0f, 0.0f};
  if (REXCVAR_GET(sr_native_half_pixel_offset) &&
      !(LoadReg(base, dev, rex::graphics::XE_GPU_REG_PA_SU_VTX_CNTL) & 1)) {
    float w = 0.0f, h = 0.0f;
    if (ScreenSpaceDraw(base, dev)) {
      ScreenSpaceTargetSize(w, h);
    } else {
      w = float(Load32(base, dev + kDevViewport + 8));
      h = float(Load32(base, dev + kDevViewport + 12));
      if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
        w = float(kOutputWidth);
        h = float(kOutputHeight);
      }
    }
    half_pixel[0] = 1.0f / w;
    half_pixel[1] = -1.0f / h;
  }
  // Preserve the legacy correction and append only the requested scene delta.
  // Screen-space/postprocess, shadows and ordinary frames retain their inputs.
  motion_jitter_draw_active_=false;
  if(motion_capture_active_ && !motion_scene_finished_ && motion_jitter_ready_ && REXCVAR_GET(sr_native_debug_motion_jitter) &&
     bound_rts_[0] && bound_ds_ && bound_rts_[0]->guest_format==12 &&
     bound_rts_[0]->width==kOutputWidth && bound_rts_[0]->height==kOutputHeight &&
     bound_samples_==1 && !ScreenSpaceDraw(base,dev) && REXCVAR_GET(sr_native_shader_dir).empty()) {
    const auto color=bound_rts_[0]->resource->GetDesc(),depth=bound_ds_->resource->GetDesc();
    if(color.Width==depth.Width && color.Height==depth.Height &&
       color.Width==uint32_t(HostPx(kOutputWidth,render_scale_)) && color.Height==uint32_t(HostPx(kOutputHeight,render_scale_))) {
      half_pixel[0]+=motion_frame_jitter_.ndc_x;half_pixel[1]+=motion_frame_jitter_.ndc_y;
      motion_jitter_draw_active_=true;
    }
  }
  std::memcpy(&s[73], &half_pixel[0], 4);
  std::memcpy(&s[74], &half_pixel[1], 4);
  if(post_vertex_active_ && use_mirror) {
    const bool caster=current_vs_hash_==0x57A00D4EB3BF4C31ull || current_vs_hash_==0xC7C0E381A3C9B79Eull;
    const bool receiver=current_vs_hash_==graphics::CameraCandidateProbe::kViewProjectionShader;
    if(caster || receiver) {
      const auto& words=mirror_.FloatConstants(false);
      const uint32_t row=caster ? 4 : 9;
      graphics::ShadowProjection projection;
      std::memcpy(projection.u.data(),words.data()+row*4,16);
      std::memcpy(projection.v.data(),words.data()+(row+1)*4,16);
      std::memcpy(projection.z.data(),words.data()+(row+2)*4,16);
      auto& saved=caster ? post_vertex_caster_projection_ : post_vertex_receiver_projection_;
      auto& valid=caster ? post_vertex_caster_valid_ : post_vertex_receiver_valid_;
      if(graphics::ShadowRayDirection(projection).valid) {
        if(valid && !graphics::ShadowProjectionsMatch(saved,projection)) post_vertex_light_conflict_=true;
        saved=projection; valid=true;
      } else post_vertex_light_conflict_=true;
    }
  }
  if(post_vertex_active_ && use_mirror &&
     current_vs_hash_==graphics::CameraCandidateProbe::kViewProjectionShader &&
     !ScreenSpaceDraw(base,dev)) {
    graphics::Matrix4 projection{};
    const auto& words=mirror_.FloatConstants(false);
    std::memcpy(projection.data(),words.data(),sizeof(projection));
    // Invert the actual final raster clip, including the host pixel-center
    // shift. World varying extraction itself does not need this inverse.
    for(size_t c=0;c<4;++c) {
      projection[c]+=half_pixel[0]*projection[12+c];
      projection[4+c]+=half_pixel[1]*projection[12+c];
    }
    graphics::Matrix4 inverse{};
    const bool valid=graphics::InvertProjection(projection,inverse);
    if(valid && post_vertex_projection_valid_)
      for(size_t i=0;i<16;++i)
        if(std::abs(inverse[i]-post_vertex_inverse_[i])>1e-4f) post_vertex_camera_conflict_=true;
    if(valid) {post_vertex_inverse_=inverse; post_vertex_projection_valid_=true;}
    for(size_t axis=0;axis<3;++axis)
      std::memcpy(&post_vertex_camera_[axis],words.data()+(5+axis)*4+3,sizeof(float));
  }
  // Runtime spec constants (shared c28.x, see the XenosRecomp patch):
  // bit 1 = alpha test (RB_COLORCONTROL), threshold from RB_ALPHA_REF.
  // The translated test is clip(alpha - ref): exact for GREATER/GEQUAL.
  uint32_t color_control = LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_COLORCONTROL);
  uint32_t alpha_func = color_control & 7;
  uint32_t spec = decl_normal_spec;
  float alpha_ref = 0.0f;
  if ((color_control & 0x8) && alpha_func != 7) {
    spec |= 1u << 1;
    uint32_t ref = LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_ALPHA_REF);
    std::memcpy(&alpha_ref, &ref, 4);
    if (alpha_func == 0) alpha_ref = 2.0f;  // NEVER: discard everything
    static bool logged_func[8] = {};
    if (alpha_func != 4 && alpha_func != 6 && alpha_func != 0 && !logged_func[alpha_func]) {
      logged_func[alpha_func] = true;
      REXLOG_WARN("native: alpha test func {} approximated as GEQUAL", alpha_func);
    }
  }
  std::memcpy(&s[75], &alpha_ref, 4);
  // Foliage antialiasing: the alpha test becomes alpha-to-coverage
  // (SPEC_CONSTANT_ALPHA_TO_COVERAGE, mip-corrected, see the PSO side).
  // Soft particles: blended, depth-tested, non-depth-writing draws of the
  // transparent pass fade out near the scene surface behind them.
  if (soft_particles_ && camera_valid_ && scene_depth_key_ && g_current_pass == kPassRenderSorted &&
      bound_rts_[0] && !ScreenSpaceDraw(base, dev)) {
    uint32_t blend = LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_BLENDCONTROL0);
    uint32_t depthctl = LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_DEPTHCONTROL);
    auto rt_it = resolve_textures_.find(scene_depth_key_);
    if ((blend & 0x1FFF) != 0x0001 && (depthctl & 0x2) && !(depthctl & 0x4) &&
        rt_it != resolve_textures_.end()) {
      ResolveTexture& depth = rt_it->second;
      if (!depth.plain_srv) {
        depth.plain_srv = AllocSrvIndex();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = depth.srv_format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(
            depth.resource.Get(), &srv,
            provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                            depth.plain_srv));
      }
      if (depth.plain_srv) {
        float w_column[2] = {inv_view_proj_[11], inv_view_proj_[15]};
        float distance = float(REXCVAR_GET(sr_native_soft_particle_distance));
        float rt_width = float(HostPx(bound_rts_[0]->width, bound_rts_[0]->scale));
        float texel_scale = float(HostPx(depth.width, depth.scale)) / std::max(rt_width, 1.0f);
        std::memcpy(&s[116], w_column, 8);
        std::memcpy(&s[118], &distance, 4);
        std::memcpy(&s[119], &texel_scale, 4);
        s[120] = depth.plain_srv;
        // SRC_ALPHA source blend: fade alpha only; otherwise rgba.
        spec |= ((blend & 0x1F) == 6) ? (1u << 5) : (1u << 4);
      }
    }
  }
  if (AlphaTestToCoverage(base, dev)) {
    spec = (spec & ~(1u << 1)) | (1u << 3);
    ++stats_.a2c_draws[g_current_pass & 31];
  } else if (spec & (1u << 1)) {
    ++stats_.alpha_test_draws[g_current_pass & 31];
  }
  s[112] = spec;
  // g_PixelPosScale (c28.y): host pixel -> guest pixel for the pixel position
  // input (targets rendered at render_scale / shadow_quality).
  {
    float inv_scale = 1.0f / bound_scale_;
    std::memcpy(&s[113], &inv_scale, 4);
    // g_ShadowAtlasTexelScale (c28.z): shadow atlas PCF tap offsets.
    float atlas_scale =
        REXCVAR_GET(sr_native_shadow_pcf_mode) == 1 ? 1.0f : 1.0f / shadow_scale_;
    std::memcpy(&s[114], &atlas_scale, 4);
    // g_ShadowSoftness (c28.w): shadow smoothing option (rotated, widened PCF).
    static const float kSoftness[3] = {0.0f, 1.0f, 1.6f};
    float softness = kSoftness[std::clamp(shadow_smoothing_, 0, 2)];
    std::memcpy(&s[115], &softness, 4);
  }
  for (uint32_t i = 0; i < 16; ++i) {
    s[76 + i] = GpuConstant(base, dev, Pm4Mirror::kLoopConstantBase + i, kDevVsLoops + 4 * i, use_mirror);
    s[92 + i] =
        GpuConstant(base, dev, Pm4Mirror::kLoopConstantBase + 16 + i, kDevPsLoops + 4 * i, use_mirror);
  }
  // g_ScreenXform (c27): screen-space draws (viewport transform disabled in
  // PA_CL_VTE_CNTL, e.g. the upscale pass) emit pixel positions; map them to NDC.
  float xform[4] = {1.0f, 1.0f, 0.0f, 0.0f};
  if (ScreenSpaceDraw(base, dev)) {
    float w = 0, h = 0;
    ScreenSpaceTargetSize(w, h);
    uint32_t win = LoadReg(base, dev, rex::graphics::XE_GPU_REG_PA_SC_WINDOW_OFFSET);
    // 15-bit signed X/Y window offsets.
    float wx = float(int32_t(win << 17) >> 17), wy = float(int32_t((win >> 16) << 17) >> 17);
    xform[0] = 2.0f / w;
    xform[1] = -2.0f / h;
    xform[2] = -1.0f + wx * xform[0];
    xform[3] = 1.0f + wy * xform[1];
  }
  std::memcpy(&s[108], xform, sizeof(xform));
  if(motion_capture_active_) {
    // Preserve the exact CPU operands written above, without reading a mapped
    // write-combined upload heap when shared staging/cache are disabled.
    std::memcpy(motion_clip_fixup_.data(),xform,sizeof(xform));
    std::memcpy(motion_clip_fixup_.data()+4,half_pixel,sizeof(half_pixel));
  }
  if (trace_state_ == 1) {
    const float* v = reinterpret_cast<const float*>(mirror_.regs() + Pm4Mirror::kAluConstantBase);
    std::string vc;
    for (int r = 160; r < 168; ++r)
      vc += fmt::format(" c{}=({} {} {} {})", r, v[r * 4], v[r * 4 + 1], v[r * 4 + 2], v[r * 4 + 3]);
    REXLOG_INFO("trace   vs{}", vc);
    std::string first_bank;
    for (int r = 0; r < 8; ++r)
      first_bank += fmt::format(" c{}=({} {} {} {})", r, v[r * 4], v[r * 4 + 1], v[r * 4 + 2], v[r * 4 + 3]);
    REXLOG_INFO("trace constants vs {:016X}{}",current_vs_hash_,first_bank);
    const float* c = reinterpret_cast<const float*>(mirror_.regs() + Pm4Mirror::kAluConstantBase + 1024);
    REXLOG_INFO("trace   ps c160 ({} {} {} {}) c161 ({} {} {} {})", c[640], c[641], c[642], c[643],
                c[644], c[645], c[646], c[647]);
    REXLOG_INFO("trace constants ps {:016X} c48=({} {} {} {})",current_ps_hash_,c[192],c[193],c[194],c[195]);
  }
  derived_timer.Stop();
  CpuTimer shared_upload_timer(cpu_timings.shared_upload_ns);
  uint64_t shared_gpu = cache_shared ? shared_constants_cache_.Find(shared_data) : shared.gpu;
  if (!cache_shared && stage_shared_constants_)
    std::memcpy(shared.cpu,shared_data.data(),sizeof(shared_data));
  if (cache_shared && shared_gpu) {
    if (CpuProfiling()) ++cpu_timings.shared_hits;
  } else if (cache_shared) {
    if (!Upload(sizeof(shared_data), 256, shared)) {
      if(detail) ++d.allocation_failures;
      return false;
    }
    std::memcpy(shared.cpu, shared_data.data(), sizeof(shared_data));
    shared_gpu = shared.gpu;
    shared_constants_cache_.Commit(shared_data, shared_gpu);
    if (CpuProfiling()) ++cpu_timings.shared_uploads;
  }
  shared_upload_timer.Stop();
  CpuTimer bindings_timer(d.bindings_ns,detail);
  command_list_->SetGraphicsRootConstantBufferView(0, cb_vs_gpu_);
  command_list_->SetGraphicsRootConstantBufferView(1, cb_ps_gpu_);
  command_list_->SetGraphicsRootConstantBufferView(2, shared_gpu);
  return true;
}

bool Renderer::AlphaTestToCoverage(uint8_t* base, uint32_t dev) {
  // foliage_antialiasing: opaque (non-blended) alpha-tested draws into a
  // multisampled target get alpha-to-coverage instead of a hard alpha test:
  // leaves, grass and grilles get antialiased edges from the game's 4x MSAA.
  if (!foliage_aa_ || bound_samples_ <= 1) return false;
  uint32_t color_control = LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_COLORCONTROL);
  uint32_t func = color_control & 7;
  if (!(color_control & 0x8) || (func != 4 && func != 6)) return false;  // GREATER / GEQUAL
  uint32_t blend = LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_BLENDCONTROL0);
  // color and alpha: ONE, ZERO, ADD (no blending)
  return (blend & 0x1FFF) == 0x0001 && (blend & 0x1FFF0000) == 0x00010000;
}

bool Renderer::ScreenSpaceDraw(uint8_t* base, uint32_t dev) {
  // VPORT_X_SCALE_ENA clear: the vertex shader output is in window coordinates.
  return !(LoadReg(base, dev, rex::graphics::XE_GPU_REG_PA_CL_VTE_CNTL) & 1u);
}

void Renderer::ScreenSpaceTargetSize(float& w, float& h) {
  HostSurface* t = bound_rts_[0] ? bound_rts_[0] : bound_ds_;
  w = t ? float(t->width) : float(kOutputWidth);
  h = t ? float(t->height) : float(kOutputHeight);
}

void Renderer::ApplyFixedFunctionState(uint8_t* base, uint32_t dev) {
  CpuTimer cpu_timer(cpu_timings.fixed_state_ns);
  // sub_820F2810 caches D3DVIEWPORT9: four uint32 coordinates, then two floats.
  D3D12_VIEWPORT vp{};
  vp.TopLeftX = float(Load32(base, dev + kDevViewport + 0));
  vp.TopLeftY = float(Load32(base, dev + kDevViewport + 4));
  vp.Width = float(Load32(base, dev + kDevViewport + 8));
  vp.Height = float(Load32(base, dev + kDevViewport + 12));
  vp.MinDepth = LoadF32(base, dev + kDevViewport + 16);
  vp.MaxDepth = LoadF32(base, dev + kDevViewport + 20);
  std::optional<graphics::guest::Extent> screen_space;
  if (ScreenSpaceDraw(base, dev)) {
    float width, height;
    ScreenSpaceTargetSize(width, height);
    screen_space = graphics::guest::Extent{width, height};
  }
  const auto normalized = graphics::guest::NormalizeViewport(
      {vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height, vp.MinDepth, vp.MaxDepth},
      {float(kOutputWidth), float(kOutputHeight)}, screen_space, 1.0f);
  vp = {normalized.x, normalized.y, normalized.width, normalized.height,
        normalized.min_depth, normalized.max_depth};
  if (trace_state_ == 1) {
    auto regf = [&](uint32_t r) {
      uint32_t v = LoadReg(base, dev, r);
      float f;
      std::memcpy(&f, &v, 4);
      return f;
    };
    REXLOG_INFO("trace   vp {} {} {}x{} z {}..{} | vport zscale {} zoffset {} vte {:08X} clip {:08X} "
                "mirror zscale {} zoffset {} stencilrefmask {:08X} (mirror {:08X}) depthctl mirror {:08X} "
                "surface_info {:08X} color_info {:08X} depth_info {:08X} modecontrol {:08X}",
                vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height, vp.MinDepth, vp.MaxDepth,
                regf(rex::graphics::XE_GPU_REG_PA_CL_VPORT_ZSCALE),
                regf(rex::graphics::XE_GPU_REG_PA_CL_VPORT_ZOFFSET),
                LoadReg(base, dev, rex::graphics::XE_GPU_REG_PA_CL_VTE_CNTL),
                LoadReg(base, dev, rex::graphics::XE_GPU_REG_PA_CL_CLIP_CNTL),
                [&] { uint32_t v = mirror_.reg(rex::graphics::XE_GPU_REG_PA_CL_VPORT_ZSCALE); float f; std::memcpy(&f, &v, 4); return f; }(),
                [&] { uint32_t v = mirror_.reg(rex::graphics::XE_GPU_REG_PA_CL_VPORT_ZOFFSET); float f; std::memcpy(&f, &v, 4); return f; }(),
                LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_STENCILREFMASK),
                mirror_.reg(rex::graphics::XE_GPU_REG_RB_STENCILREFMASK),
                mirror_.reg(rex::graphics::XE_GPU_REG_RB_DEPTHCONTROL), mirror_.reg(0x2000),
                mirror_.reg(0x2001), mirror_.reg(0x2002), mirror_.reg(0x2208));
  }
  if (bound_scale_ != 1.0f) {
    float k = bound_scale_;
    vp.TopLeftX *= k;
    vp.TopLeftY *= k;
    vp.Width *= k;
    vp.Height *= k;
  }
  if (!fixed_state_cached_ || std::memcmp(&fixed_viewport_, &vp, sizeof(vp)) != 0) {
    command_list_->RSSetViewports(1, &vp);
    fixed_viewport_ = vp;
  }
  current_motion_raster_.full_viewport=bound_rts_[0] && vp.TopLeftX==0 && vp.TopLeftY==0 &&
      vp.Width==float(HostPx(bound_rts_[0]->width,bound_rts_[0]->scale)) &&
      vp.Height==float(HostPx(bound_rts_[0]->height,bound_rts_[0]->scale)) && vp.MinDepth==0 && vp.MaxDepth==1;
  // Scissor (draw_util::GetScissor): window scissor + window offset, clamped by
  // the screen scissor. Not during predicated tiling: host surfaces are untiled
  // while the XDK sets a per-tile scissor.
  D3D12_RECT scissor{0, 0, 16384, 16384};
  if (REXCVAR_GET(sr_native_scissor) && !tiling_active_) {
    uint32_t tl = mirror_.reg(rex::graphics::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL);
    uint32_t br = mirror_.reg(rex::graphics::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR);
    const auto decoded_scissor = graphics::guest::DecodeScissor(
        {tl, br, mirror_.reg(rex::graphics::XE_GPU_REG_PA_SC_WINDOW_OFFSET),
         mirror_.reg(rex::graphics::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL),
         mirror_.reg(rex::graphics::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR)},
        true, false, bound_scale_);
    scissor = {decoded_scissor.left, decoded_scissor.top,
               decoded_scissor.right, decoded_scissor.bottom};
    if (trace_state_ == 1) {
      REXLOG_INFO("trace   scissor tl {:08X} br {:08X} -> {},{}-{},{}", tl, br, scissor.left,
                  scissor.top, scissor.right, scissor.bottom);
    }
  }
  if (!fixed_state_cached_ || std::memcmp(&fixed_scissor_, &scissor, sizeof(scissor)) != 0) {
    command_list_->RSSetScissorRects(1, &scissor);
    fixed_scissor_ = scissor;
  }
  current_motion_raster_.scissor=scissor;
  uint32_t stencil_ref_mask = LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_STENCILREFMASK);
  const uint32_t stencil_ref = stencil_ref_mask & 0xFF;
  if (!fixed_state_cached_ || fixed_stencil_ref_ != stencil_ref) {
    command_list_->OMSetStencilRef(stencil_ref);
    fixed_stencil_ref_ = stencil_ref;
  }
  float blend_factor[4];
  uint32_t blend_bits[4];
  for (uint32_t i = 0; i < 4; ++i) {
    uint32_t v = LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_BLEND_RED + i);
    std::memcpy(&blend_factor[i], &v, 4);
    blend_bits[i] = v;
  }
  if (!fixed_state_cached_ || std::memcmp(fixed_blend_bits_, blend_bits, sizeof(blend_bits)) != 0) {
    command_list_->OMSetBlendFactor(blend_factor);
    std::memcpy(fixed_blend_bits_, blend_bits, sizeof(blend_bits));
  }
  fixed_state_cached_ = true;
}

bool Renderer::PrepareDraw(uint8_t* base, uint32_t dev, uint32_t prim,
                           D3D12_PRIMITIVE_TOPOLOGY& topology, bool& quads,
                           D3D12_INDEX_BUFFER_STRIP_CUT_VALUE strip_cut) {
  CpuTimer cpu_timer(cpu_timings.prepare_ns);
  {
    const int want = REXCVAR_GET(sr_native_dump_before_draw);
    const int index = draw_calls_in_frame_++;
    // sr_native_dump_vs: dump before the first draw of that vertex shader and
    // at the next draw (= right after it).
    if (REXCVAR_GET(sr_native_watch_blue) && !watch_done_ && frame_open_ &&
        REXCVAR_GET(sr_native_dump_frame_at_s) > 0 &&
        compat::BenchElapsedMs() >= REXCVAR_GET(sr_native_dump_frame_at_s) * 1000.0) {
      if (watch_frame_ >= 0 && int64_t(frame_count_) != watch_frame_) {
        if (watch_last_index_ >= 800) {
          watch_done_ = true;
          REXLOG_INFO("native watch: frame done ({} draws)", watch_last_index_ + 1);
        } else {
          // Not a scene frame (menu, HUD-only, loading): try the next one.
          watch_frame_ = -1;
          watch_prev_count_ = -1;
          watch_prev_sentinel_ = -1;
          for (float& v : watch_prev_probe_) v = -1.0f;
          watch_ops_.clear();
        }
      }
      if (!watch_done_) {
        if (watch_frame_ < 0 && index == 0) watch_frame_ = int64_t(frame_count_);
        if (watch_frame_ >= 0 && int64_t(frame_count_) == watch_frame_) {
          watch_last_index_ = uint32_t(index);
          WatchHdr(uint32_t(index));
        }
      }
    }
    const bool dump_after = dump_vs_after_pending_;
    bool dump_vs = false;
    if (!dump_after && !REXCVAR_GET(sr_native_dump_vs).empty()) {
      const uint64_t vs_want = std::strtoull(REXCVAR_GET(sr_native_dump_vs).c_str(), nullptr, 16);
      // Shader objects are reused: refresh their registration like the draw
      // setup below does before trusting the object -> hash mapping.
      TryRegisterInlineShaders(base, Load32(base, dev + kDevShaderA), Load32(base, dev + kDevShaderB));
      for (uint32_t off : {kDevShaderA, kDevShaderB}) {
        const GuestShaderInfo* info = LookupGuestShader(Load32(base, dev + off));
        if (info && info->is_vertex && info->container_hash == vs_want) dump_vs = true;
      }
    }
    if (((want >= 0 && index == want) || dump_vs || dump_after) && !frame_dump_done_ && frame_open_ &&
        REXCVAR_GET(sr_native_dump_frame_at_s) > 0 &&
        compat::BenchElapsedMs() >= REXCVAR_GET(sr_native_dump_frame_at_s) * 1000.0) {
      // Streams were bound before PrepareDraw. Keep their SRV leases pinned
      // across the debug submission and readback fences: the draw itself will
      // only be recorded after the dump.
      for (auto& retired : retired_srvs_) {
        for (const auto& meta : vertex_fetch_) {
          if (meta[2] && meta[0] == retired.second) {
            retired.first = UINT64_MAX;
            break;
          }
        }
      }
      auto order_srv_leases = [&] {
        std::stable_sort(retired_srvs_.begin(), retired_srvs_.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
      };
      order_srv_leases();
      command_list_->Close();
      ID3D12CommandList* lists[] = {command_list_.Get()};
      queue_->ExecuteCommandLists(1, lists);
      queue_->Signal(fence_.Get(), next_fence_value_++);
      DumpFrameResources(dump_after ? std::string("after_")
                         : dump_vs  ? std::string("before_") : fmt::format("d{}_", index), true);
      for (auto& retired : retired_srvs_)
        if (retired.first == UINT64_MAX) retired.first = next_fence_value_;
      order_srv_leases();
      command_list_->Reset(allocators_[frame_index_].Get(), nullptr);
      command_list_->IASetVertexBuffers(0, 16, guest_vertex_views_);
      bound_vertex_views_mask_ = 0xFFFFu;
      frame_state_bound_ = false;
      fixed_state_cached_ = false;
      bound_rt_count_ = 0;
      bound_ds_ = nullptr;
      std::memset(bound_rts_, 0, sizeof(bound_rts_));
      if (dump_vs) {
        dump_vs_after_pending_ = true;  // the next draw dumps the "after" state
      } else {
        dump_vs_after_pending_ = false;
        frame_dump_done_ = true;
      }
      REXLOG_INFO("native: dumped {} draw {}", dump_after ? "after" : dump_vs ? "before vs" : "before", index);
    }
  }
  static const uint64_t pass_mask = [] {
    std::string list = REXCVAR_GET(sr_native_pass_mask);
    if (list.empty()) return ~0ull;
    uint64_t mask = 0;
    size_t pos = 0;
    while (pos < list.size()) {
      size_t comma = list.find(',', pos);
      if (comma == std::string::npos) comma = list.size();
      mask |= 1ull << std::stoi(list.substr(pos, comma - pos));
      pos = comma + 1;
    }
    return mask;
  }();
  if (!(pass_mask & (1ull << (g_current_pass & 63)))) {
    return false;
  }
  static const std::array<int, 3> skip = [] {
    std::array<int, 3> r{-1, 0, -1};
    std::sscanf(REXCVAR_GET(sr_native_skip_draws).c_str(), "%d:%d-%d", &r[0], &r[1], &r[2]);
    return r;
  }();
  uint32_t draw_index = pass_draw_index_[g_current_pass & 31]++;
  if (g_current_pass == skip[0] && int(draw_index) >= skip[1] && int(draw_index) <= skip[2]) {
    return false;
  }
  uint64_t vs_hash = 0, ps_hash = 0;
  uint32_t vs_obj = Load32(base, dev + kDevShaderA);
  uint32_t ps_obj = Load32(base, dev + kDevShaderB);
  // Lazily register any shader objects whose inline containers were not yet
  // hashed by OnCreateShader (the hook fires before the XDK finalizes the
  // container header; see shader_registry.cpp TryRegisterInlineShaders).
  TryRegisterInlineShaders(base, vs_obj, ps_obj);
  for (uint32_t off : {kDevShaderA, kDevShaderB}) {
    if (const GuestShaderInfo* info = LookupGuestShader(Load32(base, dev + off))) {
      (info->is_vertex ? vs_hash : ps_hash) = info->container_hash;
    }
  }
  uint32_t decl = Load32(base, dev + kDevVertexDecl);
  static const uint64_t skip_ps = [] {
    std::string v = REXCVAR_GET(sr_native_skip_ps);
    return v.empty() ? 0ull : std::stoull(v, nullptr, 16);
  }();
  if (skip_ps && ps_hash == skip_ps) return false;
  if (trace_state_ == 1) {
    uint32_t n = decl ? Load32(base, decl + 0x18) : 0;
    std::string els;
    for (uint32_t i = 0; i < n && i < 16; ++i) {
      uint32_t e = decl + 0x34 + 12 * i;
      uint32_t st = Load16(base, e);
      if (st == 0xFF) break;
      els += fmt::format(" s{}o{}t{:X}u{}.{}", st,
                         Load16(base, e + 2),
                         Load32(base, e + 4), Load8(base, e + 9), Load8(base, e + 10));
    }
    std::string tex;
    for (uint32_t slot = 0; slot < 10; ++slot) {
      uint32_t f0 = Load32(base, dev + kDevFetchConstants + 24 * slot);
      uint32_t f1 = Load32(base, dev + kDevFetchConstants + 24 * slot + 4);
      uint32_t f3 = Load32(base, dev + kDevFetchConstants + 24 * slot + 12);
      if ((f0 & 3) == 2) tex += fmt::format(" t{}={:08X}/{:08X}/{:08X}", slot, f0, f1, f3);
    }
    uint32_t rt0 = Load32(base, dev + kDevRenderTargets);
    tex += fmt::format(" rt0info {:08X}", rt0 ? Load32(base, rt0 + 0x1C) : 0);
    els += tex;
    REXLOG_INFO("trace draw p{} #{} prim {} vs {:016X} ps {:016X} stride0 {} fetch0 {:08X} cc {:08X} blend {:08X} depth {:08X} mask {:X}{}",
                g_current_pass, draw_index, prim, vs_hash, ps_hash, Load8(base, dev + kDev.stream_strides) * 4,
                Load32(base, dev + kDevVertexFetch0),
                LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_COLORCONTROL),
                LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_BLENDCONTROL0),
                LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_DEPTHCONTROL),
                LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_COLOR_MASK), els);
    REXLOG_INFO("trace scissor #{} window {:08X}-{:08X} screen {:08X}-{:08X} offset {:08X}", draw_index,
                mirror_.reg(rex::graphics::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL),
                mirror_.reg(rex::graphics::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR),
                mirror_.reg(rex::graphics::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL),
                mirror_.reg(rex::graphics::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR),
                mirror_.reg(rex::graphics::XE_GPU_REG_PA_SC_WINDOW_OFFSET));
  }
  bool depth_only = !ps_hash && vs_hash && Load32(base, dev + kDevShaderA) == 0;
  if (!vs_hash || (!ps_hash && !depth_only) || !decl) {
    ++stats_.skip_shader;
    stats_.pass_last_skip[g_current_pass & 31] = "shader";
    LogUnsupportedOnce("draw without registered shaders or declaration", vs_hash ^ (ps_hash << 1),
                       "shader object not seen by the CreateShader hooks");
    return false;
  }
  D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  quads = false;
  graphics::guest::Primitive primitive;
  std::string primitive_error;
  if (!graphics::guest::DecodePrimitive(prim, primitive, primitive_error)) {
    ++stats_.skip_prim;
    stats_.pass_last_skip[g_current_pass & 31] = "prim";
    LogUnsupportedOnce("primitive type", prim);
    return false;
  }
  switch (primitive) {
    case graphics::guest::Primitive::kTriangles:
      topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
      break;
    case graphics::guest::Primitive::kTriangleStrip:
      topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
      break;
    case graphics::guest::Primitive::kQuads:
      topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
      quads = true;
      break;
    case graphics::guest::Primitive::kLines:
      topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
      topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
      break;
    case graphics::guest::Primitive::kLineStrip:
      topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
      topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
      break;
    case graphics::guest::Primitive::kPoints:
      topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
      topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
      break;
    case graphics::guest::Primitive::kRectangles:
      // Expanded on the CPU into a triangle list by the caller.
      topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
      break;
  }
  DXGI_FORMAT rtv_formats[4];
  uint32_t rt_count = 0;
  DXGI_FORMAT dsv_format = DXGI_FORMAT_UNKNOWN;
  graphics::SurfaceDrawMetadata draw_metadata{};
  draw_metadata.vs_hash=vs_hash;draw_metadata.ps_hash=ps_hash;
  if (surface_probe_) {
    draw_metadata = {vs_hash, ps_hash, uint32_t(g_current_pass), prim,
                     LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_DEPTHCONTROL),
                     LoadReg(base, dev, rex::graphics::XE_GPU_REG_RB_COLOR_MASK)};
  }
  if (!BindRenderTargets(base, dev, rtv_formats, rt_count, dsv_format, draw_metadata)) {
    ++stats_.skip_rt;
    stats_.pass_last_skip[g_current_pass & 31] = "rt";
    LogUnsupportedOnce("render target bind", 0);
    return false;
  }
  if(motion_pre_hud_pending_ && bound_rts_[0] && bound_rts_[0]->guest_format==0 && bound_rts_[0]->samples==1 &&
     bound_rts_[0]->width==kOutputWidth && bound_rts_[0]->height==kOutputHeight) {
    // Bounded diagnostic checkpoints, before distinct final-color draw families.
    // The first font draw was already too late (HUD bar present); inspect the
    // earlier checkpoints instead of asserting that it is a pre-HUD boundary.
    const std::array<uint64_t,2> pair{vs_hash,ps_hash};
    if(motion_color_pairs_.size()<12 &&
       std::find(motion_color_pairs_.begin(),motion_color_pairs_.end(),pair)==motion_color_pairs_.end()) {
      motion_color_pairs_.push_back(pair);
      QueueMotionReadback(bound_rts_[0]->resource.Get(),bound_rts_[0]->state,
          fmt::format("candidate_color_d{}_vs{:016X}_ps{:016X}",draw_calls_in_frame_,vs_hash,ps_hash));
    }
    if(vs_hash==0xFCB6D23E7B941855ull && ps_hash==0x9841B42F263B95DEull)
      motion_pre_hud_pending_=false;
  }
  // After binding targets: EDRAM reinterpretation inside BindRenderTargets
  // may have changed the root signature and heaps.
  if (!frame_state_bound_) {
    ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), sampler_heap_.Get()};
    command_list_->SetDescriptorHeaps(2, heaps);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    for (uint32_t i = 0; i < 3; ++i) {
      command_list_->SetGraphicsRootDescriptorTable(3 + i,
                                                    srv_heap_->GetGPUDescriptorHandleForHeapStart());
    }
    command_list_->SetGraphicsRootDescriptorTable(
        6, sampler_heap_->GetGPUDescriptorHandleForHeapStart());
    command_list_->SetGraphicsRootDescriptorTable(
        7, srv_heap_->GetGPUDescriptorHandleForHeapStart());
    frame_state_bound_ = true;
  }
  // Primitive restart only matters for strips.
  rt_shadow_draw_active_=ConfigureRayShadowDraw(base,dev,vs_hash,ps_hash);
  if (topology != D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP &&
      topology != D3D_PRIMITIVE_TOPOLOGY_LINESTRIP) {
    strip_cut = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
  }
  ID3D12PipelineState* pso = GetPipeline(base, dev, vs_hash, ps_hash, decl, topology_type,
                                         rtv_formats, rt_count, dsv_format, strip_cut);
  if(!pso && motion_coverage_draw_active_) {
    // Optional diagnostics must never remove an otherwise valid guest draw.
    // The failed variant key stays separate; retry the original PS/RT layout
    // and recompute its ordinary motion eligibility in GetPipeline below.
    motion_coverage_failed_shaders_.insert(ps_hash);
    motion_coverage_draw_active_=false;motion_coverage_draw_id_=0;motion_coverage_shader_=nullptr;
    motion_coverage_bound_=false;
    rtv_formats[1]=DXGI_FORMAT_UNKNOWN;rt_count=1;bound_rt_count_=1;
    const auto original_rtv=bound_rts_[0]->view;
    command_list_->OMSetRenderTargets(1,&original_rtv,FALSE,bound_ds_?&bound_ds_->view:nullptr);
    REXLOG_WARN("native motion coverage PSO unavailable: ps={:016X}; retrying original draw without owner MRT",ps_hash);
    pso=GetPipeline(base,dev,vs_hash,ps_hash,decl,topology_type,rtv_formats,rt_count,dsv_format,strip_cut);
  }
  if(!pso && rt_shadow_draw_active_) {
    rt_shadow_draw_active_=false;
    pso=GetPipeline(base,dev,vs_hash,ps_hash,decl,topology_type,rtv_formats,rt_count,dsv_format,strip_cut);
  }
  if (!pso) {
    ++stats_.skip_pso;
    ++stats_.skip_pso_pairs[{vs_hash, ps_hash, g_current_pass}];
    stats_.pass_last_skip[g_current_pass & 31] = "pso";
    LogUnsupportedOnce("pipeline", vs_hash ^ (ps_hash << 1));
    return false;
  }
  // Constants may create/upload textures (copy commands): record them before
  // the draw, the render targets stay bound.
  if (trace_state_ == 1 && ps_hash == 0x1EFDCC2FE2C551A4ull) {
    static int logged = 0;
    if (logged++ < 3) {
      std::string m;
      for (uint32_t r : {162u, 164u, 165u, 175u, 177u, 178u, 189u, 190u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u, 32u, 33u, 64u, 65u, 66u}) {
        uint32_t a = dev + kDevPsConstants + r * 16;
        m += fmt::format(" c{}=({:.4g},{:.4g},{:.4g},{:.4g})", r, LoadF32(base, a), LoadF32(base, a + 4),
                         LoadF32(base, a + 8), LoadF32(base, a + 12));
      }
      REXLOG_INFO("trace ground ps consts (pre-flush, pending ring {}):{} psloop0 {:08X} psbools {:08X} {:08X}",
                  pending_ring_constants_.size(), m, Load32(base, dev + kDevPsLoops),
                  Load32(base, dev + kDevPsBools), Load32(base, dev + kDevPsBools + 4));
      uint32_t f[6];
      for (uint32_t d = 0; d < 6; ++d) f[d] = Load32(base, dev + kDevFetchConstants + 24 * 12 + 4 * d);
      DecodedTexture t;
      const char* why = nullptr;
      std::string texels;
      if (DecodeTexture(f, t, &why)) {
        // RGBA16F rows: sample 9 points along each row.
        for (uint32_t row = 0; row < t.height && t.format == DXGI_FORMAT_R16G16B16A16_FLOAT; ++row) {
          for (uint32_t x : {0u, 64u, 128u, 256u, 512u, 768u, 1024u, 1536u, 2047u}) {
            const uint16_t* px = reinterpret_cast<const uint16_t*>(
                t.data.data() + t.levels[0].offset + row * t.levels[0].row_pitch + x * 8);
            auto h = [](uint16_t v) {
              uint32_t sign = v >> 15, e = (v >> 10) & 31, m = v & 1023;
              float f = e ? std::ldexp(float(m | 1024), int(e) - 25) : std::ldexp(float(m), -24);
              return sign ? -f : f;
            };
            texels += fmt::format(" r{}x{}=({:.3f},{:.3f},{:.3f},{:.3f})", row, x, h(px[0]), h(px[1]),
                                  h(px[2]), h(px[3]));
          }
        }
      }
      REXLOG_INFO("trace fog table fetch {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}: {}x{} dxgi {} map {:X} data{}",
                  f[0], f[1], f[2], f[3], f[4], f[5], t.width, t.height, int(t.format), t.component_mapping, texels);
    }
  }
  current_ps_hash_ = ps_hash;
  current_vs_hash_ = vs_hash;
  if(motion_capture_active_) for(auto& source:motion_color_sources_) source={};
  if (!UploadConstants(base, dev)) {
    return false;
  }
  if ((ssao_ || soft_particles_) && !camera_valid_ && g_current_pass == kPassRenderOpaque) {
    CaptureCamera();
  }
  ApplyFixedFunctionState(base, dev);
  if(rt_shadow_draw_active_) {
    UploadAlloc rt_constants;
    if(!Upload(sizeof(rt_shadow_constants_),256,rt_constants)) return false;
    std::memcpy(rt_constants.cpu,&rt_shadow_constants_,sizeof(rt_shadow_constants_));
    command_list_->SetGraphicsRootShaderResourceView(8,rt_shadow_scene_->AccelerationStructure()->GetGPUVirtualAddress());
    command_list_->SetGraphicsRootConstantBufferView(9,rt_constants.gpu);
    command_list_->SetGraphicsRootUnorderedAccessView(10,rt_shadow_counter_[frame_index_]->GetGPUVirtualAddress());
  }
  command_list_->SetPipelineState(pso);
  current_raster_pipeline_=pso;
  if(motion_coverage_draw_active_) command_list_->SetGraphicsRoot32BitConstant(11,motion_coverage_draw_id_,0);
  const D3D12_VERTEX_BUFFER_VIEW default_view{
      default_vertex_buffer_->GetGPUVirtualAddress(), 16, 0};
  command_list_->IASetVertexBuffers(16, 1, &default_view);
  command_list_->IASetPrimitiveTopology(topology);
  return true;
}

void Renderer::SwapBufferRange(uint8_t* base, const BufferEntry& entry, uint32_t begin,
                               uint32_t end, uint8_t* data) {
  uint32_t size = end - begin;
  const uint8_t* src = GuestPtr(base, 0xA0000000u + entry.address + begin, size);
  std::memcpy(data, src, size);
  if (entry.index_format & 3) {
    uint32_t mask = IndexByteXor(entry.index_format >> 2);
    for (uint32_t i = 0; i < size; ++i) data[i] = src[i ^ mask];
    const bool index32 = (entry.index_format & 3) == 2;
    const uint32_t width = index32 ? 4 : 2;
    for (uint32_t i = 0; i + width <= size; i += width) {
      uint32_t value = 0;
      std::memcpy(&value, data + i, width);
      value = HostStripIndex(value, index32, entry.reset_index);
      std::memcpy(data + i, &value, width);
    }
    return;
  }
  // Identical element endian conversion for both render backends.
  graphics::guest::SwapVertexElements({data,size},entry.stride,0,
      entry.index_format>>8,ReadVertexDeclaration(base,entry.decl));
}

void Renderer::VertexRange::Resolve() {
  if (resolved) return;
  resolved = true;
  uint32_t isize = index32 ? 4 : 2;
  if (!ib_phys || uint64_t(start_index + index_count) * isize > ib_size) return;
  const uint8_t* idx = REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(ib_phys);
  uint32_t reset = reset_index;
  uint32_t lo = ~0u, hi = 0;
  // SSE4.1: big-endian indices byte-swapped with pshufb; reset indices are
  // excluded from the maximum (they can only be the minimum if every index is
  // a reset, which the lo > hi check below rejects).
  uint32_t i = start_index;
  const uint32_t end_index = start_index + index_count;
  const __m128i ones = _mm_set1_epi32(-1);
  if (!index32 && index_endian == 1 && reset == UINT16_MAX) {
    const __m128i swap = _mm_setr_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
    __m128i vmin = ones, vmax = _mm_setzero_si128();
    for (; i + 8 <= end_index; i += 8) {
      __m128i v = _mm_shuffle_epi8(
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(idx + 2 * size_t(i))), swap);
      vmin = _mm_min_epu16(vmin, v);
      vmax = _mm_max_epu16(vmax, _mm_andnot_si128(_mm_cmpeq_epi16(v, ones), v));
    }
    lo = uint32_t(_mm_cvtsi128_si32(_mm_minpos_epu16(vmin))) & 0xFFFF;
    hi = ~uint32_t(_mm_cvtsi128_si32(_mm_minpos_epu16(_mm_xor_si128(vmax, ones)))) & 0xFFFF;
    if (lo == 0xFFFF) lo = ~0u;
  } else if (index32 && index_endian == 2 && reset == 0xFFFFFFu) {
    const __m128i swap = _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
    __m128i vmin = ones, vmax = _mm_setzero_si128();
    for (; i + 4 <= end_index; i += 4) {
      __m128i v = _mm_shuffle_epi8(
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(idx + 4 * size_t(i))), swap);
      v = _mm_and_si128(v, _mm_set1_epi32(0xFFFFFF));
      v = _mm_or_si128(v, _mm_cmpeq_epi32(v, _mm_set1_epi32(0xFFFFFF)));
      vmin = _mm_min_epu32(vmin, v);
      vmax = _mm_max_epu32(vmax, _mm_andnot_si128(_mm_cmpeq_epi32(v, ones), v));
    }
    vmin = _mm_min_epu32(vmin, _mm_shuffle_epi32(vmin, _MM_SHUFFLE(1, 0, 3, 2)));
    vmin = _mm_min_epu32(vmin, _mm_shuffle_epi32(vmin, _MM_SHUFFLE(2, 3, 0, 1)));
    vmax = _mm_max_epu32(vmax, _mm_shuffle_epi32(vmax, _MM_SHUFFLE(1, 0, 3, 2)));
    vmax = _mm_max_epu32(vmax, _mm_shuffle_epi32(vmax, _MM_SHUFFLE(2, 3, 0, 1)));
    lo = uint32_t(_mm_cvtsi128_si32(vmin));
    hi = uint32_t(_mm_cvtsi128_si32(vmax));
  }
  for (; i < end_index; ++i) {
    uint32_t x = LoadIndex(idx, i, index32, index_endian);
    if (index32) x &= 0xFFFFFFu;
    if (x == reset) continue;
    lo = std::min(lo, x);
    hi = std::max(hi, x);
  }
  if (lo > hi) return;
  int64_t f = int64_t(lo) + base_vertex, e = int64_t(hi) + base_vertex + 1;
  if (f >= 0 && e > f) {
    first = uint32_t(f);
    end = uint32_t(e);
  }
}

// Worker: executes a guest-side buffer plan. Planned bytes were captured with
// the command, so SwapBufferRange reads them instead of live guest memory.
Renderer::BufferEntry* Renderer::ApplyBuffer(
    uint8_t* base, const BufferPlan& plan, uint32_t position_sample_offset,
    graphics::PositionValueEvidence* position_sample) {
  thread_local std::vector<uint8_t> scratch;
  auto it = buffers_.find(plan.key);
  if (it != buffers_.end() && plan.action != 2) {
    BufferEntry& entry = it->second;
    if (plan.action == 1 && plan.end > plan.begin) {
      uint32_t b = plan.begin, e = plan.end;
      UploadAlloc upload;
      if (!Upload(e - b, 16, upload)) return nullptr;
      scratch.resize(e - b);
      SwapBufferRange(base, entry, b, e, scratch.data());
      std::memcpy(upload.cpu, scratch.data(), e - b);
      Transition(entry.resource.Get(), entry.state, D3D12_RESOURCE_STATE_COPY_DEST);
      command_list_->CopyBufferRegion(entry.resource.Get(), b, upload.resource, upload.offset,
                                      e - b);
      Transition(entry.resource.Get(), entry.state,
                 D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
                     D3D12_RESOURCE_STATE_INDEX_BUFFER |
                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      entry.content_version = next_buffer_content_version_++;
      ++stats_.buffer_partial_uploads;
      stats_.buffer_upload_bytes += e - b;
    }
    return &entry;
  }
  // First use (or unknown on this side): the whole buffer.
  uint32_t address = plan.address, size = plan.size;
  if (!size || size > (64u << 20)) {
    return nullptr;
  }
  UploadAlloc upload;
  if (!Upload(size, 16, upload)) {
    return nullptr;
  }
  ++stats_.buffer_uploads;
  stats_.buffer_upload_bytes += size;
  BufferEntry& entry = buffers_[plan.key];
  bool reuse = entry.resource && entry.size == size;
  entry.address = address;
  entry.size = size;
  entry.decl = plan.decl;
  entry.stride = plan.stride;
  entry.index_format = plan.format;
  entry.phase = plan.phase;
  entry.reset_index = plan.reset_index;
  uint32_t index_format = plan.format, stride = plan.stride, phase = plan.phase;
  // Byte-swap in cached CPU memory: the upload heap is write-combined, and
  // reading it back (as an in-place swap does) is extremely slow.
  scratch.resize(size);
  if (index_format & 3) {
    SwapBufferRange(base, entry, 0, size, scratch.data());
  } else {
    // Bytes outside whole vertices (before `phase`, trailing partial vertex)
    // stay unswapped: never read as vertices.
    std::memcpy(scratch.data(), GuestPtr(base, 0xA0000000u + address, size), size);
    uint32_t end = stride && size >= phase ? size - ((size - phase) % stride) : phase;
    if (end > phase) SwapBufferRange(base, entry, phase, end, scratch.data() + phase);
  }
  std::memcpy(upload.cpu, scratch.data(), size);
  if (!reuse) {
    if(buffer_srv_cache_) {
      if(auto lease=buffer_srv_cache_->Erase(entry.resource_generation)) {
        RetireSrvIndex(lease->index);
        if(StreamDetailProfiling()) ++stream_detail_timings.cache_invalidated;
      }
    }
    if (entry.resource) Retire(entry.resource);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = (size + 15) & ~15u;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    entry.state = D3D12_RESOURCE_STATE_COPY_DEST;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, entry.state,
                                                nullptr, IID_PPV_ARGS(&entry.resource)))) {
      buffers_.erase(plan.key);
      return nullptr;
    }
    entry.raw_srv = 0;
    entry.raw_srv_fence = 0;
    // Track generation even without the optional SRV cache so RT diagnostics
    // can distinguish new host allocations from reused guest addresses.
    entry.resource_generation = next_buffer_generation_++;
  } else {
    Transition(entry.resource.Get(), entry.state, D3D12_RESOURCE_STATE_COPY_DEST);
  }
  command_list_->CopyBufferRegion(entry.resource.Get(), 0, upload.resource, upload.offset, size);
  Transition(entry.resource.Get(), entry.state,
             D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  entry.content_version = next_buffer_content_version_++;
  if (position_sample && position_sample_offset != UINT32_MAX) {
    *position_sample = graphics::DecodeHostFloat3(
        std::span<const uint8_t>(scratch.data(), scratch.size()), position_sample_offset);
  }
  return &entry;
}

// Guest side: decides what a draw must upload of a guest buffer (tracking the
// XDK Unlock invalidations) and captures those bytes with the command.
// Guest buffers are normally invalidated by the Unlock hooks, but the game also
// rewrites buffers (the cape's cloth vertices) without any Unlock. The physical
// write watch catches those: a write to a watched page since the last check
// makes the buffer dirty again (and re-arms the watch).
bool Renderer::RefreshTrackedBuffer(TrackedBuffer& t) {
  DetailScope refresh_scope(capture_timings.fb_refresh_ns);
  if (graphics::FrameTimeline::Detail()) ++stream_plan_counters.refresh_calls;
  const bool every_use=t.size<=kHashedBufferMax && SmallBufferHashEveryUse();
  // The same small allocation can be bound thousands of times in one frame.
  // Once its contents were checked, neither hashing nor write-watch scanning
  // can find new data until a physical write advances the global sequence.
  // Explicit Unlock invalidations already set t.dirty separately.
  if (!every_use && t.content.valid && t.content.frame == front_frame_ &&
      (!texture_watch_ || !t.watched ||
       t.checked_seq == write_seq_.load(std::memory_order_acquire))) {
    return t.dirty;
  }
  bool page_written = false;
  if (texture_watch_ && t.watched && !every_use) {
    const uint32_t global = write_seq_.load(std::memory_order_acquire);
    if (global != t.checked_seq) {
      t.checked_seq = global;
      if (TextureWrittenSince(t.address, t.size, t.watch_seq)) {
        // Arm before reading so a later physical write remains observable.
        t.watch_seq = ArmTextureWatch(t.address, t.size);
        page_written = true;
      }
    }
  }
  bool changed = page_written;
  if (t.size <= kHashedBufferMax) {
    // Confirm page-level notifications against the actual allocation. Keep
    // the once-per-frame fallback for writes through unobserved virtual aliases.
    changed = t.content.Refresh(front_frame_, page_written, [&] {
      DetailScope hash_scope(capture_timings.fb_hash_ns);
      if (graphics::FrameTimeline::Detail()) {++stream_plan_counters.refresh_hashes;stream_plan_counters.refresh_hash_bytes+=t.size;}
      return TextureHash()(
          REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(t.address & 0x1FFFFFFF), t.size, 0);
    },every_use);
    if (changed) ++stats_.buffer_hash_dirty;
  }
  if (changed) {
    t.dirty = true;
    t.clean.clear();
    if (page_written) ++stats_.buffer_watch_dirty;
  }
  return t.dirty;
}

Renderer::BufferPlan Renderer::PlanBuffer(uint8_t* base, uint32_t address, uint32_t size,
                                          uint32_t decl, uint32_t stride, uint32_t index_format,
                                          uint32_t phase, uint32_t need_begin, uint32_t need_end,
                                          bool& ok, uint32_t reset_index,
                                          VertexRange* lazy_range, uint32_t lazy_offset,
                                          TrackedBuffer* already_refreshed, uint64_t already_key) {
  BufferPlan plan;
  ok = size && size <= (64u << 20);
  if (!ok) return plan;
  if (already_refreshed) {
    // The stream cache verified all key fields and refreshed the content for
    // this draw. Reuse both checks rather than hashing and probing again.
    plan.key = already_key;
  } else {
    const uint64_t key_parts[]{address,size,decl,stride,index_format,phase,reset_index};
    plan.key = XXH3_64bits(key_parts, sizeof(key_parts));
  }
  plan.address = address;
  plan.size = size;
  plan.decl = decl;
  plan.stride = stride;
  plan.format = index_format;
  plan.phase = phase;
  plan.reset_index = reset_index;
  // Vertex data is swapped per vertex: the swap range must start on a vertex.
  auto align_range = [&](uint32_t& b, uint32_t& e) {
    e = std::min(e, size);
    if ((index_format & 3) == 1 && (index_format >> 2) < 2) {
      b &= ~1u;
      e = (e + 1) & ~1u;
    } else if (index_format & 3) {
      b &= ~3u;
      e = (e + 3) & ~3u;
    } else if (stride) {
      b = b < phase ? phase : b - ((b - phase) % stride);
      uint32_t n = (e > b ? e - b + stride - 1 : 0) / stride;
      e = std::min(b + n * stride, size - ((size - phase) % stride));
    }
    e = std::min(e, size);
  };
  TrackedBuffer* tracked = already_refreshed;
  if (!tracked) {
    auto it = tracked_.find(plan.key);
    if (it != tracked_.end()) tracked = &it->second;
  }
  if (tracked) {
    TrackedBuffer& t = *tracked;
    if (REXCVAR_GET(sr_native_debug_buffers_always_dirty)) {
      t.dirty = true;
      t.clean.clear();
    }
    if (!already_refreshed) RefreshTrackedBuffer(t);
    if (!t.dirty) return plan;
    uint32_t b = need_begin, e = need_end;
    if (lazy_range) {
      // The dirty branch is the only reader of the needed range (the clean and new-buffer
      // branches ignore it), so the index scan is paid only here.
      {
        DetailScope resolve_scope(capture_timings.fs_resolve_ns);
        lazy_range->Resolve();
      }
      if (lazy_range->end != ~0u) {
        const uint64_t lo = uint64_t(lazy_offset) + uint64_t(lazy_range->first) * stride;
        const uint64_t hi = uint64_t(lazy_offset) + uint64_t(lazy_range->end) * stride;
        b = uint32_t(std::min<uint64_t>(lo, size));
        e = uint32_t(std::min<uint64_t>(hi, size));
      }
    }
    align_range(b, e);
    if (e <= b) return plan;
    for (const auto& [cb, ce] : t.clean) {
      if (cb <= b && e <= ce) return plan;
    }
    plan.action = 1;
    plan.begin = b;
    plan.end = e;
    CaptureBytes(base, 0xA0000000u + address + b, e - b);
    t.clean.emplace_back(b, e);
    if (t.clean.size() > 64) {
      // Too fragmented: start over (later draws upload their ranges again).
      t.clean.clear();
      t.dirty = true;
    }
    return plan;
  }
  TrackedBuffer& t = tracked_[plan.key];
  t.address = address;
  t.size = size;
  if (texture_watch_ && !(size<=kHashedBufferMax && SmallBufferHashEveryUse())) {
    t.watch_seq = ArmTextureWatch(address, size);
    t.checked_seq = write_seq_.load(std::memory_order_acquire);
    t.watched = true;
  }
  if (size <= kHashedBufferMax) {
    t.content.Refresh(front_frame_, false, [&] {
      return TextureHash()(
          REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(address & 0x1FFFFFFF), size, 0);
    });
  }
  for (uint32_t page = (address & 0x1FFFFFFF) >> 16;
       page <= ((address & 0x1FFFFFFF) + size - 1) >> 16; ++page) {
    buffer_pages_[page].push_back(&t);
  }
  plan.action = 2;
  plan.begin = 0;
  plan.end = size;
  CaptureBytes(base, 0xA0000000u + address, size);
  return plan;
}

void Renderer::InvalidateGuestRange(uint32_t address, uint32_t size) {
  std::lock_guard<std::mutex> lock(front_mutex_);
  if (!size) {
    return;
  }
  // Physical addresses of buffers are compared on the low 29 bits.
  uint32_t a = address & 0x1FFFFFFF;
  uint64_t stamp = ++invalidation_stamp_;
  for (uint32_t page = a >> 16; page <= (a + size - 1) >> 16; ++page) {
    auto pit = buffer_pages_.find(page);
    if (pit == buffer_pages_.end()) continue;
    for (TrackedBuffer* tp : pit->second) {
      TrackedBuffer& t = *tp;
      if (t.invalidation_stamp == stamp) continue;  // seen on an earlier page
      t.invalidation_stamp = stamp;
      uint32_t b = t.address & 0x1FFFFFFF;
      if (t.dirty && t.clean.empty()) continue;  // already fully dirty
      if (b < a + size && a < b + t.size) {
        front_invalidations_.fetch_add(1, std::memory_order_relaxed);
        front_invalidated_bytes_.fetch_add(t.size, std::memory_order_relaxed);
        t.dirty = true;
        t.clean.clear();
      }
    }
  }
}

bool Renderer::DynamicVertexFetch(uint8_t* base, uint32_t dev) {
  for (uint32_t off : {kDevShaderA, kDevShaderB}) {
    const GuestShaderInfo* info = LookupGuestShader(Load32(base, dev + off));
    if (info && info->is_vertex && info->dynamic_vertex_fetch) return true;
  }
  return false;
}

// Guest side: the vertex streams of the current draw (guest state at draw time)
// and their buffer plans.
bool Renderer::PlanStreams(uint8_t* base, uint32_t dev, uint32_t decl, VertexRange* range) {
  DetailScope scope(capture_timings.fe_streams_ns);
  if (!decl) return false;
  DetailScope prep_scope(capture_timings.fs_prep_ns);
  if (DynamicVertexFetch(base, dev)) range = nullptr;
  const uint32_t streams_used = ReadVertexDeclarationInfo(base, decl).streams_used & 0xFFFFu;
  prep_scope.Stop();
  const bool allow_cache = !REXCVAR_GET(sr_native_debug_buffers_always_dirty);
  const uint32_t dbg_vb_addr = uint32_t(REXCVAR_GET(sr_native_debug_vb_addr));
  const bool has_dbg_vs = !REXCVAR_GET(sr_native_debug_vs).empty();
  for (uint32_t mask = streams_used; mask; mask &= mask - 1) {
    const uint32_t s = uint32_t(std::countr_zero(mask));
    // Vertex fetch constant for stream s (XDK: slot 95 - s).
    uint32_t fetch0 = Load32(base, dev + kDevVertexFetch0 - 8 * s);
    uint32_t fetch1 = Load32(base, dev + kDevVertexFetch0 - 8 * s + 4);
    uint32_t address = fetch0 & ~3u;
    uint32_t size = ((fetch1 >> 2) & 0xFFFFFF) * 4;
    uint32_t stride = Load8(base, dev + kDev.stream_strides + s) * 4;
    if (!address || !size || !stride) {
      return false;
    }
    // Cache the whole guest vertex buffer (the stream offset of SetStreamSource
    // varies per draw into large shared buffers) and bind at an offset.
    uint32_t buffer_base = address, buffer_size = size;
    if (uint32_t vb_object = Load32(base, dev + kDev.stream_buffers + 4 * s)) {
      uint32_t v = Load32(base, vb_object + 0x18) & ~3u;
      uint32_t phys = (v & 0x1FFFFFFFu) + (v >= 0xE0000000u ? 0x1000u : 0u);
      uint32_t full = ((Load32(base, vb_object + 0x1C) >> 2) & 0xFFFFFF) * 4;
      if (phys <= address && address + size <= phys + full) {
        buffer_base = phys;
        buffer_size = full;
      }
    }
    uint32_t offset = address - buffer_base;
    uint32_t phase = offset % stride;
    StreamPlan sp;
    sp.stream = s;
    sp.offset = offset;
    sp.size = size;
    sp.stride = stride;
    // The base address identifies the guest allocation; its range and layout
    // are checked below. Mix aligned address bits before selecting a set.
    uint32_t address_hash = buffer_base ^ (buffer_base >> 12) ^ (buffer_base >> 23);
    const size_t set = ((address_hash * 0x9E3779B1u) >> 22) & (kFrontStreamSetCount - 1);
    const size_t first = set * 2;
    auto& cache = front_stream_cache_[s];
    const auto matches = [&](size_t slot) {
      const FrontStreamCache& candidate = cache[slot];
      return candidate.tracked && candidate.address == buffer_base && candidate.size == buffer_size &&
             candidate.decl == decl && candidate.stride == stride && candidate.phase == phase;
    };
    const bool first_hit = allow_cache && matches(first);
    const bool second_hit = !first_hit && allow_cache && matches(first + 1);
    const bool cached_fields_match = first_hit || second_hit;
    if (second_hit) {
      if (graphics::FrameTimeline::Detail()) ++stream_plan_counters.secondary_hits;
    }
    const size_t selected_slot = first + (first_hit ? 0 : second_hit ? 1 : front_stream_cache_next_[s][set]);
    FrontStreamCache& sc = cache[selected_slot];
    if (graphics::FrameTimeline::Detail()) ++stream_plan_counters.evaluated;
    const bool cached_dirty = cached_fields_match && RefreshTrackedBuffer(*sc.tracked);
    if (cached_fields_match && !cached_dirty) {
      if (graphics::FrameTimeline::Detail()) ++stream_plan_counters.fast;
      // Clean and cached: no lookup, no range needed.
      sp.buffer.key = sc.key;
      sp.buffer.address = buffer_base;
      sp.buffer.size = buffer_size;
      sp.buffer.decl = decl;
      sp.buffer.stride = stride;
      sp.buffer.format = s << 8;
      sp.buffer.phase = phase;
    } else {
      DetailScope plan_scope(capture_timings.fs_plan_ns);
      if (graphics::FrameTimeline::Detail()) {
        auto& c = stream_plan_counters;
        ++c.slow;
        if (!sc.tracked) ++c.untracked;
        else if (sc.address != buffer_base) ++c.address;
        else if (sc.size != buffer_size) ++c.size;
        else if (sc.decl != decl) ++c.decl;
        else if (sc.stride != stride) ++c.stride;
        else if (sc.phase != phase) ++c.phase;
        else ++c.dirty;  // every cached field matched: RefreshTrackedBuffer reported a dirty buffer
      }
      uint32_t need_begin = 0, need_end = ~0u;
      // SR_NATIVE_LAZY_RESOLVE=0 restores the eager range scan (same-binary A/B).
      static const bool lazy_resolve = [] {
        const char* value = std::getenv("SR_NATIVE_LAZY_RESOLVE");
        return !(value && *value == '0');
      }();
      if (range && !lazy_resolve) {
        {DetailScope resolve_scope(capture_timings.fs_resolve_ns);range->Resolve();}
        if (range->end != ~0u) {
          uint64_t b = uint64_t(offset) + uint64_t(range->first) * stride;
          uint64_t e = uint64_t(offset) + uint64_t(range->end) * stride;
          need_begin = uint32_t(std::min<uint64_t>(b, buffer_size));
          need_end = uint32_t(std::min<uint64_t>(e, buffer_size));
        }
      }
      bool ok = false;
      {
        DetailScope buffer_scope(capture_timings.fs_buffer_ns);
        sp.buffer = PlanBuffer(base, buffer_base, buffer_size, decl, stride, s << 8, phase,
                               need_begin, need_end, ok, UINT32_MAX,
                               lazy_resolve ? range : nullptr, offset,
                               cached_fields_match ? sc.tracked : nullptr, sc.key);
      }
      if (!ok) return false;
      if (!cached_fields_match) front_stream_cache_next_[s][set] ^= 1;
      auto it = tracked_.find(sp.buffer.key);
      sc.address = buffer_base;
      sc.size = buffer_size;
      sc.decl = decl;
      sc.stride = stride;
      sc.format = s << 8;
      sc.phase = phase;
      sc.key = sp.buffer.key;
      sc.tracked = it != tracked_.end() ? &it->second : nullptr;
    }
    if (dbg_vb_addr && address == dbg_vb_addr) {
      static int logged = 0;
      if (logged++ < 40) {
        std::string v;
        for (uint32_t k = 0; k < 12 && k * 4 < buffer_size; ++k)
          v += fmt::format(" {:.4f}", LoadF32(base, 0xA0000000u + buffer_base + offset + 4 * k));
        REXLOG_INFO("native dbg vb {:08X}+{} size {} stride {} plan action {} dirty {}:{}",
                    buffer_base, offset, buffer_size, stride, int(sp.buffer.action),
                    sc.tracked ? sc.tracked->dirty : -1, v);
      }
    }
    batch_->streams.push_back(sp);
    if (has_dbg_vs) {
      uint64_t want = std::strtoull(REXCVAR_GET(sr_native_debug_vs).c_str(), nullptr, 16);
      const GuestShaderInfo* a = LookupGuestShader(Load32(base, dev + kDevShaderA));
      const GuestShaderInfo* b = LookupGuestShader(Load32(base, dev + kDevShaderB));
      bool match = (a && a->is_vertex && a->container_hash == want) ||
                   (b && b->is_vertex && b->container_hash == want);
      static int logged = 0;
      if (match && logged < 90) {
        ++logged;
        uint32_t first = 0, last = 0;
        if (range) range->Resolve();  // debug-only reader: lazy mode may not have resolved it
        if (range && range->end != ~0u) {
          first = range->first;
          last = range->end;
        }
        auto vtx = [&](uint32_t index) {
          std::string v;
          uint32_t at = 0xA0000000u + address + index * stride;
          for (uint32_t k = 0; k < stride / 4 && k < 6; ++k) v += fmt::format(" {:.3f}", LoadF32(base, at + 4 * k));
          return v;
        };
        REXLOG_INFO("native dbg vs s{} addr {:08X} size {} stride {} buf {:08X}+{}/{} range [{},{}) "
                    "action {} | v[first]{} | v[last-1]{}",
                    s, address, size, stride, buffer_base, offset, buffer_size, first, last,
                    int(sp.buffer.action), (size >= stride) ? vtx(first) : "", (last > first && (last - 1) * stride < size) ? vtx(last - 1) : "");
      }
    }
  }
  return true;
}

// Worker: uploads (as planned) and binds the draw's vertex streams. Runs for
// every planned draw, even one skipped later, so host buffers stay in sync
// with the guest-side tracking.
bool Renderer::BindVertexStreams(uint8_t* base, const WorkBatch& batch, const WorkCmd& cmd) {
  CpuTimer cpu_timer(cpu_timings.streams_ns);
  const bool detail=StreamDetailProfiling();
  auto& timings=stream_detail_timings;
  CpuTimer setup_timer(timings.setup_ns,detail);
  if(detail) ++timings.calls;
  bool ok = cmd.streams_ok;
  position_draw_value_ = {};
  std::optional<graphics::guest::VertexAttribute> sampled_position;
  if (surface_probe_ && cmd.op == Op::kDraw && cmd.stream_count) {
    const auto& first_stream = batch.streams[cmd.stream_first];
    for (const auto& attribute : ReadVertexDeclaration(base, first_stream.buffer.decl)) {
      if (attribute.usage == 0 && attribute.index == 0) {
        if (attribute.type == 0x2A23B9) sampled_position = attribute;
        break;
      }
    }
  }
  const uint32_t dev = GuestDevice(base);
  const bool dynamic_fetch = dev && DynamicVertexFetch(base, dev);
  if (dynamic_fetch) {
    for (auto& meta : vertex_fetch_) {
      meta[0] = 0;
      meta[1] = 0;
      meta[2] = 0;
    }
  }
  // All streams in a draw come from PlanStreams with the same declaration.
  // Decode it once rather than repeating the lookup per bound stream.
  // ApplyBuffer may decode a different declaration while converting guest
  // endian layouts, so retain a local snapshot for all streams of this draw.
  // Entries are populated by copy_n before use. Avoid zero-initializing up
  // to 64 attributes for every draw, including draws without dynamic fetch.
  std::array<graphics::guest::VertexAttribute, 64> declaration;
  size_t declaration_count = 0;
  if (dynamic_fetch && cmd.stream_count) {
    CpuTimer declaration_timer(timings.declaration_ns, detail);
    const auto& decoded = ReadVertexDeclaration(base, batch.streams[cmd.stream_first].buffer.decl);
    declaration_count = std::min(decoded.size(), declaration.size());
    std::copy_n(decoded.begin(), declaration_count, declaration.begin());
    if (detail) { ++timings.declaration_reads; timings.attributes += declaration_count; }
  }
  setup_timer.Stop();
  for (uint32_t i = 0; i < cmd.stream_count; ++i) {
    const StreamPlan& sp = batch.streams[cmd.stream_first + i];
    if(detail) {++timings.streams;++timings.actions[sp.buffer.action<3?sp.buffer.action:3];}
    BufferEntry* vb;
    {
      CpuTimer timer(timings.apply_ns,detail);
      uint32_t sample_offset = UINT32_MAX;
      if (sampled_position && sampled_position->stream == sp.stream)
        sample_offset = graphics::HostPositionSampleOffset(
            sp.offset, sp.size, sp.buffer.size, sp.stride, cmd.u[1],
            sampled_position->offset, 12);
      vb=ApplyBuffer(base, sp.buffer, sample_offset,
                     sample_offset != UINT32_MAX ? &position_draw_value_ : nullptr);
      if (sample_offset != UINT32_MAX &&
          position_draw_value_.status != graphics::PositionValueStatus::kNotSampled)
        position_draw_value_.vertex_index = cmd.u[1];
    }
    if (!vb) {
      if(detail) ++timings.missing;
      static int logged = 0;
      if (logged++ < 20) {
        REXLOG_WARN("native: vertex stream {} of draw {} has no host buffer (addr {:08X} size {} stride {} "
                    "action {} offset {})", sp.stream, draw_calls_in_frame_, sp.buffer.address,
                    sp.buffer.size, sp.buffer.stride, int(sp.buffer.action), sp.offset);
      }
      ok = false;
      continue;
    }
    {
    CpuTimer timer(timings.ia_ns,detail);
    D3D12_VERTEX_BUFFER_VIEW view{vb->resource->GetGPUVirtualAddress() + sp.offset, sp.size,
                                  sp.stride};
    const uint16_t stream_bit = sp.stream < 16 ? uint16_t(1u << sp.stream) : 0u;
    if (sp.stream >= 16 || !(bound_vertex_views_mask_ & stream_bit) ||
        guest_vertex_views_[sp.stream].BufferLocation != view.BufferLocation ||
        guest_vertex_views_[sp.stream].SizeInBytes != view.SizeInBytes ||
        guest_vertex_views_[sp.stream].StrideInBytes != view.StrideInBytes) {
      command_list_->IASetVertexBuffers(sp.stream, 1, &view);
      if (sp.stream < 16) {
        guest_vertex_views_[sp.stream] = view;
        bound_vertex_views_mask_ |= stream_bit;
      }
      if(detail) ++timings.ia_binds;
    }
    }
    if (!dynamic_fetch) continue;
    // Static IA draws need no descriptor: allocate only for shader-driven
    // instancing, otherwise transient vertex buffers would fill the SRV heap.
    CpuTimer srv_timer(timings.srv_ns,detail);
    if(detail) ++timings.dynamic_streams;
    BufferEntry& entry = *vb;
    bool retained_hit=false;
    if(buffer_srv_cache_) {
      // The entry field may still name an evicted index. Only the lease cache
      // owns it across submissions, keyed by the actual host resource lifetime.
      entry.raw_srv=buffer_srv_cache_->Find(entry.resource_generation);
      retained_hit=entry.raw_srv && entry.raw_srv_fence!=next_fence_value_;
    }
    if (!entry.raw_srv || (!buffer_srv_cache_ && entry.raw_srv_fence != next_fence_value_)) {
      entry.raw_srv = AllocSrvIndex();
      if (!entry.raw_srv) { if(detail) ++timings.srv_failed; ok = false; continue; }
      D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
      srv.Format = DXGI_FORMAT_R32_TYPELESS;
      srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv.Buffer.NumElements = UINT(entry.resource->GetDesc().Width / 4);
      srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
      device_->CreateShaderResourceView(entry.resource.Get(), &srv,
          provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(), entry.raw_srv));
      if(buffer_srv_cache_) {
        if(auto lease=buffer_srv_cache_->Insert(entry.resource_generation,entry.raw_srv)) {
          RetireSrvIndex(lease->index);
          if(detail) ++timings.cache_evicted;
        }
      } else {
        // Default: retain only for this submission. The opt-in cache above
        // bounds residency instead of keeping one view per transient buffer.
        retired_srvs_.emplace_back(next_fence_value_, entry.raw_srv);
      }
      if(detail) ++timings.srv_created;
    } else if(detail) {
      ++timings.srv_reused;
      if(retained_hit) ++timings.srv_retained_hits;
    }
    entry.raw_srv_fence=next_fence_value_;
    srv_timer.Stop();
    CpuTimer metadata_timer(timings.metadata_ns,detail);
    for (size_t attribute_index = 0; attribute_index < declaration_count; ++attribute_index) {
      const auto& attribute = declaration[attribute_index];
      if (attribute.stream != sp.stream) continue;
      const uint32_t usage = attribute.usage, index = attribute.index;
      if (usage >= 14 || index >= 16) continue;
      auto& meta = vertex_fetch_[usage * 16 + index];
      meta[0] = vb->raw_srv;
      meta[1] = sp.offset + attribute.offset;
      meta[2] = vb->raw_srv ? sp.stride : 0;
      meta[3] = attribute.type;
      if(detail) ++timings.metadata_writes;
    }
  }
  if (!ok) {
    if(detail) ++timings.failed_calls;
    ++stats_.skip_vb;
    LogUnsupportedOnce("vertex stream", 0);
  }
  return ok;
}

static graphics::GeometryGpuTopology GeometryProbeTopology(D3D12_PRIMITIVE_TOPOLOGY topology) {
  switch (topology) {
    case D3D_PRIMITIVE_TOPOLOGY_POINTLIST: return graphics::GeometryGpuTopology::kPointList;
    case D3D_PRIMITIVE_TOPOLOGY_LINELIST: return graphics::GeometryGpuTopology::kLineList;
    case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP: return graphics::GeometryGpuTopology::kLineStrip;
    case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST: return graphics::GeometryGpuTopology::kTriangleList;
    case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP: return graphics::GeometryGpuTopology::kTriangleStrip;
    default: return graphics::GeometryGpuTopology::kUnknown;
  }
}

void Renderer::CapturePostVertexDraw(uint8_t* base,uint32_t dev,
    D3D12_PRIMITIVE_TOPOLOGY topology,bool indexed,uint32_t count,uint32_t first,
    int32_t base_vertex,D3D12_INDEX_BUFFER_STRIP_CUT_VALUE cut) {
  const uint32_t world_texcoord=graphics::WorldPositionTexcoord(current_vs_hash_);
  const bool caster=graphics::IsOriginalShadowCaster(current_vs_hash_) && bound_rts_[0] &&
      bound_rts_[0]->guest_format==4 && bound_rts_[0]->width==bound_rts_[0]->height;
  if((!post_vertex_diagnostic_ && !caster) || !post_vertex_active_ || (!caster && world_texcoord==UINT32_MAX) || SkipGpuDraw() ||
     !post_vertex_capture_ || !current_raster_pipeline_ || (!caster && !bound_ds_) ||
     (topology!=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST &&
      topology!=D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP)) return;
  const uint32_t vertices=graphics::PostVertexCapacity(count,
      topology==D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  if(!vertices) return;
  const uint32_t decl=Load32(base,dev+kDevVertexDecl);
  // The same declaration interpretation and default float inputs as raster.
  const auto& attributes=ReadVertexDeclaration(base,decl);
  const uint64_t key_parts[]{current_vs_hash_,uint64_t(cut),
      XXH3_64bits(attributes.data(),attributes.size()*sizeof(attributes[0]))};
  const uint64_t key=XXH3_64bits(key_parts,sizeof(key_parts));
  auto found=post_vertex_pipelines_.find(key);
  if(found==post_vertex_pipelines_.end()) {
    const auto* vs=LoadShader(current_vs_hash_,true);
    if(!vs) return;
    const auto inputs=VertexShaderInputs(*vs);
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    for(const auto& a:attributes) {
      const auto f=MapDeclType(a.type);
      if(f.format==DXGI_FORMAT_UNKNOWN) continue;
      if(!inputs.empty() && std::find(inputs.begin(),inputs.end(),
          std::make_pair(std::string(UsageSemantic(a.usage)),a.index))==inputs.end()) continue;
      D3D12_INPUT_ELEMENT_DESC e{};
      e.SemanticName=UsageSemantic(a.usage); e.SemanticIndex=a.index;
      e.Format=f.format; e.InputSlot=a.stream; e.AlignedByteOffset=a.offset;
      e.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
      elements.push_back(e);
    }
    for(const auto& input:VertexShaderInputs(*vs,true)) {
      if(std::any_of(elements.begin(),elements.end(),[&](const auto& e){
          return input.first==e.SemanticName && input.second==e.SemanticIndex;})) continue;
      uint32_t usage=0;
      while(usage<14 && input.first!=UsageSemantic(usage)) ++usage;
      if(usage==14) continue;
      D3D12_INPUT_ELEMENT_DESC e{};
      e.SemanticName=UsageSemantic(usage); e.SemanticIndex=input.second;
      e.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; e.InputSlot=16;
      e.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
      elements.push_back(e);
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature=root_signature_.Get();
    desc.VS={vs->data(),vs->size()};
    if(!VertexShaderHasFloatOutput(*vs,"TEXCOORD",caster ? 0 : world_texcoord,caster ? 1 : 7)) {
      REXLOG_ERROR("native: post-VS contract rejected: VS {:016X} has no float XYZ TEXCOORD{} output",
                   current_vs_hash_,world_texcoord);
      post_vertex_pipelines_.emplace(key,ComPtr<ID3D12PipelineState>{});
      return;
    }
    desc.GS=graphics::d3d12::PostVertexCapture::GeometryShader(caster ? 0 : world_texcoord);
    const D3D12_SO_DECLARATION_ENTRY so{0,"TEXCOORD",15,0,3,0};
    const uint32_t stride=12;
    desc.StreamOutput={&so,1,&stride,1,D3D12_SO_NO_RASTERIZED_STREAM};
    desc.InputLayout={elements.data(),UINT(elements.size())};
    desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    desc.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    desc.DepthStencilState.StencilReadMask=desc.DepthStencilState.StencilWriteMask=0xff;
    desc.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,
                                     D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    desc.DepthStencilState.BackFace=desc.DepthStencilState.FrontFace;
    desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.IBStripCutValue=cut; desc.SampleDesc.Count=1; desc.SampleMask=UINT_MAX;
    ComPtr<ID3D12PipelineState> pso;
    const HRESULT hr=device_->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&pso));
    if(FAILED(hr)) REXLOG_ERROR("native: post-VS pipeline vs={:016X} failed {:08X}",current_vs_hash_,uint32_t(hr));
    found=post_vertex_pipelines_.emplace(key,std::move(pso)).first;
  }
  if(!found->second) return;
  const auto range=post_vertex_capture_->Reserve(vertices,current_vs_hash_);
  if(!range) return;
  command_list_->SetPipelineState(found->second.Get());
  command_list_->SOSetTargets(0,1,&range->view);
  if(indexed) command_list_->DrawIndexedInstanced(count,1,first,base_vertex,0);
  else command_list_->DrawInstanced(count,1,first,0);
  command_list_->SOSetTargets(0,0,nullptr);
  command_list_->SetPipelineState(current_raster_pipeline_);
}

#include "native_motion_capture.inc"

void Renderer::RecordGeometrySubmission(uint8_t* base, const WorkBatch* batch, const WorkCmd* cmd,
                                        graphics::GeometrySubmission submission,
                                        uint32_t surface_draw_before) {
  if (!surface_probe_) return;
  submission.surface_draw = graphics::CorrelateSurfaceDraw(
      surface_draw_before, surface_sequence_.total_draws());
  submission.guest_pass = uint32_t(g_current_pass);
  submission.vs_hash = current_vs_hash_;
  submission.ps_hash = current_ps_hash_;
  submission.raster_draw_suppressed = SkipGpuDraw();

  if (batch && cmd) {
    submission.stream_count = cmd->stream_count;
    auto host_generation = [this](uint64_t key) -> uint64_t {
      auto it = buffers_.find(key);
      return it != buffers_.end() && it->second.resource
                 ? it->second.resource_generation : 0;
    };
    for (size_t i = 0;
         i < std::min<size_t>(cmd->stream_count, submission.streams.size()); ++i) {
      const StreamPlan& stream = batch->streams[cmd->stream_first + i];
      auto& evidence = submission.streams[i];
      evidence.guest_key = stream.buffer.key;
      evidence.host_generation = host_generation(stream.buffer.key);
      evidence.guest_address = stream.buffer.address;
      evidence.bytes = stream.size;
      evidence.stride = stream.stride;
      evidence.offset = stream.offset;
      evidence.buffer_bytes = stream.buffer.size;
      evidence.planned_action = stream.buffer.action;
      evidence.vertex_declaration = stream.buffer.decl;
      evidence.stream_slot = stream.stream;
      auto it = buffers_.find(stream.buffer.key);
      if (it != buffers_.end() && it->second.resource)
        evidence.content_version = it->second.content_version;
    }
    if (cmd->has_index) {
      auto& evidence = submission.index;
      evidence.guest_key = cmd->index.key;
      evidence.host_generation = host_generation(cmd->index.key);
      evidence.guest_address = cmd->index.address;
      evidence.bytes = cmd->index_size;
      evidence.stride = cmd->index32 ? 4 : 2;
      evidence.buffer_bytes = cmd->index.size;
      evidence.planned_action = cmd->index.action;
      auto it = buffers_.find(cmd->index.key);
      if (it != buffers_.end() && it->second.resource)
        evidence.content_version = it->second.content_version;
    }
    // The XDK declaration is shared by the planned streams. Observe the
    // POSITION0 semantic only for directly versioned triangle submissions.
    if (base && cmd->stream_count &&
        graphics::ClassifyDirectTriangleInput(submission) ==
            graphics::DirectTriangleInputStatus::kCandidate) {
      const uint32_t declaration = batch->streams[cmd->stream_first].buffer.decl;
      submission.position_layout = graphics::InspectPositionLayout(
          submission, ReadVertexDeclaration(base, declaration));
    }
    submission.position_access = graphics::InspectPositionAccess(submission);
    if (submission.position_access == graphics::PositionAccessStatus::kNonIndexedRangeValid &&
        submission.position_layout.status == graphics::PositionLayoutStatus::kFloat3) {
      submission.position_value = position_draw_value_;
      if (submission.position_value.status == graphics::PositionValueStatus::kNotSampled)
        submission.position_value.status = graphics::PositionValueStatus::kUnavailableFreshUpload;
    }
  }
  geometry_probe_.Observe(submission);
}

void Renderer::ExecDrawVertices(uint8_t* base, const WorkBatch& batch, const WorkCmd& cmd) {
  uint32_t prim = cmd.u[0], start_vertex = cmd.u[1], vertex_count = cmd.u[2];
  crumb_prim_ = prim;
  crumb_count_ = vertex_count;
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!BeginFrame()) return;
  uint32_t dev = GuestDevice(base);
  D3D12_PRIMITIVE_TOPOLOGY topology;
  bool quads;
  const uint32_t surface_before = surface_probe_ ? surface_sequence_.total_draws() : 0;
  bool streams_ok = BindVertexStreams(base, batch, cmd);
  if (!dev || !vertex_count || !PrepareDraw(base, dev, prim, topology, quads) || !streams_ok) {
    ++stats_.draws_skipped; ++stats_.pass_skips[g_current_pass & 31];
    return;
  }
  if (!REXCVAR_GET(sr_native_debug_vs).empty() &&
      current_vs_hash_ == std::strtoull(REXCVAR_GET(sr_native_debug_vs).c_str(), nullptr, 16)) {
    static int logged = 0;
    if (logged++ < 30) {
      REXLOG_INFO("native dbg vs exec (non-indexed): draw {} prim {} start {} vertices {} quads {} ps {:016X} topology {}",
                  draw_calls_in_frame_ - 1, prim, start_vertex, vertex_count, quads, current_ps_hash_, int(topology));
    }
  }
  if (quads) {
    uint32_t quad_count = vertex_count / 4;
    UploadAlloc ib;
    if (!Upload(size_t(quad_count) * 6 * 4, 4, ib)) return;
    uint32_t* idx = reinterpret_cast<uint32_t*>(ib.cpu);
    for (uint32_t q = 0; q < quad_count; ++q) {
      uint32_t b = start_vertex + q * 4;
      idx[q * 6 + 0] = b;
      idx[q * 6 + 1] = b + 1;
      idx[q * 6 + 2] = b + 2;
      idx[q * 6 + 3] = b;
      idx[q * 6 + 4] = b + 2;
      idx[q * 6 + 5] = b + 3;
    }
    D3D12_INDEX_BUFFER_VIEW ibv{ib.gpu, quad_count * 24, DXGI_FORMAT_R32_UINT};
    command_list_->IASetIndexBuffer(&ibv);
    if (!SkipGpuDraw()) {BeforeMotionColorDraw();command_list_->DrawIndexedInstanced(quad_count * 6, 1, 0, 0, 0);AfterMotionColorDraw();}
    // Generated/inline geometry has no attested temporal identity here.
    CaptureMotionClipDraw(base,dev,topology,true,quad_count*6,0,0,
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,nullptr,nullptr);
  } else {
    if (!SkipGpuDraw()) {BeforeMotionColorDraw();command_list_->DrawInstanced(vertex_count, 1, start_vertex, 0);AfterMotionColorDraw();}
    CapturePostVertexDraw(base,dev,topology,false,vertex_count,start_vertex);
    CaptureMotionClipDraw(base,dev,topology,false,vertex_count,start_vertex,0,
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,&batch,&cmd);
  }
  if (surface_probe_) {
    graphics::GeometrySubmission submission{};
    submission.kind = graphics::GeometrySubmissionKind::kVertices;
    submission.primitive = prim;
    submission.elements = vertex_count;
    submission.first_element = start_vertex;
    submission.gpu_topology = GeometryProbeTopology(topology);
    submission.gpu_index_source = quads ? graphics::GeometryIndexSource::kGenerated
                                        : graphics::GeometryIndexSource::kNone;
    submission.gpu_elements = quads ? (vertex_count / 4) * 6 : vertex_count;
    submission.gpu_first_element = quads ? 0 : start_vertex;
    submission.gpu_index32 = quads;
    RecordGeometrySubmission(base, &batch, &cmd, submission, surface_before);
  }
  ++stats_.draws; ++trace_draws_; Breadcrumb();
  ++stats_.pass_draws[g_current_pass & 31];
}

void Renderer::ExecDrawIndexedVertices(uint8_t* base, const WorkBatch& batch,
                                       const WorkCmd& cmd) {
  uint32_t prim = cmd.u[0], start_index = cmd.u[2], index_count = cmd.u[3];
  int32_t base_vertex = int32_t(cmd.u[1]);
  crumb_prim_ = prim | 0x100;
  crumb_count_ = index_count;
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!BeginFrame()) return;
  uint32_t dev = GuestDevice(base);
  D3D12_PRIMITIVE_TOPOLOGY topology;
  bool quads;
  // Uploads normalize the guest's programmable restart value to the host cut.
  D3D12_INDEX_BUFFER_STRIP_CUT_VALUE cut = cmd.index.reset_index == UINT32_MAX
      ? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED : cmd.index32
                                               ? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF
                                               : D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
  // Buffers first (planned uploads must happen even for skipped draws).
  const uint32_t surface_before = surface_probe_ ? surface_sequence_.total_draws() : 0;
  bool streams_ok = BindVertexStreams(base, batch, cmd);
  const BufferEntry* ib = cmd.has_index ? ApplyBuffer(base, cmd.index) : nullptr;
  const bool prepared = dev && index_count && PrepareDraw(base, dev, prim, topology, quads, cut);
  const uint32_t quad_indices = cmd.u[5];  // expanded triangle indices of an indexed quad list
  if (!prepared || (quads && !quad_indices) || !streams_ok || !ib) {
    if (!REXCVAR_GET(sr_native_debug_vs).empty() &&
        current_vs_hash_ == std::strtoull(REXCVAR_GET(sr_native_debug_vs).c_str(), nullptr, 16)) {
      static int logged = 0;
      if (logged++ < 12) {
        REXLOG_INFO("native dbg vs SKIP indexed: prepared {} quads {} streams_ok {} ib {} (has_index {} "
                    "index_size {}) prim {} indices {}", prepared, quads, streams_ok, ib != nullptr,
                    cmd.has_index, cmd.index_size, prim, index_count);
      }
    }
    ++stats_.draws_skipped; ++stats_.pass_skips[g_current_pass & 31];
    return;
  }
  if (quads) {
    UploadAlloc tris;
    if (!Upload(size_t(quad_indices) * 4, 4, tris)) return;
    std::memcpy(tris.cpu, batch.bytes.data() + cmd.u[4], size_t(quad_indices) * 4);
    D3D12_INDEX_BUFFER_VIEW qv{tris.gpu, quad_indices * 4, DXGI_FORMAT_R32_UINT};
    command_list_->IASetIndexBuffer(&qv);
    if (!SkipGpuDraw()) {BeforeMotionColorDraw();command_list_->DrawIndexedInstanced(quad_indices, 1, 0, base_vertex, 0);AfterMotionColorDraw();}
    ReportObservedClothDraw(batch,cmd);
    // Only the audited cloth producer supplies persistent slots for these
    // generated quad indices; every other generated path remains unverified.
    const bool observed_cloth=REXCVAR_GET(sr_native_debug_observed_skin_motion) &&
        current_vs_hash_==0xA5232A3184AA617Cull && cmd.observed_render.cloth.source_route_observed;
    CaptureMotionClipDraw(base,dev,topology,true,quad_indices,0,base_vertex,
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,observed_cloth?&batch:nullptr,observed_cloth?&cmd:nullptr);
    if (surface_probe_) {
      graphics::GeometrySubmission submission{};
      submission.kind = graphics::GeometrySubmissionKind::kIndexed;
      submission.primitive = prim;
      submission.elements = index_count;
      submission.first_element = start_index;
      submission.base_vertex = base_vertex;
      submission.index32 = cmd.index32;
      submission.gpu_topology = GeometryProbeTopology(topology);
      submission.gpu_index_source = graphics::GeometryIndexSource::kGenerated;
      submission.gpu_elements = quad_indices;
      submission.gpu_first_element = 0;
      submission.gpu_index32 = true;
      RecordGeometrySubmission(base, &batch, &cmd, submission, surface_before);
    }
    ++stats_.draws; ++trace_draws_; Breadcrumb();
    ++stats_.pass_draws[g_current_pass & 31];
    return;
  }
  D3D12_INDEX_BUFFER_VIEW ibv{ib->resource->GetGPUVirtualAddress(), cmd.index_size,
                              cmd.index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT};
  command_list_->IASetIndexBuffer(&ibv);
  if (!REXCVAR_GET(sr_native_debug_vs).empty() &&
      current_vs_hash_ == std::strtoull(REXCVAR_GET(sr_native_debug_vs).c_str(), nullptr, 16)) {
    static int logged = 0;
    if (logged++ < 30) {
      std::string st;
      for (uint32_t i = 0; i < cmd.stream_count; ++i) {
        const StreamPlan& sp = batch.streams[cmd.stream_first + i];
        st += fmt::format(" s{}:{}B/{}", sp.stream, sp.size, sp.stride);
      }
      REXLOG_INFO("native dbg vs exec: draw {} prim {} indices {} start {} base_vertex {} ps {:016X} "
                  "ib {}B index32 {} streams{}", draw_calls_in_frame_ - 1, prim, index_count, start_index,
                  base_vertex, current_ps_hash_, cmd.index_size, cmd.index32, st);
    }
  }
  if (!SkipGpuDraw()) {BeforeMotionColorDraw();command_list_->DrawIndexedInstanced(index_count, 1, start_index, base_vertex, 0);AfterMotionColorDraw();}
  CapturePostVertexDraw(base,dev,topology,true,index_count,start_index,base_vertex,cut);
  CaptureMotionClipDraw(base,dev,topology,true,index_count,start_index,base_vertex,cut,&batch,&cmd);
  if (surface_probe_) {
    graphics::GeometrySubmission submission{};
    submission.kind = graphics::GeometrySubmissionKind::kIndexed;
    submission.primitive = prim;
    submission.elements = index_count;
    submission.first_element = start_index;
    submission.base_vertex = base_vertex;
    submission.index32 = cmd.index32;
    submission.gpu_topology = GeometryProbeTopology(topology);
    submission.gpu_index_source = graphics::GeometryIndexSource::kGuest;
    submission.gpu_elements = index_count;
    submission.gpu_first_element = start_index;
    submission.gpu_index32 = cmd.index32;
    RecordGeometrySubmission(base, &batch, &cmd, submission, surface_before);
  }
  ++stats_.draws; ++trace_draws_; Breadcrumb();
  ++stats_.pass_draws[g_current_pass & 31];
}

// Xenos rectangle list: every 3 vertices describe a rectangle. As in Xenia's
// rectangle list geometry shader, the longest edge (by position x/y) is the
// diagonal and the 4th corner mirrors the remaining vertex across it
// (v3 = d1 + d2 - corner, for all float attributes; parallelogram completion
// is affine-invariant, so doing it before the vertex shader is exact). Expands
// already byte-swapped vertices into a triangle list (6 vertices per rectangle).
static std::vector<uint8_t> ExpandRectList(uint8_t* base, uint32_t decl, const uint8_t* vertices,
                                           uint32_t vertex_count, uint32_t stride) {
  std::vector<uint8_t> out;std::string error;
  graphics::guest::ExpandRectangles(std::as_bytes(std::span(vertices,size_t(vertex_count)*stride)),
      vertex_count,stride,ReadVertexDeclaration(base,decl),out,error);
  return out;
}

void Renderer::ExecDrawInlineVertices(uint8_t* base, uint32_t prim, uint32_t data,
                                      uint32_t vertex_count, uint32_t stride) {
  crumb_prim_ = prim | 0x200;
  crumb_count_ = vertex_count;
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!BeginFrame()) return;
  uint32_t dev = GuestDevice(base);
  D3D12_PRIMITIVE_TOPOLOGY topology;
  bool quads;
  const uint32_t surface_before = surface_probe_ ? surface_sequence_.total_draws() : 0;
  if (!dev || !vertex_count || !stride || !data ||
      !PrepareDraw(base, dev, prim, topology, quads)) {
    ++stats_.draws_skipped; ++stats_.pass_skips[g_current_pass & 31];
    return;
  }
  uint32_t decl = Load32(base, dev + kDevVertexDecl);
  UploadAlloc vb;
  size_t vb_size = size_t(vertex_count) * stride;
  if (!Upload(vb_size, 16, vb)) {
    ++stats_.draws_skipped; ++stats_.pass_skips[g_current_pass & 31];
    return;
  }
  thread_local std::vector<uint8_t> inline_scratch;
  inline_scratch.resize(vb_size);
  uint8_t* vdata = inline_scratch.data();
  std::memcpy(vdata, GuestPtr(base, data, uint32_t(vb_size)), vb_size);
  graphics::guest::SwapVertexElements({vdata,vb_size},stride,0,UINT32_MAX,
      ReadVertexDeclaration(base,decl));
  if (trace_state_ == 1) {
    std::string vs;
    for (uint32_t k = 0; k < std::min<uint32_t>(vertex_count * stride / 4, 40); ++k) {
      uint32_t u;
      std::memcpy(&u, vdata + 4 * k, 4);
      vs += fmt::format(" {:08X}", u);
    }
    REXLOG_INFO("trace   inline prim {} count {} stride {}:{}", prim, vertex_count, stride, vs);
  }
  if (prim == uint32_t(xenos::PrimitiveType::kRectangleList)) {
    std::vector<uint8_t> expanded = ExpandRectList(base, decl, vdata, vertex_count, stride);
    UploadAlloc rect_vb;
    if (expanded.empty() || !Upload(expanded.size(), 16, rect_vb)) {
      ++stats_.draws_skipped;
      return;
    }
    std::memcpy(rect_vb.cpu, expanded.data(), expanded.size());
    D3D12_VERTEX_BUFFER_VIEW rect_view{rect_vb.gpu, uint32_t(expanded.size()), stride};
    command_list_->IASetVertexBuffers(0, 1, &rect_view);
    bound_vertex_views_mask_ &= ~1u;
    if (!SkipGpuDraw()) {CaptureMotionInlineColorGeometry(base,decl,expanded,stride,uint32_t(expanded.size()/stride),topology,false);BeforeMotionColorDraw();command_list_->DrawInstanced(uint32_t(expanded.size() / stride), 1, 0, 0);AfterMotionColorDraw();}
    // Generated/inline geometry has no attested temporal identity here.
    CaptureMotionClipDraw(base,dev,topology,false,uint32_t(expanded.size()/stride),0,0,
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,nullptr,nullptr);
    if (surface_probe_) {
      graphics::GeometrySubmission submission{};
      submission.kind = graphics::GeometrySubmissionKind::kInline;
      submission.primitive = prim;
      submission.elements = vertex_count;
      submission.gpu_topology = GeometryProbeTopology(topology);
      submission.gpu_elements = uint32_t(expanded.size() / stride);
      submission.gpu_vertices_expanded = true;
      submission.stream_count = 1;
      submission.streams[0].guest_address = data;
      submission.streams[0].bytes = uint32_t(vb_size);
      submission.streams[0].stride = stride;
      RecordGeometrySubmission(base, nullptr, nullptr, submission, surface_before);
    }
    ++stats_.draws; ++trace_draws_; Breadcrumb();
    ++stats_.pass_draws[g_current_pass & 31];
    return;
  }
  std::memcpy(vb.cpu, vdata, vb_size);
  D3D12_VERTEX_BUFFER_VIEW vbv{vb.gpu, uint32_t(vb_size), stride};
  command_list_->IASetVertexBuffers(0, 1, &vbv);
  bound_vertex_views_mask_ &= ~1u;
  if (quads) {
    uint32_t quad_count = vertex_count / 4;
    UploadAlloc ib;
    if (!Upload(size_t(quad_count) * 6 * 2, 4, ib)) return;
    uint16_t* idx = reinterpret_cast<uint16_t*>(ib.cpu);
    for (uint32_t q = 0; q < quad_count; ++q) {
      uint16_t b = uint16_t(q * 4);
      idx[q * 6 + 0] = b;
      idx[q * 6 + 1] = b + 1;
      idx[q * 6 + 2] = b + 2;
      idx[q * 6 + 3] = b;
      idx[q * 6 + 4] = b + 2;
      idx[q * 6 + 5] = b + 3;
    }
    D3D12_INDEX_BUFFER_VIEW ibv{ib.gpu, quad_count * 12, DXGI_FORMAT_R16_UINT};
    command_list_->IASetIndexBuffer(&ibv);
    if (!SkipGpuDraw()) {CaptureMotionInlineColorGeometry(base,decl,{vdata,vb_size},stride,vertex_count,topology,quads);BeforeMotionColorDraw();command_list_->DrawIndexedInstanced(quad_count * 6, 1, 0, 0, 0);AfterMotionColorDraw();}
    // Generated/inline geometry has no attested temporal identity here.
    CaptureMotionClipDraw(base,dev,topology,true,quad_count*6,0,0,
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,nullptr,nullptr);
  } else {
    if (!SkipGpuDraw()) {CaptureMotionInlineColorGeometry(base,decl,{vdata,vb_size},stride,vertex_count,topology,quads);BeforeMotionColorDraw();command_list_->DrawInstanced(vertex_count, 1, 0, 0);AfterMotionColorDraw();}
    // Generated/inline geometry has no attested temporal identity here.
    CaptureMotionClipDraw(base,dev,topology,false,vertex_count,0,0,
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,nullptr,nullptr);
  }
  if (surface_probe_) {
    graphics::GeometrySubmission submission{};
    submission.kind = graphics::GeometrySubmissionKind::kInline;
    submission.primitive = prim;
    submission.elements = vertex_count;
    submission.gpu_topology = GeometryProbeTopology(topology);
    submission.gpu_index_source = quads ? graphics::GeometryIndexSource::kGenerated
                                        : graphics::GeometryIndexSource::kNone;
    submission.gpu_elements = quads ? (vertex_count / 4) * 6 : vertex_count;
    submission.gpu_index32 = false;
    submission.stream_count = 1;
    submission.streams[0].guest_address = data;
    submission.streams[0].bytes = uint32_t(vb_size);
    submission.streams[0].stride = stride;
    RecordGeometrySubmission(base, nullptr, nullptr, submission, surface_before);
  }
  ++stats_.draws; ++trace_draws_; Breadcrumb();
  ++stats_.pass_draws[g_current_pass & 31];
}

void Renderer::ExecBeginTiling(uint8_t* base, uint32_t count, uint32_t rects,
                               uint32_t clear_color, float clear_z, uint32_t clear_stencil) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!BeginFrame()) return;
  uint32_t w = 0, h = 0;
  for (uint32_t i = 0; i < count && rects; ++i) {
    w = std::max(w, Load32(base, rects + 16 * i + 8));
    h = std::max(h, Load32(base, rects + 16 * i + 12));
  }
  if (trace_state_ == 1)
    REXLOG_INFO("trace p{} after {} draws: BeginTiling {} tiles {}x{}", g_current_pass, trace_draws_,
                count, w, h);
  tiling_active_ = w && h;
  tiling_width_ = w;
  tiling_height_ = h;
  uint32_t dev = GuestDevice(base);
  if (!dev) return;
  // Clear the (full-size) tiling surfaces as the XDK does per tile.
  HostSurface* rt = GetSurface(base, Load32(base, dev + kDevRenderTargets), false);
  HostSurface* ds = GetSurface(base, Load32(base, dev + kDevDepthStencil), true);
  if (rt && clear_color) {
    float color[4];
    for (uint32_t i = 0; i < 4; ++i) color[i] = LoadF32(base, clear_color + 4 * i);
    if (REXCVAR_GET(sr_native_debug_clears)) {
      static int logged = 0;
      if (logged++ < 40) {
        REXLOG_INFO("native: BeginTiling clear {} {}x{} color ({}, {}, {}, {}) edram {}", SurfaceName(rt),
                    rt->width, rt->height, color[0], color[1], color[2], color[3], rt->edram_base);
      }
    }
    if (trace_state_ == 1)
      REXLOG_INFO("trace BeginTiling clear {} color ({}, {}, {}, {})", SurfaceName(rt), color[0],
                  color[1], color[2], color[3]);
    // The EDRAM clear also defines the contents of every surface aliasing
    // that EDRAM (e.g. the 8888 decal/leaf buffer sharing base 936 with the
    // HDR scene target): clear the host aliases too.
    for (auto& [key, other] : surfaces_) {
      if (other.depth || other.edram_base != rt->edram_base || other.width != rt->width ||
          other.height != rt->height || other.scale != rt->scale) {
        continue;
      }
      Transition(other.resource.Get(), other.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
      command_list_->ClearRenderTargetView(other.view, color, 0, nullptr);
    }
  }
  if (ds) {
    MaybeFinishMotionBeforeDepthWrite(ds,true,"BeginTiling depth clear");
    Transition(ds->resource.Get(), ds->state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    command_list_->ClearDepthStencilView(ds->view, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                         std::clamp(clear_z, 0.0f, 1.0f), uint8_t(clear_stencil),
                                         0, nullptr);
  }
  bound_rt_count_ = 0;
  bound_ds_ = nullptr;
}

void Renderer::ExecEndTiling() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (trace_state_ == 1) REXLOG_INFO("trace p{} after {} draws: EndTiling", g_current_pass, trace_draws_);
  tiling_active_ = false;
}

void Renderer::CopyDepthRegion(HostSurface& src, ResolveTexture& dst, uint32_t x1, uint32_t y1,
                               uint32_t x2, uint32_t y2, uint32_t dx, uint32_t dy) {
  if (!src.srv_index) {
    src.srv_index = AllocSrvIndex();
    if (!src.srv_index) return;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srv.ViewDimension =
        src.samples > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(
        src.resource.Get(), &srv,
        provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                        src.srv_index));
  }
  if (!src.stencil_srv_index) {
    src.stencil_srv_index = AllocSrvIndex();
    if (!src.stencil_srv_index) return;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
    srv.ViewDimension =
        src.samples > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (src.samples <= 1) {
      srv.Texture2D.MipLevels = 1;
      srv.Texture2D.PlaneSlice = 1;
    }
    device_->CreateShaderResourceView(
        src.resource.Get(), &srv,
        provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                        src.stencil_srv_index));
  }
  Transition(src.resource.Get(), src.state,
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(dst.resource.Get(), dst.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
  if (dst.raw_resource) {
    Transition(dst.raw_resource.Get(), dst.raw_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
  }
  ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), sampler_heap_.Get()};
  command_list_->SetDescriptorHeaps(2, heaps);
  command_list_->SetGraphicsRootSignature(depth_copy_root_signature_.Get());
  int32_t offset[2] = {int32_t(x1) - int32_t(dx), int32_t(y1) - int32_t(dy)};
  command_list_->SetGraphicsRoot32BitConstants(0, 2, offset, 0);
  command_list_->SetGraphicsRootDescriptorTable(
      1, provider_->OffsetViewDescriptor(srv_heap_->GetGPUDescriptorHandleForHeapStart(),
                                         src.srv_index));
  command_list_->SetGraphicsRootDescriptorTable(
      2, provider_->OffsetViewDescriptor(srv_heap_->GetGPUDescriptorHandleForHeapStart(),
                                         src.stencil_srv_index));
  command_list_->SetPipelineState(src.samples > 1 ? depth_copy_ms_pipeline_.Get()
                                                  : depth_copy_pipeline_.Get());
  D3D12_CPU_DESCRIPTOR_HANDLE targets[2] = {dst.rtv, dst.raw_rtv.ptr ? dst.raw_rtv : dst.rtv};
  command_list_->OMSetRenderTargets(dst.raw_resource ? 2 : 1, targets, FALSE, nullptr);
  D3D12_VIEWPORT vp{float(dx), float(dy), float(x2 - x1), float(y2 - y1), 0.0f, 1.0f};
  D3D12_RECT sc{LONG(dx), LONG(dy), LONG(dx + (x2 - x1)), LONG(dy + (y2 - y1))};
  command_list_->RSSetViewports(1, &vp);
  command_list_->RSSetScissorRects(1, &sc);
  command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list_->DrawInstanced(3, 1, 0, 0);
  Breadcrumb("depth copy");
  if (dst.raw_resource) {
    Transition(dst.raw_resource.Get(), dst.raw_state,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
  // The guest state (root signature, heaps, targets) must be rebound.
  frame_state_bound_ = false;
  fixed_state_cached_ = false;
  bound_vertex_views_mask_ = 0;
  bound_rt_count_ = 0;
  bound_ds_ = nullptr;
  std::memset(bound_rts_, 0, sizeof(bound_rts_));
}

void Renderer::ExecResolve(uint8_t* base, uint32_t flags, uint32_t src_rect,
                           uint32_t dest_texture, uint32_t dest_point, uint32_t clear_color,
                           float clear_z, uint32_t clear_stencil,uint32_t level,uint32_t slice) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!BeginFrame()) return;
  uint32_t dev = GuestDevice(base);
  if (!dev) return;
  uint32_t source = flags & 7;
  bool depth = source == 4;
  HostSurface* src = GetSurface(
      base, Load32(base, dev + (depth ? kDevDepthStencil : kDevRenderTargets + 4 * source)), depth);
  if (!src) return;
  ++stats_.resolves;
  {
    const int want = REXCVAR_GET(sr_native_dump_before_resolve);
    const int index = resolve_in_frame_++;
    if (want >= 0 && index == want && !frame_dump_done_ &&
        REXCVAR_GET(sr_native_dump_frame_at_s) > 0 &&
        compat::BenchElapsedMs() >= REXCVAR_GET(sr_native_dump_frame_at_s) * 1000.0 && frame_open_) {
      command_list_->Close();
      ID3D12CommandList* lists[] = {command_list_.Get()};
      queue_->ExecuteCommandLists(1, lists);
      queue_->Signal(fence_.Get(), next_fence_value_++);
      DumpFrameResources(fmt::format("r{}_", index), true);
      command_list_->Reset(allocators_[frame_index_].Get(), nullptr);
      frame_state_bound_ = false;
      fixed_state_cached_ = false;
      bound_vertex_views_mask_ = 0;
      bound_rt_count_ = 0;
      bound_ds_ = nullptr;
      std::memset(bound_rts_, 0, sizeof(bound_rts_));
      frame_dump_done_ = true;
      REXLOG_INFO("native: dumped before resolve {}", index);
    }
  }
  for (SlotCache& sc : slot_cache_) sc.frame = ~0ull;
  // The command's PM4 words were scanned by Execute (copy draw included).
  uint32_t ring_before = ring_last_;
  uint64_t copies_before = exec_copies_before_;
  uint32_t copy_dest_info = mirror_.last_copy_dest_info;
  if (trace_state_ == 1) {
    {
      std::string dw;
      for (uint32_t a = ring_before; a < ring_last_ && a < ring_before + 4 * 96; a += 4)
        dw += fmt::format(" {:08X}", Load32(base, a));
      REXLOG_INFO("trace resolve packets:{}", dw);
    }
    REXLOG_INFO("trace resolve ring {:08X}->{:08X} copies {} dest_info {:08X} dest_base {:08X}",
                ring_before, ring_last_, mirror_.copy_draws - copies_before,
                mirror_.last_copy_dest_info, mirror_.last_copy_dest_base);
  }
  if (trace_state_ == 1) {
    uint32_t r[4] = {};
    if (src_rect) for (int i = 0; i < 4; ++i) r[i] = Load32(base, src_rect + 4 * i);
    uint32_t dp[2] = {};
    if (dest_point) for (int i = 0; i < 2; ++i) dp[i] = Load32(base, dest_point + 4 * i);
    uint32_t df1 = dest_texture ? Load32(base, dest_texture + 0x20) : 0;
    uint32_t df2 = dest_texture ? Load32(base, dest_texture + 0x24) : 0;
    REXLOG_INFO("trace p{} after {} draws: resolve flags {:X} src {} rect {},{}-{},{} -> {:08X} {}x{} fmt {} at {},{} (ptr {:08X}) destinfo {:08X}",
                g_current_pass, trace_draws_, flags, SurfaceName(src), r[0], r[1], r[2], r[3],
                dest_texture ? GuestPhysical(df1 & 0xFFFFF000u) : 0, (df2 & 0x1FFF) + 1,
                ((df2 >> 13) & 0x1FFF) + 1, df1 & 0x3F, dp[0], dp[1], dest_point, copy_dest_info);
    trace_draws_ = 0;
  }

  if (dest_texture) {
    uint32_t fetch[6];
    for (uint32_t d = 0; d < 6; ++d) fetch[d] = Load32(base, dest_texture + 0x1C + 4 * d);
    uint32_t dest_base = GuestPhysical(fetch[1] & 0xFFFFF000u);
    uint32_t dest_w = (fetch[2] & 0x1FFF) + 1;
    uint32_t dest_h = ((fetch[2] >> 13) & 0x1FFF) + 1;
    const auto stack_plan=stacked_resolves_?PlanStackedResolve(fetch,level,slice):std::nullopt;
    const bool volume=stack_plan.has_value() && !depth && src->samples==1 &&
        HostPx(stack_plan->width,src->scale)<=D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION &&
        HostPx(stack_plan->height,src->scale)<=D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
    const uint32_t layers=volume?stack_plan->layers:1;
    if(stacked_resolves_ && IsStacked2DFetch(fetch) && !volume)
      LogUnsupportedOnce("stacked resolve",dest_base,"requires one mip, color, one sample and host size <=2048; using legacy path");
    // Depth resolves become R32_FLOAT textures written by a shader copy (depth
    // formats only allow whole-subresource copies; the shadow atlas needs
    // region copies at a destination point).
    DXGI_FORMAT dst_format = depth ? DXGI_FORMAT_R32_FLOAT : src->resource_format;
    // The same guest memory is resolved at different sizes within a frame (e.g.
    // downsample chains): one host texture per size, never recreated per frame.
    uint64_t resolve_key = (uint64_t(dest_base) << 32) | (uint32_t(dst_format) & 0xFF) |
                           (((dest_w - 1) & 0x1FFF) << 8) | (((dest_h - 1) & 0x7FF) << 21);
    if (complete_resolve_identity_) {
      uint32_t scale_bits;
      std::memcpy(&scale_bits,&src->scale,sizeof(scale_bits));
      resolve_key=resolve_identities_.Get({dest_base,uint32_t(dst_format),dest_w,dest_h,
                                         scale_bits,layers,volume,depth,src->shadow});
    }
    resolve_latest_[dest_base] = resolve_key;
    ResolveTexture& dst = resolve_textures_[resolve_key];
    std::memcpy(dst.guest_fetch, fetch, sizeof(fetch));
    if (!dst.resource || dst.width != dest_w || dst.height != dest_h ||
        dst.scale != src->scale || dst.layers!=layers || dst.stacked_volume!=volume) {
      Retire(dst.resource);
      for (auto& [mapping, index] : dst.srv_by_mapping) RetireSrvIndex(index);
      D3D12_CPU_DESCRIPTOR_HANDLE rtv = dst.rtv, raw_rtv = dst.raw_rtv;
      Retire(dst.raw_resource);
      for (auto& [mapping, index] : dst.raw_srv_by_mapping) RetireSrvIndex(index);
      if (dst.plain_srv) RetireSrvIndex(dst.plain_srv);
      dst = ResolveTexture{};
      dst.rtv = rtv;
      dst.raw_rtv = raw_rtv;
      dst.width = dest_w;
      dst.height = dest_h;
      dst.layers = layers;
      dst.stacked_volume = volume;
      dst.scale = src->scale;
      dst.shadow = src->shadow;
      ++stats_.resolve_textures_created;
      dst.format = dst_format;
      dst.srv_format = depth ? DXGI_FORMAT_R32_FLOAT : src->srv_format;
      D3D12_RESOURCE_DESC desc{};
      desc.Dimension = volume?D3D12_RESOURCE_DIMENSION_TEXTURE3D:D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      desc.Width = render_scale::HostDimension(dest_w, dst.scale);
      desc.Height = render_scale::HostDimension(dest_h, dst.scale);
      desc.DepthOrArraySize = uint16_t(layers);
      desc.MipLevels = 1;
      desc.Format = dst.format;
      desc.SampleDesc.Count = 1;
      if (depth) desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      D3D12_HEAP_PROPERTIES heap{};
      heap.Type = D3D12_HEAP_TYPE_DEFAULT;
      dst.state = D3D12_RESOURCE_STATE_COPY_DEST;
      if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, dst.state,
                                                  nullptr, IID_PPV_ARGS(&dst.resource)))) {
        resolve_textures_.erase(resolve_key);
        resolve_latest_.erase(dest_base);
        return;
      }
      if(volume) {
        // Initialize every slice, including padding slices not written by the game.
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 bytes=0;
        device_->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
        UploadAlloc zeros;
        if(!Upload(size_t(bytes),D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,zeros)) {
          resolve_textures_.erase(resolve_key);resolve_latest_.erase(dest_base);
          return;
        }
        std::memset(zeros.cpu,0,size_t(bytes));
        footprint.Offset+=zeros.offset;
        D3D12_TEXTURE_COPY_LOCATION from{},to{};
        from.pResource=zeros.resource;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint=footprint;
        to.pResource=dst.resource.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        command_list_->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        REXLOG_INFO("native stacked resolve: created volume base={:08X} size={}x{}x{} format={}",dest_base,dest_w,dest_h,layers,uint32_t(dst.format));
      }
      if (depth) {
        if (!dst.rtv.ptr) {
          dst.rtv = provider_->OffsetRTVDescriptor(rtv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                                   rtv_heap_next_++ % kRtvHeapSize);
        }
        device_->CreateRenderTargetView(dst.resource.Get(), nullptr, dst.rtv);
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.raw_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        // The raw D24S8-as-RGBA8 copy (k_8_8_8_8 fetches of a depth resolve) is
        // never read from shadow maps; at shadow_quality 4 it would add 512 MB.
        if (!dst.shadow && SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                       dst.raw_state, nullptr,
                                                       IID_PPV_ARGS(&dst.raw_resource)))) {
          if (!dst.raw_rtv.ptr) {
            dst.raw_rtv = provider_->OffsetRTVDescriptor(
                rtv_heap_->GetCPUDescriptorHandleForHeapStart(), rtv_heap_next_++ % kRtvHeapSize);
          }
          device_->CreateRenderTargetView(dst.raw_resource.Get(), nullptr, dst.raw_rtv);
        }
      }
    }
    std::memcpy(dst.guest_fetch, fetch, sizeof(fetch));
    // copy_dest_swap: the copy exchanges red and blue (A8R8G8B8 destinations);
    // applied as a component swap in the texture views.
    if (mirror_.copy_draws != copies_before && !depth) {
      bool swap = (copy_dest_info >> 24) & 1;
      if (swap != dst.swap_rb) {
        dst.swap_rb = swap;
        for (auto& [mapping, index] : dst.srv_by_mapping) RetireSrvIndex(index);
        dst.srv_by_mapping.clear();
      }
    }
    // Source rectangle (default: whole destination).
    uint32_t x1 = 0, y1 = 0, x2 = std::min(dest_w, src->width), y2 = std::min(dest_h, src->height);
    if (src_rect) {
      x1 = Load32(base, src_rect + 0);
      y1 = Load32(base, src_rect + 4);
      x2 = Load32(base, src_rect + 8);
      y2 = Load32(base, src_rect + 12);
    }
    uint32_t dx = x1, dy = y1;
    if (dest_point) {
      dx = Load32(base, dest_point + 0);
      dy = Load32(base, dest_point + 4);
      // Predicated tiling: per-tile resolves of a full-screen target that pass
      // a (0,0) destination keep the tile at its screen position.
      if (tiling_active_ && REXCVAR_GET(sr_native_tile_resolve_in_place) && !dx && !dy &&
          (x1 || y1) && dest_w >= src->width && dest_h >= src->height) {
        dx = x1;
        dy = y1;
      }
    }
    x2 = std::min(x2, src->width);
    y2 = std::min(y2, src->height);
    if (dx + (x2 - x1) > dest_w) x2 = x1 + (dest_w - std::min(dest_w, dx));
    if (dy + (y2 - y1) > dest_h) y2 = y1 + (dest_h - std::min(dest_h, dy));
    if (x2 > x1 && y2 > y1 && REXCVAR_GET(sr_native_debug_resolve_mode) != 1) {
      // Guest pixels -> host pixels.
      const float k = src->scale;
      x1 = HostPx(x1, k);
      y1 = HostPx(y1, k);
      x2 = HostPx(x2, k);
      y2 = HostPx(y2, k);
      dx = HostPx(dx, k);
      dy = HostPx(dy, k);
      if (!depth) {
        Transition(src->resource.Get(), src->state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(dst.resource.Get(), dst.state, D3D12_RESOURCE_STATE_COPY_DEST);
      }
      D3D12_TEXTURE_COPY_LOCATION s{}, d{};
      s.pResource = src->resource.Get();
      s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      d.pResource = dst.resource.Get();
      d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      D3D12_BOX box{x1, y1, 0, x2, y2, 1};
      if(motion_capture_active_ && motion_capture_phase_==2) {
        dst.motion_source_frame=dst.motion_source_serial=dst.motion_source_resource=0;
        dst.motion_full_color_copy=false;
      }
      if (depth) {
        CopyDepthRegion(*src, dst, x1, y1, x2, y2, dx, dy);
        // The scene depth (not a shadow map) for soft particles later in the frame.
        if (!dst.shadow && dest_w >= 512) scene_depth_key_ = resolve_key;
      } else if (src->samples > 1) {
        // Xenos color resolves average the samples.
        Transition(src->resource.Get(), src->state, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        Transition(dst.resource.Get(), dst.state, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        if (!resolve_list_) command_list_.As(&resolve_list_);
        D3D12_RECT rect{LONG(x1), LONG(y1), LONG(x2), LONG(y2)};
        resolve_list_->ResolveSubresourceRegion(dst.resource.Get(), 0, dx, dy,
                                                src->resource.Get(), 0, &rect,
                                                src->resource_format, D3D12_RESOLVE_MODE_AVERAGE);
        Breadcrumb("msaa resolve");
      } else {
        command_list_->CopyTextureRegion(&d, dx, dy, volume?stack_plan->destination_z:0, &s, &box);
        if(volume && trace_state_==1)
          REXLOG_INFO("trace stacked resolve base={:08X} mip={} slice={} host_z={} layers={}",dest_base,level,slice,stack_plan->destination_z,layers);
        if(motion_capture_active_ && motion_capture_phase_==2) {
          dst.motion_source_frame=src->motion_write_frame;dst.motion_source_serial=src->motion_write_serial;
          dst.motion_source_resource=reinterpret_cast<uint64_t>(src->resource.Get());
          dst.motion_full_color_copy=!volume && !x1 && !y1 && !dx && !dy &&
              x2==HostPx(src->width,src->scale) && y2==HostPx(src->height,src->scale) &&
              x2==HostPx(dst.width,dst.scale) && y2==HostPx(dst.height,dst.scale);
          if(motion_scene_finished_ && motion_color_reports_<64)
            REXLOG_INFO("native motion color copy: host_frame={} serial={} source={:X} destination={:X} source_frame={} source_draw={} full={} rect={}x{}+{},{} destination_xy={},{}",frame_count_+1,++motion_color_serial_,dst.motion_source_resource,reinterpret_cast<uint64_t>(dst.resource.Get()),dst.motion_source_frame,dst.motion_source_serial,dst.motion_full_color_copy,x2-x1,y2-y1,x1,y1,dx,dy);
        }
        Breadcrumb("color resolve");
      }
      Transition(dst.resource.Get(), dst.state,
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
  }

  // Clears requested with the resolve.
  if (REXCVAR_GET(sr_native_debug_resolve_mode) == 2) flags &= ~0x300u;
  // Xenos clears only the resolved EDRAM rectangle: with predicated tiling
  // each tile's resolve clears that tile for the next pass, and our host
  // surfaces are untiled, so clearing everything would wipe the other tiles.
  D3D12_RECT clear_rect{0, 0, 16384, 16384};
  if (src_rect) {
    clear_rect = {LONG(Load32(base, src_rect + 0)), LONG(Load32(base, src_rect + 4)),
                  LONG(Load32(base, src_rect + 8)), LONG(Load32(base, src_rect + 12))};
  }
  if (flags & 0x100) {
    HostSurface* rt = GetSurface(base, Load32(base, dev + kDevRenderTargets), false);
    if (rt) {
      float color[4] = {};
      if (clear_color) {
        for (uint32_t i = 0; i < 4; ++i) color[i] = LoadF32(base, clear_color + 4 * i);
      }
      if (REXCVAR_GET(sr_native_debug_clears)) {
        static int logged = 0;
        if (rt->width >= 1000 && swap_number_ > 400 && logged++ < 60) {
          REXLOG_INFO("native: resolve clear {} {}x{} color ({}, {}, {}, {}) edram {} swap {}", SurfaceName(rt),
                      rt->width, rt->height, color[0], color[1], color[2], color[3], rt->edram_base,
                      swap_number_);
        }
      }
      D3D12_RECT r = clear_rect;
      r.right = std::min<LONG>(r.right, LONG(rt->width));
      r.bottom = std::min<LONG>(r.bottom, LONG(rt->height));
      r = ScaleRect(r, rt->scale);
      // Host surfaces of other formats at the same EDRAM base alias the same
      // memory on Xenos (e.g. the 8888 and HDR scene targets at 936).
      for (auto& [key, other] : surfaces_) {
        if (other.depth || other.edram_base != rt->edram_base || other.width != rt->width ||
            other.height != rt->height || other.scale != rt->scale) {
          continue;
        }
        Transition(other.resource.Get(), other.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        command_list_->ClearRenderTargetView(other.view, color, 1, &r);
      }
    }
  }
  if (flags & 0x200) {
    HostSurface* ds = GetSurface(base, Load32(base, dev + kDevDepthStencil), true);
    if (ds) {
      D3D12_RECT r = clear_rect;
      r.right = std::min<LONG>(r.right, LONG(ds->width));
      r.bottom = std::min<LONG>(r.bottom, LONG(ds->height));
      r = ScaleRect(r, ds->scale);
      {
        static int logged = 0;
        if (logged < 200 && REXCVAR_GET(sr_native_debug_clears)) {
          ++logged;
          REXLOG_INFO("native dbg depth clear {} z {} stencil {} rect {},{}-{},{} pass {} swap {}",
                      SurfaceName(ds), clear_z, clear_stencil, r.left, r.top, r.right, r.bottom,
                      g_current_pass, swap_number_);
        }
      }
      MaybeFinishMotionBeforeDepthWrite(ds,true,"resolve depth clear");
      Transition(ds->resource.Get(), ds->state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
      command_list_->ClearDepthStencilView(ds->view,
                                           D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                           std::clamp(clear_z, 0.0f, 1.0f),
                                           uint8_t(clear_stencil), 1, &r);
    }
  }
  // Render target states changed; force a rebind at the next draw.
  bound_rt_count_ = 0;
  bound_ds_ = nullptr;
  std::memset(bound_rts_, 0, sizeof(bound_rts_));
}

void Renderer::RecordCapturedRayProbe() {
  using namespace graphics::d3d12;
  const auto& ranges=post_vertex_capture_->ranges();
  if(ranges.empty()) return;
  if(!rt_capture_scene_) {
    auto scene=std::make_unique<RayTracingScene>();
    const HRESULT hr=scene->Initialize(device_);
    if(FAILED(hr)) {
      REXLOG_ERROR("native: captured DXR scene unavailable {:08X}: {}",uint32_t(hr),scene->LastError());
      return;
    }
    rt_capture_scene_=std::move(scene);
  }
  const auto& last=ranges.back();
  const uint32_t vertex_count=last.offset/12+last.vertices;
  // One opaque validation BLAS over the contiguous world-space arena. Materials,
  // alpha cutouts and logical character identity are not attested by this probe.
  const RayTracingGeometry geometry{post_vertex_capture_->vertices(),0,vertex_count,false,{}};
  HRESULT hr=rt_capture_scene_->BeginFrame(frame_index_);
  if(SUCCEEDED(hr)) hr=rt_capture_scene_->BuildScene(command_list_.Get(),{&geometry,1});
  if(FAILED(hr)) {
    REXLOG_ERROR("native: captured DXR BLAS/TLAS failed {:08X}: {}",uint32_t(hr),rt_capture_scene_->LastError());
    return;
  }
  REXLOG_INFO("native: captured DXR BLAS/TLAS recorded: vertices={} triangles={} dropped_draws={} (validation scene; no visual RT option applied)",
              vertex_count,vertex_count/3,post_vertex_capture_->dropped());
  if(!post_vertex_projection_valid_ || post_vertex_camera_conflict_) {
    REXLOG_WARN("native: captured DXR ray validation skipped: camera missing or ambiguous");
    return;
  }
  for(float value:post_vertex_camera_) if(!std::isfinite(value)) return;
  std::vector<RayTracingRay> rays;
  for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
    const auto p=graphics::RecoverWorldPosition(post_vertex_inverse_,
        {x*0.5f,y*0.5f,0.5f,1},{1,1,0,0},{0,0});
    if(!p.valid) continue;
    RayTracingRay ray{};
    double length2=0;
    for(size_t axis=0;axis<3;++axis) {
      ray.origin[axis]=post_vertex_camera_[axis];
      ray.direction[axis]=p.xyz[axis]-ray.origin[axis];
      length2+=double(ray.direction[axis])*ray.direction[axis];
    }
    if(!std::isfinite(length2) || length2<1e-12) continue;
    for(float& value:ray.direction) value/=float(std::sqrt(length2));
    ray.t_min=0.01f; ray.t_max=100000.0f;
    rays.push_back(ray);
  }
  if(rays.empty()) return;
  const uint32_t bytes=uint32_t(rays.size()*sizeof(RayTracingHit));
  auto buffer=[&](D3D12_HEAP_TYPE heap_type,D3D12_RESOURCE_FLAGS flags,
      D3D12_RESOURCE_STATES state,ComPtr<ID3D12Resource>& result) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=heap_type;
    D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=bytes; d.Height=1;
    d.DepthOrArraySize=d.MipLevels=1; d.SampleDesc.Count=1;
    d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags=flags;
    return device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,
                                            nullptr,IID_PPV_ARGS(&result));
  };
  hr=buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,rt_probe_results_[frame_index_]);
  if(SUCCEEDED(hr)) hr=buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST,rt_probe_readback_[frame_index_]);
  if(SUCCEEDED(hr)) hr=rt_capture_scene_->TraceRays(command_list_.Get(),rays,
                                                   rt_probe_results_[frame_index_].Get());
  if(FAILED(hr)) {
    REXLOG_ERROR("native: captured DXR ray query failed {:08X}: {}",uint32_t(hr),rt_capture_scene_->LastError());
    return;
  }
  auto state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  Transition(rt_probe_results_[frame_index_].Get(),state,D3D12_RESOURCE_STATE_COPY_SOURCE);
  command_list_->CopyBufferRegion(rt_probe_readback_[frame_index_].Get(),0,
                                  rt_probe_results_[frame_index_].Get(),0,bytes);
  rt_probe_ray_count_[frame_index_]=uint32_t(rays.size());
  RecordDepthReceiverProbe();
  RecordCapturedVisibility();
}

void Renderer::SnapshotPostVertexDepth() {
  auto* depth=bind_set_[4];
  if(!depth || depth->samples!=1 || !post_vertex_projection_valid_ || post_vertex_camera_conflict_) return;
  const auto desc=depth->resource->GetDesc();
  D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
  const HRESULT hr=device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
      D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&post_vertex_depth_snapshot_));
  if(FAILED(hr)) {REXLOG_ERROR("native: receiver depth snapshot failed {:08X}",uint32_t(hr));return;}
  const auto before=depth->state;
  Transition(depth->resource.Get(),depth->state,D3D12_RESOURCE_STATE_COPY_SOURCE);
  command_list_->CopyResource(post_vertex_depth_snapshot_.Get(),depth->resource.Get());
  Transition(depth->resource.Get(),depth->state,before);
  auto state=D3D12_RESOURCE_STATE_COPY_DEST;
  Transition(post_vertex_depth_snapshot_.Get(),state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  auto* color=bind_set_[0];
  if(color && color->samples==1) {
    const auto color_desc=color->resource->GetDesc();
    if(SUCCEEDED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&color_desc,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&post_vertex_color_snapshot_)))) {
      const auto color_before=color->state;
      Transition(color->resource.Get(),color->state,D3D12_RESOURCE_STATE_COPY_SOURCE);
      command_list_->CopyResource(post_vertex_color_snapshot_.Get(),color->resource.Get());
      Transition(color->resource.Get(),color->state,color_before);
      state=D3D12_RESOURCE_STATE_COPY_DEST;
      Transition(post_vertex_color_snapshot_.Get(),state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
  }
  REXLOG_INFO("native: receiver depth snapshot {}x{} fmt={} after_world_draws={} before_new_surface_bind (candidate scene boundary)",
              desc.Width,desc.Height,uint32_t(desc.Format),post_vertex_capture_->ranges().size());
}

void Renderer::RecordDepthReceiverProbe() {
  using namespace graphics::d3d12;
  if(!post_vertex_depth_snapshot_ || !rt_capture_scene_ || post_vertex_camera_conflict_) return;
  RayTracingDepthInputs input{};
  const auto desc=post_vertex_depth_snapshot_->GetDesc();
  input.depth=post_vertex_depth_snapshot_.Get(); input.width=uint32_t(desc.Width); input.height=desc.Height;
  input.grid_width=32; input.grid_height=18;
  std::memcpy(input.inv_view_projection,post_vertex_inverse_.data(),sizeof(input.inv_view_projection));
  std::memcpy(input.camera,post_vertex_camera_.data(),sizeof(input.camera));
  const uint32_t count=input.grid_width*input.grid_height,bytes=count*sizeof(RayTracingDepthHit);
  D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width=bytes; buffer.Height=1; buffer.DepthOrArraySize=buffer.MipLevels=1;
  buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  buffer.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
  HRESULT hr=device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&rt_depth_results_[frame_index_]));
  heap.Type=D3D12_HEAP_TYPE_READBACK; buffer.Flags=D3D12_RESOURCE_FLAG_NONE;
  if(SUCCEEDED(hr)) hr=device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
      D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rt_depth_readback_[frame_index_]));
  if(SUCCEEDED(hr)) hr=rt_capture_scene_->ProbeDepth(command_list_.Get(),input,rt_depth_results_[frame_index_].Get());
  if(FAILED(hr)) {REXLOG_ERROR("native: receiver depth probe failed {:08X}: {}",uint32_t(hr),rt_capture_scene_->LastError());return;}
  auto state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  Transition(rt_depth_results_[frame_index_].Get(),state,D3D12_RESOURCE_STATE_COPY_SOURCE);
  command_list_->CopyResource(rt_depth_readback_[frame_index_].Get(),rt_depth_results_[frame_index_].Get());
  rt_depth_count_[frame_index_]=count;
}

void Renderer::RecordCapturedVisibility() {
  using namespace graphics::d3d12;
  const bool match=post_vertex_caster_valid_ && post_vertex_receiver_valid_ &&
      graphics::ShadowProjectionsMatch(post_vertex_caster_projection_,post_vertex_receiver_projection_);
  if(!match || post_vertex_light_conflict_ || !post_vertex_depth_snapshot_ || !post_vertex_color_snapshot_) {
    REXLOG_INFO("native captured visibility skipped: caster={} receiver={} projection_match={} conflict={} depth={} color={}",
        post_vertex_caster_valid_,post_vertex_receiver_valid_,match,post_vertex_light_conflict_,
        bool(post_vertex_depth_snapshot_),bool(post_vertex_color_snapshot_));
    return;
  }
  const auto direction=graphics::ShadowRayDirection(post_vertex_receiver_projection_);
  RayTracingTraceInputs input{};
  const auto desc=post_vertex_depth_snapshot_->GetDesc();
  input.depth=post_vertex_depth_snapshot_.Get(); input.color=post_vertex_color_snapshot_.Get();
  input.width=uint32_t(desc.Width); input.height=desc.Height;
  std::memcpy(input.inv_view_projection,post_vertex_inverse_.data(),sizeof(input.inv_view_projection));
  std::memcpy(input.camera,post_vertex_camera_.data(),sizeof(input.camera));
  std::memcpy(input.direction_to_light,direction.xyz.data(),sizeof(input.direction_to_light));
  input.inv_view_projection_valid=input.camera_valid=input.light_valid=input.depth_valid=input.color_valid=true;
  // Explicit diagnostic settings, not claims about the game's light size/bias.
  input.ray_bias=0.05f; input.max_distance=10000; input.max_depth_delta=0.005f;
  const HRESULT trace=rt_capture_scene_->Trace(command_list_.Get(),input);
  if(FAILED(trace)) {REXLOG_ERROR("native captured visibility failed {:08X}: {}",uint32_t(trace),rt_capture_scene_->LastError());return;}
  auto* shadow=rt_capture_scene_->Outputs().shadow;
  const auto output_desc=shadow->GetDesc();
  uint64_t size=0;
  device_->GetCopyableFootprints(&output_desc,0,1,0,&rt_visibility_layout_[frame_index_],nullptr,nullptr,&size);
  D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width=size;
  buffer.Height=1; buffer.DepthOrArraySize=buffer.MipLevels=1; buffer.SampleDesc.Count=1;
  buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_READBACK;
  if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
      D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rt_visibility_readback_[frame_index_])))) return;
  auto state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  Transition(shadow,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION src{},dst{};
  src.pResource=shadow; src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.pResource=rt_visibility_readback_[frame_index_].Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint=rt_visibility_layout_[frame_index_];
  command_list_->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
  rt_visibility_size_[frame_index_]=size; rt_visibility_swap_[frame_index_]=swap_number_;
  REXLOG_INFO("native captured visibility recorded: {}x{} light=({:.6g},{:.6g},{:.6g}) matched_caster_receiver=true (diagnostic texture; no lighting composition or RT option applied)",
              input.width,input.height,direction.xyz[0],direction.xyz[1],direction.xyz[2]);
}

void Renderer::ReportCapturedRayProbe(uint32_t slot) {
  if(rt_visibility_size_[slot] && rt_visibility_readback_[slot]) {
    const size_t size=size_t(rt_visibility_size_[slot]); rt_visibility_size_[slot]=0;
    const D3D12_RANGE range{0,size}; void* data=nullptr;
    if(SUCCEEDED(rt_visibility_readback_[slot]->Map(0,&range,&data))) {
      const auto& layout=rt_visibility_layout_[slot];
      uint32_t valid=0,occluded=0; double sum=0;
      for(uint32_t y=0;y<layout.Footprint.Height;++y) {
        const auto* row=reinterpret_cast<const float*>(static_cast<const uint8_t*>(data)+layout.Offset+y*layout.Footprint.RowPitch);
        for(uint32_t x=0;x<layout.Footprint.Width;++x) if(std::isfinite(row[x]) && row[x]>=0 && row[x]<=1) {
          ++valid; occluded+=row[x]<1; sum+=row[x];
        }
      }
      const auto folder=std::filesystem::u8path(REXCVAR_GET(sr_native_dump_dir));
      const auto file=folder/fmt::format("rt_visibility_s{}.raw",rt_visibility_swap_[slot]);
      std::ofstream out;
      const uint32_t header[]{layout.Footprint.Width,layout.Footprint.Height,uint32_t(DXGI_FORMAT_R32_FLOAT),layout.Footprint.RowPitch};
      bool saved=false;
      if(!folder.empty()) {
        std::error_code error; std::filesystem::create_directories(folder,error);
        if(!error) {
          out.open(file,std::ios::binary);
          out.write(reinterpret_cast<const char*>(header),sizeof(header));
          const bool rows_saved=graphics::WritePaddedReadbackRows(out,
              {static_cast<const uint8_t*>(data),size},size_t(layout.Offset),
              layout.Footprint.Width*sizeof(float),layout.Footprint.RowPitch,layout.Footprint.Height);
          out.close(); saved=rows_saved && bool(out);
        }
      }
      const D3D12_RANGE empty{0,0}; rt_visibility_readback_[slot]->Unmap(0,&empty);
      REXLOG_INFO("native captured visibility completed: valid_receivers={} occluded={} mean_visibility={:.6g} dump={} saved={} (partial opaque scene; shadows not composed)",
          valid,occluded,valid ? sum/valid : 0,file.string(),saved);
    }
  }
  if(rt_depth_count_[slot] && rt_depth_readback_[slot]) {
    const uint32_t count=rt_depth_count_[slot]; rt_depth_count_[slot]=0;
    D3D12_RANGE range{0,count*sizeof(graphics::d3d12::RayTracingDepthHit)};
    void* data=nullptr;
    if(SUCCEEDED(rt_depth_readback_[slot]->Map(0,&range,&data))) {
      const auto* samples=static_cast<const graphics::d3d12::RayTracingDepthHit*>(data);
      uint32_t valid=0,hits=0,agree=0;
      std::vector<float> errors;
      for(uint32_t i=0;i<count;++i) if(samples[i].valid) {
        ++valid; hits+=samples[i].hit!=0;
        if(samples[i].hit && std::isfinite(samples[i].relative_error)) {
          errors.push_back(samples[i].relative_error);
          agree+=samples[i].relative_error<=0.01f;
        }
      }
      std::sort(errors.begin(),errors.end());
      const D3D12_RANGE empty{0,0}; rt_depth_readback_[slot]->Unmap(0,&empty);
      REXLOG_INFO("native receiver depth agreement: samples={} valid={} hits={} within_one_percent={} relative_error_p50={:.6g} p95={:.6g} fence_completed=true (coverage/camera experiment; no temporal readiness attested)",
          count,valid,hits,agree,errors.empty() ? -1.f : errors[errors.size()/2],
          errors.empty() ? -1.f : errors[(errors.size()-1)*95/100]);
    }
  }
  const uint32_t count=rt_probe_ray_count_[slot];
  if(!count || !rt_probe_readback_[slot]) return;
  rt_probe_ray_count_[slot]=0;
  const D3D12_RANGE range{0,count*sizeof(graphics::d3d12::RayTracingHit)};
  void* data=nullptr;
  if(FAILED(rt_probe_readback_[slot]->Map(0,&range,&data))) return;
  const auto* results=static_cast<const graphics::d3d12::RayTracingHit*>(data);
  uint32_t hits=0;
  for(uint32_t i=0;i<count;++i) {
    if(results[i].hit) {
      ++hits;
      REXLOG_INFO("native captured DXR hit: ray={} distance={:.6g} primitive={} instance={}",
                  i,results[i].distance,results[i].primitive_id,results[i].instance_id);
    }
  }
  const D3D12_RANGE empty{0,0}; rt_probe_readback_[slot]->Unmap(0,&empty);
  REXLOG_INFO("native captured DXR execution: rays={} hits={} misses={} completed_after_frame_slot_fence=true (probe only; RT shadows/reflections Off)",
              count,hits,count-hits);
}

void Renderer::BeginRayShadows() {
  HarvestRayShadowExecution();
  graphics_frame_request_=graphics::SharedGraphicsOptions()->Snapshot();
  rt_shadow_built_=rt_shadow_attempted_=rt_shadow_draw_active_=rt_shadow_counter_recorded_=false;
  if(ray_tracing_capable_ && rt_shadow_wave_capable_ && rt_shadow_shader_.empty()) {
    if(ShaderTranslator::Get().FetchRtShadowVariant(0x8A6DD1D52752FAD6ull,rt_shadow_shader_)==TranslateState::kReady)
      REXLOG_INFO("native RT shadow shader ready: {} bytes; original lighting family specialization",rt_shadow_shader_.size());
  }
  rt_shadow_requested_=graphics_frame_request_.requested[0]==1 && ray_tracing_capable_ &&
      rt_shadow_wave_capable_ && !rt_shadow_shader_.empty() && REXCVAR_GET(sr_native_pm4_mirror);
  if(!rt_shadow_requested_) return;
  auto buffer=[&](uint64_t bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,
                  D3D12_RESOURCE_FLAGS flags,ComPtr<ID3D12Resource>& resource) {
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;
    d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;
    d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=flags;
    return device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&resource));
  };
  HRESULT hr=S_OK;
  if(!rt_shadow_zero_) {
    hr=buffer(4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_FLAG_NONE,rt_shadow_zero_);
    void* mapped=nullptr; const D3D12_RANGE empty{0,0};
    if(SUCCEEDED(hr)) hr=rt_shadow_zero_->Map(0,&empty,&mapped);
    if(SUCCEEDED(hr)) {std::memset(mapped,0,4);rt_shadow_zero_->Unmap(0,nullptr);}
    else rt_shadow_zero_.Reset();
  }
  if(SUCCEEDED(hr) && !rt_shadow_counter_[frame_index_])
    hr=buffer(4,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,rt_shadow_counter_[frame_index_]);
  if(SUCCEEDED(hr) && !rt_shadow_counter_readback_[frame_index_])
    hr=buffer(4,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_FLAG_NONE,rt_shadow_counter_readback_[frame_index_]);
  if(FAILED(hr)) {REXLOG_ERROR("native RT shader counter unavailable {:08X}",uint32_t(hr));rt_shadow_requested_=false;return;}
  Transition(rt_shadow_counter_[frame_index_].Get(),rt_shadow_counter_state_[frame_index_],D3D12_RESOURCE_STATE_COPY_DEST);
  command_list_->CopyBufferRegion(rt_shadow_counter_[frame_index_].Get(),0,rt_shadow_zero_.Get(),0,4);
  Transition(rt_shadow_counter_[frame_index_].Get(),rt_shadow_counter_state_[frame_index_],D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void Renderer::BuildRayShadowScene() {
  using namespace graphics::d3d12;
  rt_shadow_attempted_=true;
  if(!post_vertex_capture_ || !post_vertex_caster_valid_ || post_vertex_light_conflict_) return;
  const auto& ranges=post_vertex_capture_->ranges();
  if(ranges.empty() || post_vertex_capture_->dropped()) return;
  for(const auto& range:ranges) if(!graphics::IsOriginalShadowCaster(range.vs)) return;
  post_vertex_capture_->Finish(command_list_.Get(),false);
  post_vertex_active_=false;
  if(!rt_shadow_scene_) {
    auto scene=std::make_unique<RayTracingScene>();
    const HRESULT hr=scene->Initialize(device_);
    if(FAILED(hr)) {REXLOG_WARN("native RT shadows initialization failed: {}",scene->LastError());return;}
    rt_shadow_scene_=std::move(scene);
  }
  const auto& last=ranges.back();
  const RayTracingGeometry geometry{post_vertex_capture_->vertices(),0,last.offset/12+last.vertices,false,{}};
  HRESULT hr=rt_shadow_scene_->BeginFrame(frame_index_);
  if(SUCCEEDED(hr)) hr=rt_shadow_scene_->BuildScene(command_list_.Get(),{&geometry,1});
  if(FAILED(hr)) {REXLOG_WARN("native RT shadows scene failed: {}",rt_shadow_scene_->LastError());return;}
  rt_shadow_built_=true;
}

bool Renderer::ConfigureRayShadowDraw(uint8_t* base,uint32_t dev,uint64_t vs,uint64_t ps) {
  // Only color-producing scene draws can contribute to the applied effect.
  // A depth-only invocation must not count as an active visual RT shadow.
  if(!bound_rts_[0] || bound_rts_[0]->guest_format!=12 ||
     !(LoadReg(base,dev,rex::graphics::XE_GPU_REG_RB_COLOR_MASK)&7u)) return false;
  if(!rt_shadow_requested_ || !rt_shadow_built_ || !rt_shadow_scene_->AccelerationStructure() ||
     vs!=0x2C45BFFE76D77398ull || !graphics::IsRtShadowShader(ps) ||
     rt_shadow_shader_.empty() || post_vertex_light_conflict_) return false;
  const auto& words=mirror_.FloatConstants(false);
  graphics::ShadowProjection projection;
  std::memcpy(projection.u.data(),words.data()+9*4,16);
  std::memcpy(projection.v.data(),words.data()+10*4,16);
  std::memcpy(projection.z.data(),words.data()+11*4,16);
  if(!graphics::ShadowProjectionsMatch(projection,post_vertex_caster_projection_)) return false;
  const auto direction=graphics::ShadowRayDirection(projection);
  rt_shadow_constants_={};
  std::memcpy(rt_shadow_constants_.light,direction.xyz.data(),sizeof(rt_shadow_constants_.light));
  rt_shadow_constants_.enabled=1;rt_shadow_constants_.bias=.05f;rt_shadow_constants_.max_distance=10000;
  const bool high=graphics_frame_request_.requested[1]==2;
  rt_shadow_constants_.cone=high ? .01f : .003f;rt_shadow_constants_.samples=high ? 16 : 4;
  rt_shadow_constants_.mask=3; // Logical character identity is still unknown.
  return true;
}

void Renderer::RecordRayShadowCounter() {
  if(!rt_shadow_requested_ || !rt_shadow_counter_[frame_index_] || !rt_shadow_counter_readback_[frame_index_]) return;
  Transition(rt_shadow_counter_[frame_index_].Get(),rt_shadow_counter_state_[frame_index_],D3D12_RESOURCE_STATE_COPY_SOURCE);
  command_list_->CopyBufferRegion(rt_shadow_counter_readback_[frame_index_].Get(),0,rt_shadow_counter_[frame_index_].Get(),0,4);
  rt_shadow_counter_recorded_=true;
}

void Renderer::HarvestRayShadowExecution() {
  if(!fence_) return;
  const uint64_t completed=fence_->GetCompletedValue();
  if(completed==UINT64_MAX) return;
  const auto previous=rt_shadow_execution_.Latest();
  for(uint32_t slot=0;slot<3;++slot) {
    const auto ticket=rt_shadow_execution_.Pending(slot);
    if(!ticket.fence || ticket.fence>completed) continue;
    uint32_t pixels=0;
    if(ticket.counter_recorded && rt_shadow_counter_readback_[slot]) {
      const D3D12_RANGE range{0,4};void* mapped=nullptr;
      if(SUCCEEDED(rt_shadow_counter_readback_[slot]->Map(0,&range,&mapped))) {
        std::memcpy(&pixels,mapped,4);
        const D3D12_RANGE empty{0,0};rt_shadow_counter_readback_[slot]->Unmap(0,&empty);
      }
    }
    rt_shadow_execution_.Complete(slot,completed,pixels);
  }
  const auto result=rt_shadow_execution_.Latest();
  if(result.applied!=previous.applied || (result.applied[0] && result.frame%120==0))
    REXLOG_INFO("native RT shadows executed: frame={} revision={} shader_pixels={} applied={} softness={} fence_completed=true (mapped lighting family; original fallback elsewhere)",
        result.frame,result.revision,result.pixels,result.applied[0],result.applied[1]);
}

void Renderer::PublishGraphicsOptions() {
  auto options=graphics::SharedGraphicsOptions();
  const auto request=options->Snapshot();
  graphics::GraphicsBackendReport report;
  const auto executed=rt_shadow_execution_.Latest();
  report.request_revision=executed.revision;
  report.frame=frame_count_+1;
  for(size_t i=0;i<4;++i)
    report.features[i]={false,ray_tracing_capable_,
        ray_tracing_capable_ ? "Scene geometry and lighting integration pending" : "DXR 1.1 is unavailable on this device"};
  const bool shadow_ready=ray_tracing_capable_ && rt_shadow_wave_capable_ && !rt_shadow_shader_.empty() &&
      REXCVAR_GET(sr_native_pm4_mirror);
  report.features[0]={shadow_ready,ray_tracing_capable_ && rt_shadow_wave_capable_,
      shadow_ready ? "Some materials retain original shadows" : "RT lighting shader is not ready"};
  report.features[1]=report.features[0];
  report.features[2]={false,ray_tracing_capable_,"Character caster ownership has not been identified"};
  report.applied=executed.applied;
  const bool dlss_supported=dlss_ && dlss_->GetStatus().support==graphics::d3d12::DlssSupport::Supported;
  report.features[4]={false,dlss_supported,dlss_supported ?
      "Scene depth, object motion and HUD separation pending" : "DLSS SDK support is unavailable"};
  if(request.requested[0] && !report.applied[0]) report.failure="RT shadows are waiting for a verified shader execution";
  options->PublishBackend(graphics_options_epoch_,report);
}

void Renderer::EndFrameAndPresent(uint32_t front_buffer_address) {
  if(post_vertex_active_ && post_vertex_capture_ && post_vertex_diagnostic_) {
    post_vertex_capture_->Finish(command_list_.Get(),true);
    RecordCapturedRayProbe();
    REXLOG_INFO("native: post-VS extraction recorded guest_swap={} draws={} dropped={}; results after slot fence",
                swap_number_,post_vertex_capture_->ranges().size(),post_vertex_capture_->dropped());
    post_vertex_done_=!post_vertex_capture_->ranges().empty();
    post_vertex_active_=false;
  }
  EndMotionCapture();
  RecordRayShadowCounter();
  // Blit the resolved front buffer into the output target.
  auto latest = resolve_latest_.find(front_buffer_address);
  auto it = latest != resolve_latest_.end() ? resolve_textures_.find(latest->second)
                                            : resolve_textures_.end();
  Transition(output_rt(), output_rt_states_[frame_index_], D3D12_RESOURCE_STATE_RENDER_TARGET);
  if (it != resolve_textures_.end()) {
    ResolveTexture& fb = it->second;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = fb.srv_format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    // One heap slot per frame slot for the blit source, allocated once. (Was:
    // allocate every frame and give back the heap top - wrong when the index
    // came from the retired list: the top index belonged to a live texture,
    // whose descriptor the next allocation then overwrote.)
    static uint32_t blit_slots[kFramesInFlight] = {};
    if (!blit_slots[frame_index_]) blit_slots[frame_index_] = AllocSrvIndex();
    uint32_t srv_index = blit_slots[frame_index_];
    auto cpu = provider_->OffsetViewDescriptor(srv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                               srv_index);
    device_->CreateShaderResourceView(fb.resource.Get(), &srv, cpu);
    Transition(fb.resource.Get(), fb.state,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), sampler_heap_.Get()};
    command_list_->SetDescriptorHeaps(2, heaps);
    command_list_->SetGraphicsRootSignature(blit_root_signature_.Get());
    command_list_->SetGraphicsRootDescriptorTable(
        0, provider_->OffsetViewDescriptor(srv_heap_->GetGPUDescriptorHandleForHeapStart(),
                                           srv_index));
    // Guest display gamma ramp (Xenos applies it at scan-out; Xenia too).
    UploadAlloc gamma;
    if (Upload(1024 + 16, 256, gamma)) {
      std::array<uint32_t,256> ramp{};
      if (!GuestGammaRamp256(ramp.data())) ramp.fill(0);
      const auto data=BuildGammaUpload(ramp,REXCVAR_GET(sr_native_gamma_ramp));
      std::memcpy(gamma.cpu,data.data(),sizeof(data));
      command_list_->SetGraphicsRootConstantBufferView(1, gamma.gpu);
    }
    command_list_->SetPipelineState(blit_pipeline_.Get());
    command_list_->OMSetRenderTargets(1, &output_rtvs_[frame_index_], FALSE, nullptr);
    D3D12_VIEWPORT vp{0, 0, float(output_width_), float(output_height_), 0, 1};
    D3D12_RECT sc{0, 0, LONG(output_width_), LONG(output_height_)};
    command_list_->RSSetViewports(1, &vp);
    command_list_->RSSetScissorRects(1, &sc);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);
    frame_state_bound_ = false;
    fixed_state_cached_ = false;
    bound_vertex_views_mask_ = 0;
  } else {
    float color[4] = {0.3f, 0.0f, 0.3f, 1.0f};  // magenta-ish: no front buffer resolved
    command_list_->ClearRenderTargetView(output_rtvs_[frame_index_], color, 0, nullptr);
  }

  // native_present=false: render (and dump) natively but leave presentation to
  // Xenos, for same-run A/B comparisons against its screenshots.
  if (!REXCVAR_GET(sr_native_ab_mode) && !REXCVAR_GET(sr_native_debug_no_present))
  presenter_->RefreshGuestOutput(
      output_width_, output_height_, 1280, 720,
      [&](rex::ui::Presenter::GuestOutputRefreshContext& context) -> bool {
        auto& d3d12_context =
            static_cast<rex::ui::d3d12::D3D12Presenter::D3D12GuestOutputRefreshContext&>(context);
        ID3D12Resource* output = d3d12_context.resource_uav_capable();
        Transition(output_rt(), output_rt_states_[frame_index_],
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_RESOURCE_STATES out_state =
            rex::ui::d3d12::D3D12Presenter::kGuestOutputInternalState;
        if (!own_queue_) {
          Transition(output, out_state, D3D12_RESOURCE_STATE_COPY_DEST);
          command_list_->CopyResource(output, output_rt());
          Transition(output, out_state,
                     rex::ui::d3d12::D3D12Presenter::kGuestOutputInternalState);
        }
        EndFrameTimestamp();
        ProfileEnd();
        command_list_->Close();
        ID3D12CommandList* lists[] = {command_list_.Get()};
        queue_->ExecuteCommandLists(1, lists);
        frame_open_ = false;
        if (own_queue_) {
          // The presenter's guest output belongs to the direct queue timeline:
          // copy there, after this frame's native work.
          queue_->Signal(fence_.Get(), next_fence_value_);
          ID3D12CommandQueue* direct = provider_->GetDirectQueue();
          direct->Wait(fence_.Get(), next_fence_value_);
          ++next_fence_value_;
          present_allocators_[frame_index_]->Reset();
          present_list_->Reset(present_allocators_[frame_index_].Get(), nullptr);
          D3D12_RESOURCE_BARRIER barrier{};
          barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          barrier.Transition.pResource = output;
          barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
          barrier.Transition.StateBefore = out_state;
          barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
          if (barrier.Transition.StateBefore != barrier.Transition.StateAfter) {
            present_list_->ResourceBarrier(1, &barrier);
          }
          present_list_->CopyResource(output, output_rt());
          std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
          if (barrier.Transition.StateBefore != barrier.Transition.StateAfter) {
            present_list_->ResourceBarrier(1, &barrier);
          }
          present_list_->Close();
          ID3D12CommandList* present_lists[] = {present_list_.Get()};
          direct->ExecuteCommandLists(1, present_lists);
          direct->Signal(present_fence_.Get(), present_fence_next_);
          present_fence_values_[frame_index_] = present_fence_next_++;
        }
        context.SetIs8bpc(true);
        return true;
      });

  if (frame_open_) {
    EndFrameTimestamp();
    ProfileEnd();
    command_list_->Close();
    ID3D12CommandList* lists[] = {command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    frame_open_ = false;
  }
  const HRESULT submitted=queue_->Signal(fence_.Get(), next_fence_value_);
  frame_fence_values_[frame_index_] = next_fence_value_;
  if(SUCCEEDED(submitted)) rt_shadow_execution_.Submit(frame_index_,next_fence_value_,frame_count_+1,
      graphics_frame_request_.request_revision,graphics_frame_request_.requested,rt_shadow_counter_recorded_);
  ++next_fence_value_;
  HarvestRayShadowExecution();
  PublishGraphicsOptions();
  ProfileReport();
  if (dump_all_active_) {
    dump_all_active_ = false;
    frame_dump_done_ = true;
    REXLOG_INFO("native: per-pass dump done");
  }
  bool dump_by_swap = REXCVAR_GET(sr_native_dump_swap) > 0 &&
                      swap_number_ == uint64_t(REXCVAR_GET(sr_native_dump_swap));
  if (!frame_dump_done_ && REXCVAR_GET(sr_native_dump_after_pass) < 0 &&
      (dump_by_swap || (REXCVAR_GET(sr_native_dump_before_draw) < 0 &&
                        REXCVAR_GET(sr_native_dump_before_resolve) < 0 &&
                        REXCVAR_GET(sr_native_dump_vs).empty() &&
                        !REXCVAR_GET(sr_native_watch_blue))) &&
      (dump_by_swap || (REXCVAR_GET(sr_native_dump_frame_at_s) > 0 &&
                        compat::BenchElapsedMs() >=
                            REXCVAR_GET(sr_native_dump_frame_at_s) * 1000.0))) {
    frame_dump_done_ = true;
    DumpFrameResources();
  }
  // Multi-swap A/B (sr_native_ab_swaps): the output only, prefixed by the swap.
  {
    static const std::vector<uint64_t> ab_swaps = [] {
      std::vector<uint64_t> v;
      std::string list = REXCVAR_GET(sr_native_ab_swaps);
      size_t pos = 0;
      while (pos < list.size()) {
        size_t comma = list.find(',', pos);
        if (comma == std::string::npos) comma = list.size();
        try {
          v.push_back(std::stoull(list.substr(pos, comma - pos)));
        } catch (...) {
        }
        pos = comma + 1;
      }
      return v;
    }();
    if (std::find(ab_swaps.begin(), ab_swaps.end(), swap_number_) != ab_swaps.end()) {
      DumpFrameResources(fmt::format("s{:06}_", swap_number_), false, true);
    }
  }
  std::memset(pass_draw_index_, 0, sizeof(pass_draw_index_));
  if (trace_state_ == 1) {
    REXLOG_INFO("trace end of frame ({} draws since last event)", trace_draws_);
    trace_state_ = 2;
  } else if (trace_state_ == 0 &&
             ((REXCVAR_GET(sr_native_trace_swap) > 0 &&
               swap_number_ + 1 == uint64_t(REXCVAR_GET(sr_native_trace_swap))) ||
              (REXCVAR_GET(sr_native_trace_frame_at_s) > 0 &&
               compat::BenchElapsedMs() >= REXCVAR_GET(sr_native_trace_frame_at_s) * 1000.0) ||
              NativeTraceTriggered())) {
    trace_state_ = 1;
    trace_draws_ = 0;
    REXLOG_INFO("trace begin frame {} after_guest_swap={}", frame_count_ + 1, swap_number_);
  }
  if (surface_probe_) {
    REXLOG_INFO("native surface sequence: guest_swap={} frame={} observed_draws={} segments={} "
                "dropped_segments={} (observations; game pass roles not identified)",
                swap_number_, frame_count_, surface_sequence_.total_draws(),
                surface_sequence_.size(), surface_sequence_.dropped_segments());
    for (size_t i = 0; i < surface_sequence_.size(); ++i) {
      const auto& segment = surface_sequence_[i];
      const auto& s = segment.surface;
      REXLOG_INFO("native surface sequence: segment={} first_draw={} draws={} color_edram={} "
                  "color={}x{} fmt={} depth_edram={} depth={}x{}",
                  i, segment.first_draw, segment.draws, s.color_edram, s.color_width,
                  s.color_height, s.color_format, s.depth_edram, s.depth_width, s.depth_height);
      for (uint32_t j = 0; j < segment.sample_count; ++j) {
        const auto& sample = segment.samples[j];
        const auto& draw = sample.draw;
        REXLOG_INFO("native surface sample: segment={} draw_offset={} guest_pass={} prim={} "
                    "vs={:016X} ps={:016X} depth={:08X} mask={:08X}",
                    i, sample.draw_offset, draw.guest_pass, draw.primitive,
                    draw.vs_hash, draw.ps_hash, draw.depth_control, draw.color_mask);
      }
    }
    REXLOG_INFO("native geometry sequence: guest_swap={} frame={} submissions={} vertices={} "
                "indexed={} inline={} samples={} (host versions tracked; no guest mesh identity/BLAS reuse proven)",
                geometry_probe_.guest_swap(), geometry_probe_.frame(), geometry_probe_.total(),
                geometry_probe_.count(graphics::GeometrySubmissionKind::kVertices),
                geometry_probe_.count(graphics::GeometrySubmissionKind::kIndexed),
                geometry_probe_.count(graphics::GeometrySubmissionKind::kInline),
                geometry_probe_.sample_count());
    REXLOG_INFO("native geometry readiness: candidate={} non_triangle={} too_few={} "
                "generated={} missing_streams={} unobserved_streams={} "
                "unversioned_stream={} unversioned_index={} (position layout and transforms still unverified)",
                geometry_probe_.status_count(graphics::DirectTriangleInputStatus::kCandidate),
                geometry_probe_.status_count(graphics::DirectTriangleInputStatus::kNonTriangle),
                geometry_probe_.status_count(graphics::DirectTriangleInputStatus::kTooFewElements),
                geometry_probe_.status_count(graphics::DirectTriangleInputStatus::kGeneratedGeometry),
                geometry_probe_.status_count(graphics::DirectTriangleInputStatus::kMissingStreams),
                geometry_probe_.status_count(graphics::DirectTriangleInputStatus::kUnobservedStreams),
                geometry_probe_.status_count(graphics::DirectTriangleInputStatus::kUnversionedStream),
                geometry_probe_.status_count(graphics::DirectTriangleInputStatus::kUnversionedIndex));
    REXLOG_INFO("native geometry POSITION0 layout: float3={} float4={} other_encoded={} "
                "missing={} missing_stream={} unsupported_type={} out_of_bounds={} "
                "not_checked={} (declaration only; vertex values/transforms not validated)",
                geometry_probe_.position_count(graphics::PositionLayoutStatus::kFloat3),
                geometry_probe_.position_count(graphics::PositionLayoutStatus::kFloat4),
                geometry_probe_.position_count(graphics::PositionLayoutStatus::kOtherEncoded),
                geometry_probe_.position_count(graphics::PositionLayoutStatus::kMissingPosition),
                geometry_probe_.position_count(graphics::PositionLayoutStatus::kMissingStream),
                geometry_probe_.position_count(graphics::PositionLayoutStatus::kUnsupportedType),
                geometry_probe_.position_count(graphics::PositionLayoutStatus::kOutOfBounds),
                geometry_probe_.position_count(graphics::PositionLayoutStatus::kNotChecked));
    REXLOG_INFO("native geometry POSITION0 access: nonindexed_window_valid={} "
                "indexed_values_needed={} backing_too_small={} index_window_too_small={} "
                "position_window_too_small={} not_checked={} "
                "(window bounds only; index values, vertex contents and world transforms unverified)",
                geometry_probe_.access_count(graphics::PositionAccessStatus::kNonIndexedRangeValid),
                geometry_probe_.access_count(graphics::PositionAccessStatus::kIndexValuesNeeded),
                geometry_probe_.access_count(graphics::PositionAccessStatus::kBackingTooSmall),
                geometry_probe_.access_count(graphics::PositionAccessStatus::kIndexWindowTooSmall),
                geometry_probe_.access_count(graphics::PositionAccessStatus::kWindowTooSmall),
                geometry_probe_.access_count(graphics::PositionAccessStatus::kNotChecked));
    REXLOG_INFO("native geometry POSITION0 values: decoded={} nonfinite={} "
                "fresh_upload_unavailable={} strip_restart={} out_of_bounds={} not_sampled={} "
                "(sampled local coordinates only from fresh complete CPU uploads; no world-space proof)",
                geometry_probe_.value_count(graphics::PositionValueStatus::kDecoded),
                geometry_probe_.value_count(graphics::PositionValueStatus::kNonFinite),
                geometry_probe_.value_count(graphics::PositionValueStatus::kUnavailableFreshUpload),
                geometry_probe_.value_count(graphics::PositionValueStatus::kStripRestart),
                geometry_probe_.value_count(graphics::PositionValueStatus::kOutOfBounds),
                geometry_probe_.value_count(graphics::PositionValueStatus::kNotSampled));
    for (size_t i = 0; i < geometry_probe_.sample_count(); ++i) {
      const auto& draw = geometry_probe_.sample(i);
      REXLOG_INFO("native geometry sample: sample={} kind={} surface_draw={} pass={} prim={} "
                  "elements={} first={} base={} vs={:016X} ps={:016X} "
                  "stream_count={} index32={} suppressed={} gpu_topology={} gpu_index_source={} "
                  "gpu_elements={} gpu_first={} gpu_index32={} gpu_expanded_vertices={} direct_input={}",
                  i, graphics::GeometrySubmissionKindName(draw.kind), draw.surface_draw,
                  draw.guest_pass, draw.primitive, draw.elements, draw.first_element,
                  draw.base_vertex, draw.vs_hash, draw.ps_hash, draw.stream_count,
                  draw.index32, draw.raster_draw_suppressed,
                  graphics::GeometryGpuTopologyName(draw.gpu_topology),
                  graphics::GeometryIndexSourceName(draw.gpu_index_source), draw.gpu_elements,
                  draw.gpu_first_element, draw.gpu_index32, draw.gpu_vertices_expanded,
                  graphics::DirectTriangleInputStatusName(graphics::ClassifyDirectTriangleInput(draw)));
      if (graphics::ClassifyDirectTriangleInput(draw) ==
          graphics::DirectTriangleInputStatus::kCandidate) {
        const auto& p = draw.position_layout;
        REXLOG_INFO("native geometry POSITION0: sample={} layout={} access={} value={} slot={} "
                    "type={:08X} offset={} stride={}",
                    i, graphics::PositionLayoutStatusName(p.status),
                    graphics::PositionAccessStatusName(draw.position_access),
                    graphics::PositionValueStatusName(draw.position_value.status), p.stream_slot,
                    p.type, p.offset, p.stride);
        if (draw.position_value.status == graphics::PositionValueStatus::kDecoded)
          REXLOG_INFO("native geometry POSITION0 host sample: sample={} vertex={} local=({:.6g},{:.6g},{:.6g})",
                      i, draw.position_value.vertex_index, draw.position_value.x,
                      draw.position_value.y, draw.position_value.z);
      }
      for (size_t stream = 0;
           stream < std::min<size_t>(draw.stream_count, draw.streams.size()); ++stream) {
        const auto& b = draw.streams[stream];
        REXLOG_INFO("native geometry buffer: sample={} stream={} slot={} decl={:08X} "
                    "key={:016X} host_gen={} addr={:08X} bytes={} backing_bytes={} stride={} offset={} "
                    "action={} version={} host_versioned={}",
                    i, stream, b.stream_slot, b.vertex_declaration, b.guest_key,
                    b.host_generation, b.guest_address, b.bytes, b.buffer_bytes,
                    b.stride, b.offset, unsigned(b.planned_action), b.content_version,
                    b.HasVersionedHostContent());
      }
      if (draw.kind == graphics::GeometrySubmissionKind::kIndexed) {
        const auto& b = draw.index;
        REXLOG_INFO("native geometry index: sample={} key={:016X} host_gen={} addr={:08X} "
                    "bytes={} backing_bytes={} stride={} action={} version={} host_versioned={}",
                    i, b.guest_key, b.host_generation, b.guest_address, b.bytes,
                    b.buffer_bytes, b.stride,
                    unsigned(b.planned_action), b.content_version,
                    b.HasVersionedHostContent());
      }
    }
    surface_probe_done_ = true;
    surface_probe_ = false;
  }
  if (temporal_probe_) {
    const auto& tf = temporal_.current();
    if (frame_count_ % 120 == 0) {
      REXLOG_INFO("native temporal probe: frame={} blocker={} camera={} prev={} scene_color={} "
                  "scene_depth={} depth_convention={} motion={} object_motion={} jitter={} hud={}",
                  tf.frame, graphics::TemporalBlockerName(temporal_.DlssBlocker()),
                  tf.camera_valid, tf.previous_camera_valid, tf.scene_color != 0,
                  tf.scene_depth != 0, tf.depth_convention_valid, tf.has_motion_vectors,
                  tf.has_object_motion, tf.jitter_valid, tf.hud_boundary_valid);
    }
    temporal_.End();
  }
  if (camera_candidate_probe_enabled_) {
    const auto& c = camera_candidate_probe_.frame();
    if (frame_count_ % 120 == 0 && c.valid > 0) {
      REXLOG_INFO("native camera candidate: frame={} guest_swap={} draws={} valid={} rejected={} "
                  "varying_draws={} in_frame_max_delta={:.6g} previous={} inter_frame_max_delta={:.6g} "
                  "c0=({:.6g},{:.6g},{:.6g},{:.6g}) c1=({:.6g},{:.6g},{:.6g},{:.6g}) "
                  "c2=({:.6g},{:.6g},{:.6g},{:.6g}) c3=({:.6g},{:.6g},{:.6g},{:.6g})",
                  c.frame, swap_number_, c.draws, c.valid, c.rejected, c.varying_draws,
                  c.max_within_frame_delta, c.has_previous, c.max_previous_frame_delta,
                  c.first[0], c.first[1], c.first[2], c.first[3],
                  c.first[4], c.first[5], c.first[6], c.first[7],
                  c.first[8], c.first[9], c.first[10], c.first[11],
                  c.first[12], c.first[13], c.first[14], c.first[15]);
    }
    camera_candidate_probe_.End();
  }
  frame_index_ = (frame_index_ + 1) % kFramesInFlight;
  ++frame_count_;
  static double last_buffer_log = 0;
  if (compat::BenchElapsedMs() - last_buffer_log > 2000) {
    last_buffer_log = compat::BenchElapsedMs();
    REXLOG_INFO("native: buffers uploads {} partial {} ({} MB) invalidations {} ({} MB) live {}",
                stats_.buffer_uploads, stats_.buffer_partial_uploads,
                stats_.buffer_upload_bytes >> 20, front_invalidations_.load(),
                front_invalidated_bytes_.load() >> 20, buffers_.size());
    std::string per_pass;
    for (int p = 0; p < 32; ++p) {
      if (stats_.pass_draws[p] || stats_.pass_skips[p]) {
        per_pass += fmt::format(" p{}:{}/{}{}", p, stats_.pass_draws[p], stats_.pass_skips[p],
                                stats_.pass_last_skip[p] ? stats_.pass_last_skip[p] : "");
      }
    }
    REXLOG_INFO("native: t={:.0f}s swap {} per pass drawn/skipped:{}",
                compat::BenchElapsedMs() / 1000, swap_number_, per_pass);
    {
      std::string at;
      for (int p = 0; p < 32; ++p) {
        if (stats_.a2c_draws[p] || stats_.alpha_test_draws[p]) {
          at += fmt::format(" p{}:{}/{}", p, stats_.a2c_draws[p], stats_.alpha_test_draws[p]);
        }
      }
      if (!at.empty()) REXLOG_INFO("native: alpha test as coverage / hard per pass:{}", at);
    }
    {
      static Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
      if (!adapter) {
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
          factory->EnumAdapterByLuid(device_->GetAdapterLuid(), IID_PPV_ARGS(&adapter));
        }
      }
      DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
      if (adapter) {
        adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
        adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal);
      }
      REXLOG_INFO("native: vram {} MB / budget {} MB, sysmem {} MB | textures {} resolves {} "
                  "surfaces {} buffers {} retired {} srv_next {} free_srvs {} rtv_next {} upload_peak {} KB "
                  "overflow_pages {}",
                  local.CurrentUsage >> 20, local.Budget >> 20, nonlocal.CurrentUsage >> 20,
                  textures_.size(), resolve_textures_.size(), surfaces_.size(), buffers_.size(),
                  retired_.size(), srv_heap_next_, retired_srvs_.size(), rtv_heap_next_,
                  upload_peak_ >> 10, overflow_pages_created_);
    }
    REXLOG_INFO("native: EDRAM reinterpretations {} over {} frames", stats_.edram_reinterprets, frame_count_);
    REXLOG_INFO("native: buffers made dirty by the write watch {} / by the per-frame hash {}",
                stats_.buffer_watch_dirty, stats_.buffer_hash_dirty);
    REXLOG_INFO("native: gpu fence wait {} ms total, frames {}", stats_.gpu_wait_us / 1000,
                frame_count_);
    REXLOG_INFO("native: pm4 mirror packets {} dwords {} indirect {} ({} dwords) resyncs {} "
                "unreadable ALU loads {}",
                mirror_.packets, mirror_.dwords, mirror_.indirect_buffers,
                mirror_.indirect_dwords, ring_resyncs_, mirror_.unreadable_alu_loads);
  }
  if ((frame_count_ % 600) == 0) {
    REXLOG_INFO(
        "native: frame {} draws={} skipped={} (shader {} pso {} prim {} rt {} vb {}) resolves={} "
        "(textures created {}) psos={} textures={} buffers={}",
        frame_count_, stats_.draws, stats_.draws_skipped, stats_.skip_shader, stats_.skip_pso,
        stats_.skip_prim, stats_.skip_rt, stats_.skip_vb, stats_.resolves,
        stats_.resolve_textures_created, stats_.pso_created,
        stats_.textures_created, buffers_.size());
    std::string per_pass;
    for (int p = 0; p < 32; ++p) {
      if (stats_.pass_draws[p] || stats_.pass_skips[p]) {
        per_pass += fmt::format(" p{}:{}/{}{}", p, stats_.pass_draws[p], stats_.pass_skips[p],
                                stats_.pass_last_skip[p] ? stats_.pass_last_skip[p] : "");
      }
    }
    REXLOG_INFO("native: per pass drawn/skipped:{}", per_pass);
    // Which (vs, ps) pairs account for the pipeline skips.
    std::vector<std::pair<uint64_t, std::tuple<uint64_t, uint64_t, int>>> top;
    for (const auto& [k, n] : stats_.skip_pso_pairs) top.emplace_back(n, k);
    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < top.size() && i < 12; ++i) {
      const auto& [vs, ps, pass] = top[i].second;
      REXLOG_INFO("native: pso skips {} vs={:016X} ps={:016X} pass {}", top[i].first, vs, ps, pass);
    }
  }
}

void Renderer::ExecOnPassEnd(int pass) {
  // sr_native_dump_after_pass=99: dump the surfaces after every pass of one frame
  // (starting at the shadow pass), files prefixed with the pass number.
  bool all = REXCVAR_GET(sr_native_dump_after_pass) == 99;
  if (all && !frame_dump_done_ && REXCVAR_GET(sr_native_dump_frame_at_s) > 0 &&
      compat::BenchElapsedMs() >= REXCVAR_GET(sr_native_dump_frame_at_s) * 1000.0 &&
      (dump_all_active_ || pass == 4)) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!frame_open_) return;
    dump_all_active_ = true;
    command_list_->Close();
    ID3D12CommandList* lists[] = {command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    queue_->Signal(fence_.Get(), next_fence_value_++);
    DumpFrameResources(fmt::format("p{:02}_", pass), true);
    command_list_->Reset(allocators_[frame_index_].Get(), nullptr);
    frame_state_bound_ = false;
    fixed_state_cached_ = false;
    bound_vertex_views_mask_ = 0;
    bound_rt_count_ = 0;
    bound_ds_ = nullptr;
    std::memset(bound_rts_, 0, sizeof(bound_rts_));
    return;
  }
  if (all) return;
  // The draw/VS/resolve/watch triggers own the dump of their frame.
  if (REXCVAR_GET(sr_native_dump_before_draw) >= 0 || !REXCVAR_GET(sr_native_dump_vs).empty() ||
      REXCVAR_GET(sr_native_dump_before_resolve) >= 0 || REXCVAR_GET(sr_native_watch_blue)) {
    return;
  }
  if (frame_dump_done_ || pass != REXCVAR_GET(sr_native_dump_after_pass) ||
      REXCVAR_GET(sr_native_dump_frame_at_s) <= 0 ||
      compat::BenchElapsedMs() < REXCVAR_GET(sr_native_dump_frame_at_s) * 1000.0) {
    return;
  }
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!frame_open_) return;
  frame_dump_done_ = true;
  // Submit what the frame recorded so far, dump, then continue recording.
  command_list_->Close();
  ID3D12CommandList* lists[] = {command_list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  queue_->Signal(fence_.Get(), next_fence_value_++);
  DumpFrameResources();
  command_list_->Reset(allocators_[frame_index_].Get(), nullptr);
  frame_state_bound_ = false;
  fixed_state_cached_ = false;
  bound_vertex_views_mask_ = 0;
  bound_rt_count_ = 0;
  bound_ds_ = nullptr;
  std::memset(bound_rts_, 0, sizeof(bound_rts_));
  REXLOG_INFO("native: dumped after pass {}", pass);
}

static float HalfToFloat(uint16_t v) {
  uint32_t sign = v >> 15, e = (v >> 10) & 31, m = v & 1023;
  float f = e == 31 ? 65504.0f : e ? std::ldexp(float(m | 1024), int(e) - 25) : std::ldexp(float(m), -24);
  return sign ? -f : f;
}

// Debug (sr_native_watch_blue): submits the frame so far, reads the HDR target
// back and counts the "blue dash" pixels of its lower part; the draw recorded
// just before this call (current_vs_hash_/current_ps_hash_) is blamed when the
// count changed.
void Renderer::WatchHdr(uint32_t index) {
  ID3D12Resource* res = nullptr;
  D3D12_RESOURCE_STATES* state = nullptr;
  for (auto& [key, surf] : surfaces_) {
    if (surf.depth || surf.samples > 1 || surf.width != 1280 || surf.height != 720) continue;
    if (surf.resource->GetDesc().Format != DXGI_FORMAT_R16G16B16A16_FLOAT) continue;
    res = surf.resource.Get();
    state = &surf.state;
    break;
  }
  if (!res) return;
  for (auto& retired : retired_srvs_) {
    for (const auto& meta : vertex_fetch_) {
      if (meta[2] && meta[0] == retired.second) {
        retired.first = UINT64_MAX;
        break;
      }
    }
  }
  auto order_srv_leases = [&] {
    std::stable_sort(retired_srvs_.begin(), retired_srvs_.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
  };
  order_srv_leases();
  command_list_->Close();
  ID3D12CommandList* lists[] = {command_list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  queue_->Signal(fence_.Get(), next_fence_value_);
  fence_->SetEventOnCompletion(next_fence_value_, fence_event_);
  ++next_fence_value_;
  WaitForSingleObject(fence_event_, 5000);
  D3D12_RESOURCE_DESC desc = res->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
  UINT rows;
  UINT64 row_size, total;
  device_->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &rows, &row_size, &total);
  if (!watch_readback_ || watch_total_ < total) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buf{};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Width = total;
    buf.Height = 1;
    buf.DepthOrArraySize = 1;
    buf.MipLevels = 1;
    buf.SampleDesc.Count = 1;
    buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    watch_readback_.Reset();
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buf,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&watch_readback_)))) {
      watch_done_ = true;
      return;
    }
    watch_total_ = total;
  }
  allocators_[frame_index_]->Reset();
  command_list_->Reset(allocators_[frame_index_].Get(), nullptr);
  D3D12_RESOURCE_STATES old_state = *state;
  Transition(res, *state, D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
  src.pResource = res;
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.pResource = watch_readback_.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = fp;
  command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  Transition(res, *state, old_state);
  command_list_->Close();
  ID3D12CommandList* copy_lists[] = {command_list_.Get()};
  queue_->ExecuteCommandLists(1, copy_lists);
  queue_->Signal(fence_.Get(), next_fence_value_);
  fence_->SetEventOnCompletion(next_fence_value_, fence_event_);
  ++next_fence_value_;
  WaitForSingleObject(fence_event_, 5000);
  int count = 0;
  int first_x = -1, first_y = -1;
  float probe[4] = {};
  int sentinel = 0;
  void* data = nullptr;
  D3D12_RANGE range{0, size_t(total)};
  if (SUCCEEDED(watch_readback_->Map(0, &range, &data))) {
    for (uint32_t y = 400; y < desc.Height; ++y) {
      const uint16_t* row = reinterpret_cast<const uint16_t*>(
          static_cast<uint8_t*>(data) + fp.Offset + size_t(y) * fp.Footprint.RowPitch);
      for (uint32_t x = 0; x < desc.Width; ++x) {
        float b = HalfToFloat(row[4 * x + 2]);
        if (b < 2.0f || b > 29.5f) continue;
        float r = HalfToFloat(row[4 * x]), g = HalfToFloat(row[4 * x + 1]);
        if (b > 8.0f * std::max(r, g)) {
          if (!count++) {
            first_x = int(x);
            first_y = int(y);
          }
        }
      }
    }
    for (uint32_t y = 0; y < desc.Height; ++y) {
      const uint16_t* row = reinterpret_cast<const uint16_t*>(
          static_cast<uint8_t*>(data) + fp.Offset + size_t(y) * fp.Footprint.RowPitch);
      for (uint32_t x = 0; x < desc.Width; ++x) {
        if (row[4 * x + 2] == 0x4F80 && row[4 * x] == 0 && row[4 * x + 1] == 0) ++sentinel;
      }
    }
    int px = 640, py = 50;
    std::sscanf(REXCVAR_GET(sr_native_watch_xy).c_str(), "%d,%d", &px, &py);
    if (px >= 0 && py >= 0 && uint32_t(px) < desc.Width && uint32_t(py) < desc.Height) {
      const uint16_t* row = reinterpret_cast<const uint16_t*>(
          static_cast<uint8_t*>(data) + fp.Offset + size_t(py) * fp.Footprint.RowPitch);
      for (int k = 0; k < 4; ++k) probe[k] = HalfToFloat(row[4 * px + k]);
    }
    watch_readback_->Unmap(0, nullptr);
  }
  const bool probe_changed = std::memcmp(probe, watch_prev_probe_, sizeof(probe)) != 0;
  if (count != watch_prev_count_ || probe_changed || sentinel != watch_prev_sentinel_) {
    REXLOG_INFO("native watch: before draw {} sentinel px {} -> {} dashes {} -> {} (first {},{}) probe "
                "({:.3g} {:.3g} {:.3g} {:.3g}) -> ({:.3g} {:.3g} {:.3g} {:.3g}) ops since last read:{} | "
                "previous draw vs {:016X} ps {:016X}", index, watch_prev_sentinel_, sentinel,
                watch_prev_count_, count, first_x, first_y, watch_prev_probe_[0], watch_prev_probe_[1],
                watch_prev_probe_[2], watch_prev_probe_[3], probe[0], probe[1], probe[2], probe[3],
                watch_ops_, current_vs_hash_, current_ps_hash_);
    watch_prev_count_ = count;
    watch_prev_sentinel_ = sentinel;
    std::memcpy(watch_prev_probe_, probe, sizeof(probe));
  }
  watch_ops_.clear();
  for (auto& retired : retired_srvs_)
    if (retired.first == UINT64_MAX) retired.first = next_fence_value_;
  order_srv_leases();
  command_list_->Reset(allocators_[frame_index_].Get(), nullptr);
  command_list_->IASetVertexBuffers(0, 16, guest_vertex_views_);
  bound_vertex_views_mask_ = 0xFFFFu;
  frame_state_bound_ = false;
  fixed_state_cached_ = false;
  bound_rt_count_ = 0;
  bound_ds_ = nullptr;
  std::memset(bound_rts_, 0, sizeof(bound_rts_));
}

void Renderer::DumpFrameResources(const std::string& prefix, bool surfaces_only,
                                  bool output_only) {
  std::string dir = REXCVAR_GET(sr_native_dump_dir);
  if (dir.empty()) return;
  std::error_code directory_error;
  std::filesystem::create_directories(dir, directory_error);
  if (directory_error) {
    REXLOG_ERROR("native: cannot create dump directory {}: {}", dir, directory_error.message());
    return;
  }
  uint64_t last = next_fence_value_ - 1;
  if (fence_->GetCompletedValue() < last) {
    fence_->SetEventOnCompletion(last, fence_event_);
    WaitForSingleObject(fence_event_, 5000);
  }
  struct Item {
    std::string name;
    ID3D12Resource* resource;
    D3D12_RESOURCE_STATES* state;
  };
  std::vector<Item> items;
  for (auto& [key, surf] : surfaces_) {
    if (output_only) break;
    if (surf.samples > 1) continue;  // multisampled: not copyable to a buffer
    char n[96];
    std::snprintf(n, sizeof(n), "surf_%08X_%ux%u_%s", uint32_t(key >> 32), surf.width,
                  surf.height, surf.depth ? "depth" : "color");
    items.push_back({n, surf.resource.Get(), &surf.state});
  }
  if (!surfaces_only && output_rts_[frame_index_]) {
    items.push_back({"output_1280x720", output_rts_[frame_index_].Get(),
                     &output_rt_states_[frame_index_]});
  }
  if (!surfaces_only && !output_only && ssao_ao_) {
    items.push_back({"ssao_ao", ssao_ao_.Get(), &ssao_ao_state_});
  }
  if (REXCVAR_GET(sr_native_debug_texture_format) >= 0 && !surfaces_only) {
    for (auto& [key, te] : textures_) {
      if (!te.resource || te.guest_format != uint32_t(REXCVAR_GET(sr_native_debug_texture_format))) continue;
      D3D12_RESOURCE_DESC rd = te.resource->GetDesc();
      if (rd.Width > 512 || rd.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) continue;
      char n[96];
      std::snprintf(n, sizeof(n), "tex_%08X_%llux%u", te.guest_base, (unsigned long long)rd.Width,
                    rd.Height);
      te.debug_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      items.push_back({n, te.resource.Get(), &te.debug_state});
    }
  }
  for (auto& [key, rt] : resolve_textures_) {
    if (surfaces_only || output_only) break;
    char n[96];
    std::snprintf(n, sizeof(n), "resolve_%08X_f%u_%ux%u_id%llX", GuestPhysical(rt.guest_fetch[1]&0xFFFFF000u),
                  uint32_t(rt.format),rt.width, rt.height,static_cast<unsigned long long>(key));
    items.push_back({n, rt.resource.Get(), &rt.state});
  }
  uint32_t written = 0;
  for (Item& item : items) {
    D3D12_RESOURCE_DESC desc = item.resource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    UINT rows;
    UINT64 row_size, total;
    device_->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &rows, &row_size, &total);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buf{};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Width = total;
    buf.Height = 1;
    buf.DepthOrArraySize = 1;
    buf.MipLevels = 1;
    buf.SampleDesc.Count = 1;
    buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buf,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&readback)))) {
      continue;
    }
    allocators_[frame_index_]->Reset();
    command_list_->Reset(allocators_[frame_index_].Get(), nullptr);
    D3D12_RESOURCE_STATES old_state = *item.state;
    Transition(item.resource, *item.state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = item.resource;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = fp;
    command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(item.resource, *item.state, old_state);
    command_list_->Close();
    ID3D12CommandList* lists[] = {command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    queue_->Signal(fence_.Get(), next_fence_value_);
    fence_->SetEventOnCompletion(next_fence_value_, fence_event_);
    ++next_fence_value_;
    WaitForSingleObject(fence_event_, 5000);
    void* data = nullptr;
    D3D12_RANGE range{0, size_t(total)};
    if (SUCCEEDED(readback->Map(0, &range, &data))) {
      std::ofstream out(std::filesystem::u8path(dir)/(prefix+item.name+".raw"),std::ios::binary);
      if (out) {
        uint32_t header[4] = {uint32_t(desc.Width), desc.Height, uint32_t(desc.Format),
                              fp.Footprint.RowPitch};
        out.write(reinterpret_cast<const char*>(header),sizeof(header));
        const bool valid_rows=row_size<=UINT32_MAX && graphics::WritePaddedReadbackRows(out,
            {static_cast<const uint8_t*>(data),size_t(total)},size_t(fp.Offset),
            uint32_t(row_size),fp.Footprint.RowPitch,rows);
        out.close();
        if(valid_rows && out) ++written;
        else REXLOG_ERROR("native: incomplete dump {}/{}{}.raw",dir,prefix,item.name);
      } else {
        REXLOG_ERROR("native: cannot write dump {}/{}{}.raw", dir, prefix, item.name);
      }
      readback->Unmap(0, nullptr);
    }
  }
  REXLOG_INFO("native: dumped {}/{} frame resources to {}", written, items.size(), dir);
}

// ---------------------------------------------------------------------------
// Front end (guest threads): capture + queue. See sr_native_worker.
// ---------------------------------------------------------------------------

void Renderer::BeginCmd(Op op) {
  DetailScope scope(capture_timings.fe_begin_ns);
  if(CpuProfiling()) front_cpu_timings.capture_start=std::chrono::steady_clock::now();
  if (!worker_mode_checked_ && !worker_stop_) {
    // This frontend is shared by D3D12 and the Vulkan packet sink. Initialize
    // before capturing buffers/textures, not in the D3D12-only BeginFrame.
    // BeginCmd callers hold front_mutex_, and the worker starts only below.
    if (REXCVAR_GET(sr_native_texture_watch)) {
      page_write_seq_ = std::make_unique<std::atomic<uint32_t>[]>(0x20000);
      for (uint32_t p = 0; p < 0x20000; ++p) page_write_seq_[p].store(0);
      auto handle = REX_KERNEL_MEMORY()->RegisterPhysicalMemoryInvalidationCallback(
          &Renderer::OnPhysicalWrite, this);
      texture_watch_ = handle != nullptr;
      REXLOG_INFO("native: guest physical memory write watch {} (api={})",
          texture_watch_ ? "enabled" : "unavailable", packet_sink_ ? "vulkan" : "d3d12");
    }
    worker_mode_checked_ = true;
    worker_mode_ = bool(packet_sink_) || (REXCVAR_GET(sr_native_worker) && REXCVAR_GET(sr_native_pm4_mirror));
    if (worker_mode_) {
      worker_=std::thread(&Renderer::WorkerMain, this);
      REXLOG_INFO("native: recording worker enabled");
    }
  }
  if (!batch_) batch_ = std::make_unique<WorkBatch>();
  cur_ = WorkCmd{};
  cur_.op = op;
  if(op==Op::kDraw || op==Op::kDrawIndexed || op==Op::kDrawInline)
    cur_.observed_render=CaptureInstanceProbeInvocation(guest_base_);
  cur_.command_serial = ++capture_serial_;
  cur_.tiling_active = capture_tiling_active_;
  cur_.packet_check = (!worker_stop_ && bool(packet_sink_)) || (REXCVAR_GET(sr_native_packet_check) && graphics::guest::IsCaptureFrame(
      front_frame_, uint32_t(std::max(1,REXCVAR_GET(sr_native_packet_check_every)))));
  cur_.pass = g_guest_pass;
  cur_.range_first = uint32_t(batch_->ranges.size());
  cur_.stream_first = uint32_t(batch_->streams.size());
}

void Renderer::EndCmd(uint8_t* base) {
  DetailScope scope(capture_timings.fe_end_ns);
  if(worker_stop_) return;
  checked_guest_reads.Reset();
  const auto capture_copies_before=capture_mirror_.copy_draws;
  if (cur_.packet_check && cur_.ring_bytes) {
    const auto started=std::chrono::steady_clock::now();
    std::string capture_error;
    DetailScope pm4_scope(capture_timings.fe_pm4_ns);
    graphics::guest::Pm4CaptureProbe pm4_probe;
    cur_.pm4_capture_ok = graphics::guest::CapturePm4Dependencies(*batch_, cur_,
        [base](uint32_t address, uint32_t length) {
          return ReadCommittedGuest(base,address,length);
        }, capture_error, &capture_mirror_, graphics::FrameTimeline::Detail() ? &pm4_probe : nullptr);
    capture_timings.pm4_us+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
    pm4_scope.Stop();
    if (graphics::FrameTimeline::Detail()) {
      capture_timings.fp_primary_ns += pm4_probe.primary_ns;
      capture_timings.fp_read_ns += pm4_probe.read_ns;
      capture_timings.fp_copy_ns += pm4_probe.copy_ns;
      stream_plan_counters.pm4_reads += pm4_probe.reads;
      stream_plan_counters.pm4_bytes += pm4_probe.bytes;
    }
    if (!cur_.pm4_capture_ok)
      REXLOG_WARN("native PM4 capture: {}", capture_error);
  }
  if (REXCVAR_GET(sr_native_packet_check) && !cur_.packet_check && cur_.ring_bytes) {
    // Keep the same frontend parser current between sampled frames. Only a
    // selected diagnostic frame freezes referenced bytes for packet decoding.
    capture_mirror_.ScanCopyUsing(batch_->bytes.data()+cur_.ring_offset,cur_.ring_bytes,
        [base](uint32_t address,uint32_t length) {return ReadCommittedGuest(base,address,length);});
  }
  if (cur_.packet_check && cur_.device &&
      (cur_.op==Op::kDraw || cur_.op==Op::kDrawIndexed || cur_.op==Op::kDrawInline)) {
    const auto started=std::chrono::steady_clock::now();
    DetailScope textures_scope(capture_timings.fe_textures_ns);
    CaptureTextures(base);
    capture_timings.textures_us+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
    textures_scope.Stop();
  }
  if(cur_.op==Op::kResolve && cur_.packet_check) {
    cur_.resolve_copy_draw=capture_mirror_.copy_draws!=capture_copies_before;
    cur_.resolve_copy_dest_info=capture_mirror_.last_copy_dest_info;
  }
  cur_.range_count = uint32_t(batch_->ranges.size()) - cur_.range_first;
  cur_.stream_count = uint32_t(batch_->streams.size()) - cur_.stream_first;
  if(cur_.op==Op::kBeginTiling) capture_tiling_active_=true;
  if(cur_.op==Op::kEndTiling) capture_tiling_active_=false;
  // The completed command belongs to the batch. Avoid copying its texture and
  // shader references (and diagnostic strings) for every draw on both APIs.
  {
    DetailScope push_scope(capture_timings.fe_push_ns);
    batch_->cmds.push_back(std::move(cur_));
  }
  if(CpuProfiling()) front_cpu_timings.capture_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-front_cpu_timings.capture_start).count());
  if (!worker_mode_) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Execute(base, *batch_, batch_->cmds.back());
    batch_->Clear();
    return;
  }
  // Coarse batches amortize replay-worker dispatch. Keep byte-pressure and
  // explicit swap flushes unchanged; the original 32-command path is default.
  static const size_t vulkan_batch_limit=[] {
    const char* flag=std::getenv("SR_NATIVE_BATCH_COMMANDS");
    if(flag && !std::strcmp(flag,"128"))return size_t(128);
    if(flag && !std::strcmp(flag,"256"))return size_t(256);
    return size_t(32);
  }();
  if (batch_->cmds.size() >= (packet_sink_?vulkan_batch_limit:32) || batch_->bytes.size() >= (4u << 20)) {
    FlushBatch();
  }
}

void Renderer::CaptureBytes(uint8_t* base, uint32_t address, uint32_t length) {
  if (!length) return;
  size_t offset = batch_->bytes.size();
  // Appending the source range constructs the new bytes directly. resize()
  // value-initializes (zeros) them before the memcpy, doubling the writes.
  batch_->bytes.insert(batch_->bytes.end(), base + address, base + address + length);
  if (batch_->ranges.size() > cur_.range_first) {
    CaptureRange& last = batch_->ranges.back();
    if (uint64_t(last.address) + last.length == address &&
        size_t(last.offset) + last.length == offset) {
      last.length += length;
      return;
    }
  }
  batch_->ranges.push_back({address, length, uint32_t(offset)});
}

void Renderer::CaptureTextures(uint8_t* base) {
  const bool replay_textures = TextureHashReplayEnabled();
  for (uint32_t slot=0;slot<32;++slot) {
    std::array<uint32_t,6> fetch{};
    const uint32_t first_reg=Pm4Mirror::kFetchConstantBase+slot*6;
    fetch[0]=capture_mirror_.written(first_reg) ? capture_mirror_.reg(first_reg)
        : Load32(base,cur_.device+kDev.fetch_constants+slot*24);
    if(!IsTextureBound(fetch[0])) continue;
    for(uint32_t i=1;i<6;++i) {
      uint32_t reg=Pm4Mirror::kFetchConstantBase+slot*6+i;
      fetch[i]=capture_mirror_.written(reg) ? capture_mirror_.reg(reg)
          : Load32(base,cur_.device+kDev.fetch_constants+slot*24+i*4);
    }
    auto& entry=TextureSlotLookupCaching()?CapturedTexture(slot,fetch):captured_textures_[fetch];
    if(!entry.snapshot && entry.failed_frame!=~0ull && front_frame_<entry.failed_frame+16) {cur_.texture_errors.emplace_back(slot,entry.failure);continue;}
    bool dirty=!entry.snapshot;
    bool notified=false;
    if(dirty) ++capture_timings.new_entries;
    if(entry.snapshot) {
      if(texture_watch_) {
        const auto scan=[&] {
          return TextureContentWritten(entry.snapshot->ranges,entry.watch_seq,
              [this](uint32_t address,uint32_t size,uint32_t seq) {
                return TextureWrittenSince(address,size,seq);
              });
        };
        bool reused=false;
        const bool written=TextureWatchScanCaching()
            ? entry.watch_scan.Written(front_frame_,completed_texture_writes_.load(std::memory_order_acquire),
                                       entry.watch_seq,scan,reused)
            : scan();
        if(reused) ++capture_timings.watch_scan_hits; else ++capture_timings.watch_scan_misses;
        if(written) dirty=notified=true;
      }
      if(dirty) ++capture_timings.watch_dirty;
      if(entry.checked_frame!=front_frame_) {
        uint64_t total=0;
        for(const auto& range:entry.snapshot->ranges) total+=range.length;
        // Verify dynamic movie and UI textures (up to 4MB). Guest virtual writes
        // (e.g. video decoder YUV planes) bypass physical memory write watches.
        // Re-reading every texture every frame cost ~180 MB/57 ms per frame, so
        // with the watch active, textures found unchanged are checked with an
        // exponential backoff (2..16 frames); changing ones stay per frame.
        if(!texture_watch_) dirty=true;
        else if(total<=4096u*1024 && front_frame_>=entry.next_check_frame) {if(!dirty) ++capture_timings.revalidated;dirty=true;}
      }
    }
    entry.checked_frame=front_frame_;
    if(dirty) {
      std::string error;
      std::vector<graphics::guest::TextureRange> ranges;
      if(!graphics::guest::DescribeTextureRanges(fetch,ranges,error)) {
        entry.failed_frame=front_frame_;entry.failure=error;
        cur_.texture_errors.emplace_back(slot,std::move(error));
        continue;
      }
      uint64_t hash=0xcbf29ce484222325ull;
      const bool probe = packet_sink_ && TextureSourceProbing() &&
          SelectTextureSourceProbe("vulkan", front_frame_, TextureContentBytes(ranges));
      double probe_ns = 0;
      entry.watch_seq=write_seq_.load(std::memory_order_acquire);
      for(const auto& range:ranges) {
        auto started=std::chrono::steady_clock::now();
        if(texture_watch_) ArmTextureWatch(range.address,range.length);
        capture_timings.watch_us+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
        // Hash the guest bytes where they are. Copying ~100 MB per frame only to learn that nothing
        // changed cost ~30 ms of the guest thread; the copy now happens only for a changed texture.
        ++capture_timings.reads;capture_timings.bytes+=range.length;
        started=std::chrono::steady_clock::now();
        const bool hashed=HashGuestRange(GuestSource(base,0xa0000000u+range.address,range.length),range.length,hash,
            TextureHash(),hash);
        const auto elapsed = std::chrono::steady_clock::now()-started;
        capture_timings.hash_us+=std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        if (probe) probe_ns += std::chrono::duration<double, std::nano>(elapsed).count();
        if(!hashed) {error="Texture memory is not readable";break;}
      }
      if(!error.empty()) {
        if (probe) ++texture_source_probe.unreadable;
        entry.failed_frame=front_frame_;entry.failure=error;cur_.texture_errors.emplace_back(slot,std::move(error));continue;
      }
      if (probe) MeasureTextureSource(ranges,
          [base](uint32_t address, uint32_t length) { return GuestSource(base, 0xa0000000u + address, length); },
          0xcbf29ce484222325ull, hash, probe_ns);
      const bool unchanged=entry.snapshot && hash==entry.content_hash;
      if(TextureAuditing() && entry.snapshot) {
        ObserveTextureValidationForUnlockAudit(fetch.data(),ranges);
        uint64_t bytes=0;
        for(const auto& range:ranges) {
          bytes+=range.length;
          if(texture_watch_ && TextureWrittenSince(range.address,range.length,entry.watch_seq)) notified=true;
        }
        AuditTextureHash(packet_sink_?"vulkan":"d3d12_capture",front_frame_,
            fetch[1]&0x1ffff000u,bytes,fetch[1]&0x3fu,!unchanged,notified,texture_watch_);
      }
      entry.stable_checks=unchanged?std::min(entry.stable_checks+1,4u):0;
      entry.next_check_frame=front_frame_+(uint64_t(1)<<entry.stable_checks);
      if(!unchanged) ++capture_timings.changed;
      if(!unchanged) {
        const auto started=std::chrono::steady_clock::now();
        if(!graphics::guest::CaptureTexture(fetch,cur_.command_serial,
            [base](uint32_t address,uint32_t length){
              return ReadCommittedGuest(base,address,length);
            },entry.snapshot,error)) {
          entry.failed_frame=front_frame_;entry.failure=error;
          cur_.texture_errors.emplace_back(slot,std::move(error));
          continue;
        }
        // Hash exactly the owned bytes, rather than a preceding live read.
        // A loader may write between the change check and the snapshot copy.
        hash=0xcbf29ce484222325ull;
        for(const auto& range:entry.snapshot->ranges) {
          auto bytes=entry.snapshot->memory.Read(0xa0000000u+range.address,range.length);
          hash=TextureHash()(bytes.data(),bytes.size(),hash);
        }
        entry.content_hash=hash;
        capture_timings.copy_us+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
      }
    }
    if (packet_sink_ && entry.snapshot && replay_textures)
      MaybeReplayTextureHash(fetch, front_frame_, "vulkan", entry.snapshot);
    cur_.textures[slot]=entry.snapshot;
  }
  // Commands retain ownership even when the frontend cache is pruned.
  if(captured_textures_.size()>2048) {
    InvalidateTextureSlotLookups();
    for(auto it=captured_textures_.begin();it!=captured_textures_.end();) {
      if(it->second.checked_frame+120<front_frame_) it=captured_textures_.erase(it);
      else ++it;
    }
  }
}

// Device state the worker reads for a draw/resolve (Load32 through captures):
// device pointer, vertex fetch shadow, register shadow, vertex declaration
// pointer, surface/shader/stream/index pointers and viewport, and the objects
// they point to.
void Renderer::CaptureDevice(uint8_t* base, uint32_t dev) {
  DetailScope scope(capture_timings.fe_device_ns);
  cur_.device = dev;
  if (cur_.packet_check) {
    CaptureBytes(base, dev + kDev.fetch_constants, 32 * 24);
    CaptureBytes(base, dev + kDev.vs_bools, kDev.register_shadow-kDev.vs_bools);
    for (const auto& range : profile::kRegisterShadow)
      CaptureBytes(base, dev + range.offset, range.count * 4);
    uint32_t vs = Load32(base, dev + kDev.shader_a), ps = Load32(base, dev + kDev.shader_b);
    {
      DetailScope shader_scope(capture_timings.fd_shaders_ns);
      TryRegisterInlineShaders(base, vs, ps);
      cur_.vertex_shader = CaptureGuestShader(vs);
      cur_.pixel_shader = CaptureGuestShader(ps);
    }
  }
  if (profile::kDevicePtrAddr) CaptureBytes(base, profile::kDevicePtrAddr, 4);
  CaptureBytes(base, dev, 8);
  CaptureBytes(base, dev + kDev.fetch_constants + 0x280, 0x80);  // vertex fetch slots
  CaptureBytes(base, dev + kDev.register_shadow, 0x1D0);
  CaptureBytes(base, dev + kDev.vertex_decl - 4, 8);
  CaptureBytes(base, dev + kDev.index_buffer - 0xC, 0x120);  // pointers .. viewport
  for (uint32_t i = 0; i < 4; ++i) {
    if (uint32_t rt = Load32(base, dev + kDevRenderTargets + 4 * i)) CaptureBytes(base, rt, 0x28);
  }
  if (uint32_t ds = Load32(base, dev + kDevDepthStencil)) CaptureBytes(base, ds, 0x28);
  if (uint32_t decl = Load32(base, dev + kDevVertexDecl)) {
    uint32_t n = std::min(Load32(base, decl + 0x18), 64u);
    CaptureBytes(base, decl, 0x34 + 12 * n);
  }
}

// PM4 written since the last capture, copied with the command (parsed by
// Execute; direct mode executes the same captured commands synchronously).
void Renderer::CaptureRing(uint8_t* base, uint32_t dev) {
  DetailScope scope(capture_timings.fe_ring_ns);
  if (!dev) return;
  // dev+48: last dword written into the current XDK command segment.
  uint32_t current = Load32(base, dev + kDev.ring_write) + 4;
  if (ring_last_ && current >= ring_last_ && current - ring_last_ <= (1u << 20)) {
    uint32_t n = current - ring_last_;
    if (!cur_.ring_bytes) cur_.ring_offset = uint32_t(batch_->bytes.size());
    if (cur_.ring_offset + cur_.ring_bytes == batch_->bytes.size()) {
      // Append directly, avoiding a zero-fill pass over every captured PM4 command.
      batch_->bytes.insert(batch_->bytes.end(), base + ring_last_, base + ring_last_ + n);
      cur_.ring_bytes += n;
    }
  } else if (ring_last_) {
    ++ring_resyncs_;
  }
  ring_last_ = current;
}

void Renderer::FlushBatch() {
  DetailScope scope(capture_timings.fe_flush_ns);
  if (!batch_ || batch_->cmds.empty()) return;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    work_queue_.push_back(std::move(batch_));
    ++batches_submitted_;
  }
  queue_cv_.notify_one();
  batch_ = batch_pool_->Acquire();
}

// Waits until the worker executed the first `batches` batches.
void Renderer::WaitWorkerIdle(uint64_t batches) {
  std::unique_lock<std::mutex> lock(queue_mutex_);
  done_cv_.wait(lock, [this, batches] { return worker_stop_ || batches_done_ >= batches; });
}

namespace {
// SR_VULKAN_PROFILE=1: worker-side split of the packet sink path, per 120 swaps.
struct SinkProfile {
  graphics::guest::ReplayProbe replay;
  uint64_t arena_setup_us=0;
  uint64_t parallel_batches=0,parallel_decode_cpu_ns=0,parallel_arena_cpu_ns=0,parallel_prepare_ns=0;
  uint64_t swaps=0,packets=0,replay_us=0,sink_us=0,swap_us=0,idle_us=0;
  uint64_t draws=0,vs_same=0,ps_same=0,last_vs=~0ull,last_ps=~0ull;
};
SinkProfile g_sink_profile;
bool SinkProfiling() {static const bool on=[]{const char* v=std::getenv("SR_VULKAN_PROFILE");return v && *v && *v!='0';}();return on;}
// The decoder fully assigns these banks before use. Keep an explicit off
// switch for comparisons against the former eager initialization path.
bool ReplayStorageEnabled() {static const bool on=[]{const char* v=std::getenv("SR_NATIVE_REPLAY_STORAGE");return !v || !*v || *v!='0';}();return on;}
uint64_t ElapsedUs(std::chrono::steady_clock::time_point since) {return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-since).count());}
thread_local graphics::guest::ReplayBatchSample* replay_trace_sample=nullptr;
}

void Renderer::WorkerMain() {
  compat::RegisterSampledThread(4, "sr_native_worker");
  // Experimental A/B switch: retain only retired CPU packet backing memory.
  // Aliases own the pool, so recorder teardown may safely finish after us.
  const char* pool_flag=std::getenv("SR_NATIVE_REPLAY_POOL");
  auto payload_pool=pool_flag && *pool_flag && *pool_flag!='0'
      ?std::make_shared<graphics::guest::DrawPacketPool>():nullptr;
  REXLOG_INFO("native renderer: replay packet pool {}",payload_pool?"enabled (idle limit 128 MiB / 128 blocks)":"disabled");
  const char* threads_flag=std::getenv("SR_NATIVE_REPLAY_THREADS");
  const char* scheduling_flag=std::getenv("SR_NATIVE_REPLAY_SCHEDULING");
  const auto replay_options=graphics::guest::ParseReplayOptions(
      threads_flag?threads_flag:"",scheduling_flag?scheduling_flag:"",bool(packet_sink_));
  if(replay_options.invalid_threads)REXLOG_WARN("native replay: invalid SR_NATIVE_REPLAY_THREADS; using direct replay");
  if(replay_options.invalid_scheduling)REXLOG_WARN("native replay: invalid SR_NATIVE_REPLAY_SCHEDULING; using contiguous scheduling");
  const unsigned replay_threads=replay_options.shards;
  std::unique_ptr<graphics::guest::ParallelReplayDecoder> parallel;
  if(replay_options.prepared)parallel=std::make_unique<graphics::guest::ParallelReplayDecoder>(replay_threads,replay_options.scheduling);
  REXLOG_INFO("native renderer: replay decode threads {} (ordered batches)",parallel?replay_threads:1);
  const char* batch_flag=std::getenv("SR_NATIVE_BATCH_COMMANDS");
  const unsigned batch_limit=packet_sink_ && batch_flag && !std::strcmp(batch_flag,"256")?256:
      (packet_sink_ && batch_flag && !std::strcmp(batch_flag,"128")?128:32);
  REXLOG_INFO("native renderer: replay mode={} api={} threads={} scheduling={} batch_limit={}",
      parallel?"prepared":"direct",packet_sink_?"vulkan":"d3d12",replay_threads,
      replay_options.scheduling==graphics::guest::ReplayScheduling::kInterleaved?"interleaved":"contiguous",batch_limit);
  auto& timeline = graphics::FrameTimeline::Global();
  std::ofstream trace_output;
  std::unique_ptr<graphics::guest::ReplayTraceWriter> trace_writer;
  bool trace_invalid=false;
  auto invalidate_trace=[&](const char* reason) {
    if(!trace_invalid)REXLOG_ERROR("replay_trace_invalid=1 reason={}",reason);
    trace_invalid=true;trace_writer.reset();
  };
  if(packet_sink_ && timeline.enabled() && graphics::FrameTimeline::Detail() && SinkProfiling()) {
    const auto path=graphics::FrameTimeline::PathFromEnvironment()+".replay.csv";
    std::error_code ec;
    const bool exists=std::filesystem::exists(path,ec);
    if(exists || ec)invalidate_trace("trace path exists or cannot be inspected");
    else {
      trace_output.open(path,std::ios::out);
      const auto clock_anchor=graphics::FrameTimeline::NowNs();
      const auto utc_anchor=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      if(trace_output)trace_writer=std::make_unique<graphics::guest::ReplayTraceWriter>(trace_output,clock_anchor,utc_anchor);
      if(!trace_writer || !trace_writer->good())invalidate_trace("trace open/header write failed");
    }
  }
  auto close_trace=[&] {
    replay_trace_sample=nullptr;
    if(trace_output.is_open()) {
      trace_output.flush();
      if(!trace_output.good())invalidate_trace("trace flush failed");
      trace_output.close();
      if(trace_output.fail())invalidate_trace("trace close failed");
    }
  };
  uint64_t trace_sequence=0;
  for (;;) {
    std::shared_ptr<const WorkBatch> batch;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      const auto idle_started=std::chrono::steady_clock::now();
      queue_cv_.wait(lock, [this] { return worker_stop_ || !work_queue_.empty(); });
      if(SinkProfiling()) g_sink_profile.idle_us+=ElapsedUs(idle_started);
      if(work_queue_.empty() && worker_stop_) {close_trace();return;}
      batch = batch_pool_->Freeze(std::move(work_queue_.front()));
      work_queue_.pop_front();
    }
    const uint64_t timeline_begin_ns = timeline.enabled() ? graphics::FrameTimeline::NowNs() : 0;
    graphics::TakeTimelineBlockedNs();  // drop waits recorded outside a batch
    std::optional<graphics::guest::ReplayBatchSample> trace_sample;
    if(trace_writer) {
      trace_sample.emplace();trace_sample->sequence=++trace_sequence;
      trace_sample->options=replay_options;trace_sample->commands=batch->cmds.size();
      if(!batch->cmds.empty() && batch->cmds.back().op==Op::kSwap)trace_sample->closing_swap=batch->cmds.back().u64;
      replay_trace_sample=&*trace_sample;
    }
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      const auto draw_count=std::count_if(batch->cmds.begin(),batch->cmds.end(),[](const WorkCmd& cmd) {
        return cmd.op==Op::kDraw || cmd.op==Op::kDrawIndexed || cmd.op==Op::kDrawInline;
      });
      if(trace_sample)trace_sample->draws=size_t(draw_count);
      std::optional<graphics::guest::DrawPacketArena> payloads;
      if(packet_sink_ && draw_count && !parallel) {
        const bool profile=SinkProfiling();
        const auto started=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        payloads.emplace(size_t(draw_count),ReplayStorageEnabled(),payload_pool);
        if(trace_sample)trace_sample->direct_arena_ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count());
        if(profile) g_sink_profile.arena_setup_us+=ElapsedUs(started);
      }
      std::vector<graphics::guest::ReplayResult> decoded;
      if(parallel && !worker_stop_ && !packet_sink_failed_) {
        graphics::guest::ReplayBatchProbe probe;
        decoded=parallel->Decode(batch,mirror_,SinkProfiling()?&probe:nullptr,ReplayStorageEnabled(),payload_pool);
        if(trace_sample)trace_sample->prepared=probe;
        if(SinkProfiling()) {
          auto& p=g_sink_profile;p.replay_us+=probe.wall_ns/1000;++p.parallel_batches;
          p.replay.capture_ns+=probe.stages.capture_ns;p.replay.pm4_ns+=probe.stages.pm4_ns;
          p.parallel_decode_cpu_ns+=probe.stages.decode_ns;p.parallel_arena_cpu_ns+=probe.arena_ns;
          p.parallel_prepare_ns+=probe.prepare_ns;
        }
      }
      size_t command_index=0;
      const auto execution_limit=parallel?graphics::guest::ReplayExecutionLimit(decoded):batch->cmds.size();
      for (const WorkCmd& cmd : batch->cmds) {
        if(command_index>=execution_limit)break;
        const auto started=CpuProfiling()?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        const bool draw=cmd.op==Op::kDraw || cmd.op==Op::kDrawIndexed || cmd.op==Op::kDrawInline;
        Execute(guest_base_, *batch, cmd,payloads && draw?payloads->Next():graphics::guest::DrawPacketOwner{},packet_sink_?batch:nullptr,parallel?&decoded[command_index]:nullptr);
        ++command_index;
        if(CpuProfiling()) {
          auto& p=cpu_timings;
          p.execute_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count());
          if(cmd.op==Op::kSwap && ++p.swaps==120) {
            REXLOG_INFO("native CPU worker (ms/frame over 120, nested): api={} execute={:.2f} prepare={:.2f} constants={:.2f} streams={:.2f}",packet_sink_?"vulkan":"d3d12",p.execute_ns/120000000.0,p.prepare_ns/120000000.0,p.constants_ns/120000000.0,p.streams_ns/120000000.0);
            REXLOG_INFO("native CPU prepare detail (ms/frame over 120, nested surface in targets): surface={:.3f} targets={:.3f} pipeline={:.3f} fixed_state={:.3f} other={:.3f}",p.surfaces_ns/120000000.0,p.render_targets_ns/120000000.0,p.pipeline_ns/120000000.0,p.fixed_state_ns/120000000.0,(double(p.prepare_ns)-double(p.constants_ns)-double(p.render_targets_ns)-double(p.pipeline_ns)-double(p.fixed_state_ns))/120000000.0);
            if (mirror_.alu_read_profile.enabled) {
              const auto& r=mirror_.alu_read_profile;
              REXLOG_INFO("native CPU PM4 live reads (120 swaps, validation nested in execute): calls={} bytes={} queries={} bounded_calls={} failures={} validation_ns={} validation_ms={:.4f}",r.calls,r.bytes,r.queries,r.bounded_calls,r.failures,r.validation_ns,r.validation_ns/120000000.0);
              mirror_.alu_read_profile={};
              mirror_.alu_read_profile.enabled=true;
            }
            REXLOG_INFO("native CPU constants (ms/frame over 120, nested): floats={:.2f} slots={:.2f} texture_lookup={:.2f} texture_hash={:.2f} samplers={:.2f} shared_other={:.2f} | draws={} slot_hits={} slot_misses={} hash_checks={} hash_kb={}",p.constant_float_ns/120000000.0,p.constant_slots_ns/120000000.0,p.texture_lookup_ns/120000000.0,p.texture_hash_ns/120000000.0,p.sampler_ns/120000000.0,(p.constants_ns-p.constant_float_ns-p.constant_slots_ns)/120000000.0,p.constant_draws/120,p.slot_hits/120,p.slot_misses/120,p.texture_hash_checks/120,p.texture_hash_bytes/120/1024);
            if(StreamDetailProfiling()) {
              const auto& d=stream_detail_timings;
              REXLOG_INFO("native CPU stream detail (ms/frame over 120, disjoint within streams): setup={:.4f} apply={:.4f} ia={:.4f} srv={:.4f} declaration={:.4f} metadata={:.4f}",d.setup_ns/120000000.0,d.apply_ns/120000000.0,d.ia_ns/120000000.0,d.srv_ns/120000000.0,d.declaration_ns/120000000.0,d.metadata_ns/120000000.0);
              REXLOG_INFO("native CPU stream totals (120 swaps): calls={} streams={} ia_binds={} missing={} failed_calls={} dynamic_streams={} srv_created={} srv_reused={} srv_failed={} declaration_reads={} attributes={} metadata_writes={} action0={} action1={} action2={} action_other={}",d.calls,d.streams,d.ia_binds,d.missing,d.failed_calls,d.dynamic_streams,d.srv_created,d.srv_reused,d.srv_failed,d.declaration_reads,d.attributes,d.metadata_writes,d.actions[0],d.actions[1],d.actions[2],d.actions[3]);
              REXLOG_INFO("native buffer SRV cache interval: retained_hits={} evicted={} invalidated={} live={}",d.srv_retained_hits,d.cache_evicted,d.cache_invalidated,buffer_srv_cache_?buffer_srv_cache_->size():0);
              stream_detail_timings={};
            }
            p.execute_ns=p.prepare_ns=p.constants_ns=p.streams_ns=p.swaps=0;
            p.surfaces_ns=p.render_targets_ns=p.pipeline_ns=p.fixed_state_ns=0;
            REXLOG_INFO("native CPU texture watches (ms/frame over 120, nested in texture_lookup): watch={:.2f} rearms={} reloads={}",p.texture_watch_ns/120000000.0,p.texture_watch_rearms/120,p.texture_hash_changes/120);
            p.texture_watch_ns=p.texture_watch_rearms=p.texture_hash_changes=0;
            REXLOG_INFO("native CPU shared constants (ms/frame over 120, nested): compare_upload={:.2f} hits={} uploads={} saved_kb={}",p.shared_upload_ns/120000000.0,p.shared_hits/120,p.shared_uploads/120,p.shared_hits*4/120);
            p.shared_hits=p.shared_uploads=p.shared_upload_ns=0;
            if(ConstantDetailProfiling()) {
              const auto& d=constant_detail_timings;
              REXLOG_INFO("native CPU constant detail (ms/frame over 120, disjoint within constants): flush={:.4f} shared_alloc={:.4f} vs_alloc={:.4f} vs_copy={:.4f} ps_alloc={:.4f} ps_copy={:.4f} setup={:.4f} derived={:.4f} bindings={:.4f}",d.flush_ns/120000000.0,d.shared_alloc_ns/120000000.0,d.vs_alloc_ns/120000000.0,d.vs_copy_ns/120000000.0,d.ps_alloc_ns/120000000.0,d.ps_copy_ns/120000000.0,d.setup_ns/120000000.0,d.derived_ns/120000000.0,d.bindings_ns/120000000.0);
              // Raw totals preserve exact reconciliation, rather than rounding counts per frame.
              REXLOG_INFO("native CPU constant detail totals (120 swaps): draws={} mirror_draws={} vs_uploads={} vs_reuses={} ps_uploads={} ps_reuses={} vs_bytes={} ps_bytes={} allocation_failures={} ring_requests={}",d.draws,d.mirror_draws,d.vs_uploads,d.vs_reuses,d.ps_uploads,d.ps_reuses,d.vs_bytes,d.ps_bytes,d.allocation_failures,d.ring_requests);
              REXLOG_INFO("native CPU constant transfers (ms/frame over 120, nested in vs_copy/ps_copy): vs_capture={:.4f} vs_write={:.4f} ps_capture={:.4f} ps_write={:.4f}",d.vs_capture_ns/120000000.0,d.vs_write_ns/120000000.0,d.ps_capture_ns/120000000.0,d.ps_write_ns/120000000.0);
              constant_detail_timings={};
            }
            if(!packet_sink_) FlushTextureAudit("d3d12",frame_count_);
            p.constant_float_ns=p.constant_slots_ns=p.texture_lookup_ns=p.texture_hash_ns=p.sampler_ns=0;
            p.texture_hash_bytes=p.texture_hash_checks=p.slot_hits=p.slot_misses=p.constant_draws=0;
          }
        }
      }
    }
    replay_trace_sample=nullptr;
    if(trace_sample && trace_writer && !trace_writer->Write(*trace_sample))invalidate_trace("trace batch write failed");
    if (timeline.enabled()) {
      // OnSwap flushes the batch right after the swap command, so a swap always ends its batch.
      const uint64_t batch_end_ns = graphics::FrameTimeline::NowNs();
      if (!timeline_worker_begin_ns_) timeline_worker_begin_ns_ = timeline_begin_ns;
      timeline_worker_exec_ns_ += batch_end_ns - timeline_begin_ns;
      timeline_worker_blocked_ns_ += graphics::TakeTimelineBlockedNs();
      if (!batch->cmds.empty() && batch->cmds.back().op == Op::kSwap) {
        const uint64_t blocked = std::min(timeline_worker_blocked_ns_, timeline_worker_exec_ns_);
        timeline.Record(graphics::TimelineStage::kWorker, batch->cmds.back().u64, timeline_worker_begin_ns_,
                        batch_end_ns, timeline_worker_exec_ns_ - blocked, blocked);
        timeline_worker_begin_ns_ = timeline_worker_exec_ns_ = timeline_worker_blocked_ns_ = 0;
      }
    }
    batch.reset();
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      ++batches_done_;
    }
    done_cv_.notify_all();
  }
}

void Renderer::Execute(uint8_t* base, const WorkBatch& batch, const WorkCmd& cmd,graphics::guest::DrawPacketOwner storage,std::shared_ptr<const WorkBatch> capture_owner,graphics::guest::ReplayResult* prepared) {
  if(packet_sink_) {
    if(worker_stop_ || packet_sink_failed_) return;
    graphics::guest::RenderPacket packet;std::string error;
    const bool profile=SinkProfiling();const auto started=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    bool ok;
    if(prepared) {ok=prepared->ok;packet=std::move(prepared->packet);error=std::move(prepared->error);}
    else ok=graphics::guest::ReplayCapturedRenderPacket(batch,cmd,mirror_,packet,error,std::move(storage),std::move(capture_owner),profile?&g_sink_profile.replay:nullptr);
    const bool swap=std::holds_alternative<graphics::guest::SwapPacket>(packet);
    const auto replayed=profile?std::chrono::steady_clock::now():started;
    if(replay_trace_sample && !prepared)replay_trace_sample->direct_decode_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(replayed-started).count());
    if(profile && ok && graphics::guest::GetDraw(packet)) {
      auto& p=g_sink_profile;
      const auto vs=prepared?prepared->vs_version:mirror_.vs_version,ps=prepared?prepared->ps_version:mirror_.ps_version;
      ++p.draws;p.vs_same+=p.last_vs==vs;p.ps_same+=p.last_ps==ps;
      p.last_vs=vs;p.last_ps=ps;
    }
    if(ok) {
      // The swap hand-off waits for the previous frame's recording; count it as blocked, not busy.
      if(swap) {graphics::TimelineBlockScope blocked;ok=packet_sink_(std::move(packet),error);}
      else ok=packet_sink_(std::move(packet),error);
    }
    if(profile) {
      auto& p=g_sink_profile;++p.packets;if(!prepared)p.replay_us+=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(replayed-started).count());
      (swap?p.swap_us:p.sink_us)+=ElapsedUs(replayed);
      if(swap && ++p.swaps==120) {
        REXLOG_INFO("Vulkan worker profile (ms/frame over 120): replay={:.1f} sink={:.1f} swap_enqueue={:.1f} idle={:.1f} packets={}",p.replay_us/120000.0,p.sink_us/120000.0,p.swap_us/120000.0,p.idle_us/120000.0,p.packets/120);
        REXLOG_INFO("Vulkan constant banks (per frame over 120): draws={} unchanged_vs={} unchanged_ps={}",p.draws/120,p.vs_same/120,p.ps_same/120);
        REXLOG_INFO("Vulkan replay detail (ms/frame over 120): capture={:.2f} pm4={:.2f} decode={:.2f} arena_setup={:.2f}",p.replay.capture_ns/120000000.0,p.replay.pm4_ns/120000000.0,p.replay.decode_ns/120000000.0,p.arena_setup_us/120000.0);
        if(p.parallel_batches)REXLOG_INFO("Vulkan parallel replay (over 120): batches/frame={} prepare_serial_ms={:.2f} decode_cpu_sum_ms={:.2f} arena_cpu_sum_ms={:.2f} (wall time included in replay)",p.parallel_batches/120,p.parallel_prepare_ns/120000000.0,p.parallel_decode_cpu_ns/120000000.0,p.parallel_arena_cpu_ns/120000000.0);
        p={};
      }
    }
    if(!ok) {
      packet_sink_failed_=true;
      if(!worker_stop_) {REXLOG_ERROR("native API packet failed: {}",error);rex::FatalError("Native renderer failed: "+error);}
    }
    return;
  }
  t_capture_ranges = batch.ranges.data() + cmd.range_first;
  t_capture_count = cmd.range_count;
  t_capture_bytes = batch.bytes.data();
  t_capture_bytes_size = batch.bytes.size();
  g_current_pass = cmd.pass;
  if (frame_open_ && REXCVAR_GET(sr_native_gpu_pass_timing)) PassTimestamp(cmd.pass);
  if (cmd.pass != last_exec_pass_) {
    if (temporal_probe_ && kPassRenderHud >= 0 && cmd.pass == kPassRenderHud &&
        frame_open_) {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      temporal_.ObserveHudBoundary();
    }
    if (fxaa_ && cmd.pass == kPassRenderHud && upscale_rt_ && frame_open_) {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      ApplyFxaa(*upscale_rt_);
    }
    if (cmd.pass == kPassRenderHud) upscale_rt_ = nullptr;
    if (ssao_ && !ssao_done_ && cmd.pass >= kPassEndTiling && last_exec_pass_ >= kPassRenderOpaque &&
        last_exec_pass_ < kPassEndTiling && scene_rt_ && scene_ds_ && camera_valid_ && frame_open_) {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      ApplySsao();
      ssao_done_ = true;
    }
    last_exec_pass_ = cmd.pass;
  }
  if (watch_frame_ >= 0 && !watch_done_ && cmd.op != Op::kDraw && cmd.op != Op::kDrawIndexed &&
      cmd.op != Op::kDrawInline && cmd.op != Op::kRing) {
    static const char* names[] = {"draw", "drawidx", "drawinline", "resolve", "begintiling",
                                  "endtiling", "swap", "passend", "ring", "clear"};
    watch_ops_ += fmt::format(" {}({:X})", names[int(cmd.op) % 10], cmd.u[0]);
  }
  exec_copies_before_ = mirror_.copy_draws;
  if (cmd.ring_bytes) {
    if (cmd.packet_check) {
      graphics::guest::CapturedMemory memory;
      std::string error;
      if (graphics::guest::CapturedMemory::Capture(batch, cmd, memory, error)) {
        mirror_.ScanCopyUsing(batch.bytes.data() + cmd.ring_offset, cmd.ring_bytes,
            [&memory](uint32_t address, uint32_t length) -> std::span<const uint8_t> {
              try { return memory.Read(address, length); }
              catch (const std::out_of_range&) { return {}; }
            });
      } else {
        REXLOG_WARN("native PM4 captured reader: {}", error);
      }
    } else {
      mirror_.ScanCopy(base, batch.bytes.data() + cmd.ring_offset, cmd.ring_bytes);
    }
  }
  if (cmd.packet_check) {
    graphics::guest::RenderPacket packet;
    std::string error;
    if (!graphics::guest::DecodeRenderPacket(batch, cmd, mirror_, packet, error)) {
      ++stats_.packet_errors;
      if (stats_.packet_errors <= 8)
        REXLOG_WARN("native packet check: op {} pass {}: {}", int(cmd.op), cmd.pass, error);
    } else {
      ++stats_.packets_decoded;
      if(auto* draw=graphics::guest::GetDraw(packet)) {
        for(const auto& texture:draw->textures) if(texture) ++stats_.packet_texture_refs;
        for(const auto& [slot,reason]:draw->texture_errors) {
          if(++stats_.packet_texture_errors<=8)
            REXLOG_WARN("native texture capture: slot {} format {} dimension {} base {:08X}: {}",slot,
                        draw->texture_fetch[slot][1]&0x3f,(draw->texture_fetch[slot][5]>>9)&3,
                        draw->texture_fetch[slot][1]&0xfffff000,reason);
        }
        if(!draw->vertex_shader || !draw->pixel_shader) ++stats_.packet_shader_missing;
      }
    }
    if (cmd.op == Op::kSwap)
      REXLOG_INFO("native packet check: {} decoded, {} capture errors, {} texture refs, "
                  "{} texture capture failures (including resolve-only formats), {} shader captures missing",
                  stats_.packets_decoded, stats_.packet_errors, stats_.packet_texture_refs,
                  stats_.packet_texture_errors, stats_.packet_shader_missing);
  }
  const bool profiling = prof_active_ && frame_open_;
  const uint64_t prof_stats_draws = stats_.draws;
  switch (cmd.op) {
    case Op::kDraw:
      ExecDrawVertices(base, batch, cmd);
      break;
    case Op::kDrawIndexed:
      ExecDrawIndexedVertices(base, batch, cmd);
      break;
    case Op::kDrawInline:
      ExecDrawInlineVertices(base, cmd.u[0], cmd.u[1], cmd.u[2], cmd.u[3]);
      break;
    case Op::kResolve:
      if (trace_state_ == 1)
        REXLOG_INFO("trace resolve arguments level={} slice={} flags={:08X}",cmd.u[6],cmd.u[7],cmd.u[0]);
      ExecResolve(base, cmd.u[0], cmd.u[1], cmd.u[2], cmd.u[3], cmd.u[4], cmd.f, cmd.u[5],cmd.u[6],cmd.u[7]);
      break;
    case Op::kBeginTiling:
      ExecBeginTiling(base, cmd.u[0], cmd.u[1], cmd.u[2], cmd.f, cmd.u[3]);
      break;
    case Op::kEndTiling:
      ExecEndTiling();
      break;
    case Op::kSwap:
      ExecOnSwap(base, cmd.u[0], cmd.u64);
      break;
    case Op::kPassEnd:
      ExecOnPassEnd(int(cmd.u[0]));
      break;
    case Op::kClear:
      {
        float color[4];
        std::memcpy(color, &cmd.u[3], sizeof(color));
        ExecClear(base, cmd.u[0], cmd.u[1], cmd.u[2], color, cmd.f, cmd.u[7]);
      }
      break;
    case Op::kRing:
      break;
  }
  if (profiling) ProfileMark(cmd.op, stats_.draws != prof_stats_draws);
  t_capture_count = 0;
  t_capture_ranges = nullptr;
}

void Renderer::DrawVertices(uint8_t* base, uint32_t prim, uint32_t start_vertex,
                            uint32_t vertex_count) {
  GuestNativeCaptureProfile guest_capture_profile;
  DetailScope hook_scope(capture_timings.fe_hook_ns);
  if (!REXCVAR_GET(sr_native_draws)) return;
  std::lock_guard<std::mutex> lock(front_mutex_);
  guest_base_ = base;
  uint32_t dev = GuestDevice(base);
  if (!dev) return;
  if (prim == uint32_t(xenos::PrimitiveType::kRectangleList)) {
    // Rect lists need CPU expansion: read stream 0 through the guest's
    // physical view (0xA0000000 maps physical memory 1:1) and draw inline.
    uint32_t fetch0 = Load32(base, dev + kDevVertexFetch0);
    uint32_t stride = Load8(base, dev + kDev.stream_strides) * 4;
    uint32_t phys = fetch0 & ~3u;
    if (!phys || !stride) return;
    uint32_t data = 0xA0000000u + phys + start_vertex * stride;
    BeginCmd(Op::kDrawInline);
    CaptureRing(base, dev);
    CaptureDevice(base, dev);
    CaptureBytes(base, data, vertex_count * stride);
    cur_.u[0] = prim;
    cur_.u[1] = data;
    cur_.u[2] = vertex_count;
    cur_.u[3] = stride;
    EndCmd(base);
    return;
  }
  BeginCmd(Op::kDraw);
  CaptureRing(base, dev);
  CaptureDevice(base, dev);
  cur_.u[0] = prim;
  cur_.u[1] = start_vertex;
  cur_.u[2] = vertex_count;
  VertexRange draw_range;
  draw_range.first = start_vertex;
  draw_range.end = start_vertex + vertex_count;
  cur_.streams_ok = PlanStreams(base, dev, Load32(base, dev + kDevVertexDecl), &draw_range);
  EndCmd(base);
}

void Renderer::DrawIndexedVertices(uint8_t* base, uint32_t prim, int32_t base_vertex,
                                   uint32_t start_index, uint32_t index_count) {
  GuestNativeCaptureProfile guest_capture_profile;
  DetailScope hook_scope(capture_timings.fe_hook_ns);
  if (int32_t spin = REXCVAR_GET(sr_native_debug_spin_us)) {
    auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(spin);
    while (std::chrono::steady_clock::now() < until) {
    }
  }
  if (!REXCVAR_GET(sr_native_draws)) return;
  compat::RegisterSampledThread(3, "native_draw_thread");
  std::lock_guard<std::mutex> lock(front_mutex_);
  guest_base_ = base;
  uint32_t dev = GuestDevice(base);
  if (!dev) return;
  BeginCmd(Op::kDrawIndexed);
  CaptureRing(base, dev);
  CaptureDevice(base, dev);
  cur_.u[0] = prim;
  cur_.u[1] = uint32_t(base_vertex);
  cur_.u[2] = start_index;
  cur_.u[3] = index_count;
  // Vertex range referenced by the indices, scanned only if a dynamic vertex
  // buffer of this draw is dirty.
  DetailScope index_scope(capture_timings.fe_index_ns);
  VertexRange draw_range;
  if (uint32_t ib_object = Load32(base, dev + kDev.index_buffer)) {
    uint32_t header = Load32(base, ib_object);
    bool index32 = (header & 0x80000000u) != 0;
    uint32_t endian = (header >> 29) & 3;
    static bool logged_endian[2][4] = {};
    if (!logged_endian[index32][endian]) {
      logged_endian[index32][endian] = true;
      REXLOG_INFO("native: index DMA {}-bit endian {} (header {:08X})",
                  index32 ? 32 : 16, endian, header);
    }
    // +0x18 is a guest *virtual* address: convert like the XDK draw prologue
    // (82580FF8): physical = low 29 bits, +4 KB for the 0xE0000000 view.
    uint32_t v = Load32(base, ib_object + 0x18);
    uint32_t address = (v & 0x1FFFFFFFu) + (v >= 0xE0000000u ? 0x1000u : 0u);
    uint32_t size = Load32(base, ib_object + 0x1C) & 0x00FFFFFFu;
    if (endian >= 2) size = (size + 3) & ~3u;
    draw_range.resolved = false;
    draw_range.ib_phys = address;
    draw_range.ib_size = size;
    draw_range.start_index = start_index;
    draw_range.index_count = index_count;
    draw_range.base_vertex = base_vertex;
    draw_range.index32 = index32;
    draw_range.index_endian = endian;
    const bool strip = prim == uint32_t(xenos::PrimitiveType::kTriangleStrip) ||
                       prim == uint32_t(xenos::PrimitiveType::kLineStrip);
    uint32_t reset_index = UINT32_MAX;
    if (strip && (LoadReg(base, dev, rex::graphics::XE_GPU_REG_PA_SU_SC_MODE_CNTL) & (1u << 21))) {
      const uint32_t reset = LoadReg(base, dev, rex::graphics::XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX) & 0xFFFFFFu;
      if (index32 || reset <= UINT16_MAX) reset_index = reset;
    }
    draw_range.reset_index = reset_index;
    const uint32_t isize = index32 ? 4 : 2;
    const uint32_t ib_format = (index32 ? 2u : 1u) | (endian << 2);
    const uint32_t address_hash = address ^ (address >> 12) ^ (address >> 23);
    const size_t set = ((address_hash * 0x9E3779B1u) >> 22) & (kFrontStreamSetCount - 1);
    const size_t first = set * 2;
    auto& cache = front_stream_cache_[16];
    const auto matches = [&](size_t slot) {
      const FrontStreamCache& candidate = cache[slot];
      return candidate.tracked && candidate.address == address && candidate.size == size &&
             candidate.decl == reset_index && candidate.format == ib_format;
    };
    const bool allow_cache = !REXCVAR_GET(sr_native_debug_buffers_always_dirty);
    const bool first_hit = allow_cache && matches(first);
    const bool second_hit = !first_hit && allow_cache && matches(first + 1);
    const bool cached_fields_match = first_hit || second_hit;
    const size_t selected_slot = first + (first_hit ? 0 : second_hit ? 1 : front_stream_cache_next_[16][set]);
    FrontStreamCache& sc = cache[selected_slot];
    const bool cached_dirty = cached_fields_match && RefreshTrackedBuffer(*sc.tracked);
    bool ok = false;
    if (cached_fields_match && !cached_dirty) {
      cur_.index = {};
      cur_.index.key = sc.key;
      cur_.index.address = address;
      cur_.index.size = size;
      cur_.index.format = ib_format;
      cur_.index.reset_index = reset_index;
      cur_.index.action = 0;
      ok = true;
    } else {
      cur_.index = PlanBuffer(base, address, size, 0, 0, ib_format, 0, start_index * isize,
                              (start_index + index_count) * isize, ok, reset_index,
                              nullptr, 0,
                              cached_fields_match ? sc.tracked : nullptr, sc.key);
      if (ok) {
        if (!cached_fields_match) front_stream_cache_next_[16][set] ^= 1;
        auto it = tracked_.find(cur_.index.key);
        sc.address = address;
        sc.size = size;
        sc.decl = reset_index;
        sc.stride = 0;
        sc.format = ib_format;
        sc.phase = 0;
        sc.key = cur_.index.key;
        sc.tracked = it != tracked_.end() ? &it->second : nullptr;
      }
    }
    cur_.has_index = ok;
    cur_.index32 = index32;
    cur_.index_size = size;
    if (prim == uint32_t(xenos::PrimitiveType::kQuadList) && ok &&
        uint64_t(start_index + index_count) * isize <= size) {
      // Quads have no host topology: every group of 4 indices (a b c d) becomes
      // the triangles (a b c) (a c d), like the non-indexed expansion. The
      // expanded list is captured with the command; base_vertex stays a draw
      // argument.
      const uint8_t* idx = REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(address);
      std::vector<uint32_t> tris;
      std::string error;
      if (!graphics::guest::NormalizeIndices({idx,size},start_index,index_count,
            {index32,endian,UINT32_MAX},graphics::guest::Primitive::kQuads,tris,error)) {
        cur_.has_index = false;
        REXLOG_WARN("native: quad normalization failed: {}",error);
      }
      cur_.u[4] = uint32_t(batch_->bytes.size());
      cur_.u[5] = uint32_t(tris.size());
      batch_->bytes.insert(batch_->bytes.end(), reinterpret_cast<const uint8_t*>(tris.data()),
                           reinterpret_cast<const uint8_t*>(tris.data()) + tris.size() * 4);
    }
  }
  index_scope.Stop();
  cur_.streams_ok = PlanStreams(base, dev, Load32(base, dev + kDevVertexDecl), &draw_range);
  // Opt-in diagnostic: capture the exact normalized index envelope while this
  // guest draw owns its input, never scan live guest indices on the worker.
  // DrawIndexed u6/u7 are otherwise unused; zero/zero means unverified.
  if(!REXCVAR_GET(sr_native_motion_capture_trigger_path).empty() && cur_.has_index &&
     uint64_t(start_index)+index_count<=uint64_t(draw_range.ib_size)/(draw_range.index32?4u:2u)) {
    draw_range.Resolve();
    if(draw_range.first<draw_range.end && draw_range.end<=0x1000000u) {
      cur_.u[6]=draw_range.first;cur_.u[7]=draw_range.end;
    }
  }
  EndCmd(base);
}

void Renderer::DrawInlineVertices(uint8_t* base, uint32_t prim, uint32_t data,
                                  uint32_t vertex_count, uint32_t stride) {
  GuestNativeCaptureProfile guest_capture_profile;
  DetailScope hook_scope(capture_timings.fe_hook_ns);
  if (!REXCVAR_GET(sr_native_draws)) return;
  std::lock_guard<std::mutex> lock(front_mutex_);
  guest_base_ = base;
  uint32_t dev = GuestDevice(base);
  if (!dev) return;
  BeginCmd(Op::kDrawInline);
  CaptureRing(base, dev);
  CaptureDevice(base, dev);
  if (data && stride) CaptureBytes(base, data, vertex_count * stride);
  cur_.u[0] = prim;
  cur_.u[1] = data;
  cur_.u[2] = vertex_count;
  cur_.u[3] = stride;
  EndCmd(base);
}

void Renderer::Resolve(uint8_t* base, uint32_t flags, uint32_t src_rect, uint32_t dest_texture,
                       uint32_t dest_point, uint32_t clear_color, float clear_z,
                       uint32_t clear_stencil,uint32_t level,uint32_t slice) {
  GuestNativeCaptureProfile guest_capture_profile;
  DetailScope hook_scope(capture_timings.fe_hook_ns);
  if (!REXCVAR_GET(sr_native_resolves)) return;
  std::lock_guard<std::mutex> lock(front_mutex_);
  guest_base_ = base;
  uint32_t dev = GuestDevice(base);
  if (!dev) return;
  BeginCmd(Op::kResolve);
  CaptureRing(base, dev);
  CaptureDevice(base, dev);
  if (src_rect) CaptureBytes(base, src_rect, 16);
  if (dest_point) CaptureBytes(base, dest_point, 8);
  if (dest_texture) CaptureBytes(base, dest_texture, 0x40);
  if (clear_color) CaptureBytes(base, clear_color, 16);
  cur_.u[0] = flags;
  cur_.u[1] = src_rect;
  cur_.u[2] = dest_texture;
  cur_.u[3] = dest_point;
  cur_.u[4] = clear_color;
  cur_.u[5] = clear_stencil;
  cur_.u[6] = level;
  cur_.u[7] = slice;
  cur_.f = clear_z;
  EndCmd(base);
}

void Renderer::BeginTiling(uint8_t* base, uint32_t count, uint32_t rects, uint32_t clear_color,
                           float clear_z, uint32_t clear_stencil) {
  GuestNativeCaptureProfile guest_capture_profile;
  DetailScope hook_scope(capture_timings.fe_hook_ns);
  std::lock_guard<std::mutex> lock(front_mutex_);
  guest_base_ = base;
  uint32_t dev = GuestDevice(base);
  BeginCmd(Op::kBeginTiling);
  if (dev) {
    CaptureRing(base, dev);
    CaptureDevice(base, dev);
  }
  if (rects && count) CaptureBytes(base, rects, 16 * std::min(count, 64u));
  if (clear_color) CaptureBytes(base, clear_color, 16);
  cur_.u[0] = count;
  cur_.u[1] = rects;
  cur_.u[2] = clear_color;
  cur_.u[3] = clear_stencil;
  cur_.f = clear_z;
  EndCmd(base);
}

void Renderer::Clear(uint8_t* base, uint32_t count, uint32_t rects, uint32_t flags,
                     const float color[4], float z, uint32_t stencil) {
  GuestNativeCaptureProfile guest_capture_profile;
  DetailScope hook_scope(capture_timings.fe_hook_ns);
  std::lock_guard<std::mutex> lock(front_mutex_);
  guest_base_ = base;
  uint32_t dev = GuestDevice(base);
  if (!dev) return;
  BeginCmd(Op::kClear);
  CaptureRing(base, dev);
  CaptureDevice(base, dev);
  if (rects && count) CaptureBytes(base, rects, 16 * std::min(count, 64u));
  cur_.u[0] = rects ? std::min(count, 64u) : 0;
  cur_.u[1] = rects;
  cur_.u[2] = flags;
  std::memcpy(&cur_.u[3], color, 4 * sizeof(float));
  cur_.u[7] = stencil;
  cur_.f = z;
  EndCmd(base);
}

// Clear the bound render targets and their same-EDRAM-base aliases directly.
void Renderer::ExecClear(uint8_t* base, uint32_t count, uint32_t rects, uint32_t flags,
                         const float color[4], float z, uint32_t stencil) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!BeginFrame()) return;
  uint32_t dev = GuestDevice(base);
  if (!dev) return;
  {
    static int logged = 0;
    if (logged < 16) {
      ++logged;
      REXLOG_INFO("native: D3DDevice_Clear flags {:X} color ({}, {}, {}, {}) z {} stencil {} rects {} pass {}",
                  flags, color[0], color[1], color[2], color[3], z, stencil, count, g_current_pass);
    }
  }
  std::vector<D3D12_RECT> rs;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t a = rects + 16 * i;
    rs.push_back({LONG(int32_t(Load32(base, a))), LONG(int32_t(Load32(base, a + 4))),
                  LONG(int32_t(Load32(base, a + 8))), LONG(int32_t(Load32(base, a + 12)))});
  }
  UINT nrs = UINT(rs.size());
  for (uint32_t i = 0; i < 4; ++i) {
    if (!(flags & (1u << i))) continue;
    uint32_t object = Load32(base, dev + kDevRenderTargets + 4 * i);
    if (HostSurface* rt = object ? GetSurface(base, object, false) : nullptr) {
      for (auto& [key, other] : surfaces_) {
        if (other.depth || other.edram_base != rt->edram_base || other.width != rt->width ||
            other.height != rt->height || other.scale != rt->scale) {
          continue;
        }
        Transition(other.resource.Get(), other.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        std::vector<D3D12_RECT> scaled = ScaleRects(rs, other.scale);
        command_list_->ClearRenderTargetView(other.view, color, nrs, nrs ? scaled.data() : nullptr);
        MarkWritten(&other);
      }
    }
  }
  D3D12_CLEAR_FLAGS ds_flags = D3D12_CLEAR_FLAGS(0);
  if (flags & kClearZBuffer) ds_flags |= D3D12_CLEAR_FLAG_DEPTH;
  if (flags & kClearStencil) ds_flags |= D3D12_CLEAR_FLAG_STENCIL;
  if (ds_flags) {
    HostSurface* rt0 = GetSurface(base, Load32(base, dev + kDevRenderTargets), false);
    if (HostSurface* ds = GetSurface(base, Load32(base, dev + kDevDepthStencil), true,
                                     rt0 ? rt0->width : 0, rt0 ? rt0->height : 0)) {
      MaybeFinishMotionBeforeDepthWrite(ds,(ds_flags&D3D12_CLEAR_FLAG_DEPTH)!=0,"explicit depth clear");
      Transition(ds->resource.Get(), ds->state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
      std::vector<D3D12_RECT> scaled = ScaleRects(rs, ds->scale);
      command_list_->ClearDepthStencilView(ds->view, ds_flags, std::clamp(z, 0.0f, 1.0f),
                                           uint8_t(stencil), nrs, nrs ? scaled.data() : nullptr);
      MarkWritten(ds);
    }
  }
  bound_rt_count_ = 0;
  bound_ds_ = nullptr;
  std::memset(bound_rts_, 0, sizeof(bound_rts_));
}

void Renderer::EndTiling() {
  GuestNativeCaptureProfile guest_capture_profile;
  DetailScope hook_scope(capture_timings.fe_hook_ns);
  std::lock_guard<std::mutex> lock(front_mutex_);
  if (!guest_base_) return;
  BeginCmd(Op::kEndTiling);
  EndCmd(guest_base_);
}

void Renderer::OnPassEnd(int pass) {
  GuestNativeCaptureProfile guest_capture_profile;
  DetailScope hook_scope(capture_timings.fe_hook_ns);
  if (REXCVAR_GET(sr_native_dump_after_pass) < 0) return;  // debug dumps only
  std::lock_guard<std::mutex> lock(front_mutex_);
  if (!guest_base_) return;
  BeginCmd(Op::kPassEnd);
  cur_.u[0] = uint32_t(pass);
  EndCmd(guest_base_);
}

void Renderer::SyncRing(uint8_t* base, uint32_t dev) {
  std::lock_guard<std::mutex> lock(front_mutex_);
  guest_base_ = base;
  BeginCmd(Op::kRing);
  CaptureRing(base, dev);
  EndCmd(base);
}

void Renderer::ResyncRing(uint8_t* base, uint32_t dev) {
  std::lock_guard<std::mutex> lock(front_mutex_);
  if (dev) ring_last_ = Load32(base, dev + kDev.ring_write) + 4;
}

void Renderer::OnSwap(uint8_t* base, uint32_t front_buffer_texture, uint64_t swap_number) {
  ++front_frame_;
  auto& timeline = graphics::FrameTimeline::Global();
  const uint64_t swap_entry_ns = timeline.enabled() ? graphics::FrameTimeline::NowNs() : 0;
  std::lock_guard<std::mutex> lock(front_mutex_);
  guest_base_ = base;
  BeginCmd(Op::kSwap);
  CaptureRing(base, GuestDevice(base));
  if (front_buffer_texture) CaptureBytes(base, front_buffer_texture, 0x40);
  cur_.u[0] = front_buffer_texture;
  cur_.u64 = swap_number;
  if(packet_sink_ && !worker_stop_) {
    auto gamma=std::make_shared<std::array<uint32_t,256>>();
    bool available=GuestGammaRamp256(gamma->data());
    cur_.gamma_enabled=available && REXCVAR_GET(sr_native_gamma_ramp) && std::any_of(gamma->begin(),gamma->end(),[](uint32_t entry){return entry!=0;});
    cur_.gamma=std::move(gamma);
  }
  EndCmd(base);
  if(packet_sink_ && (capture_timings.pm4_us+capture_timings.textures_us>100000 || front_frame_%120==0))
    REXLOG_INFO("native Vulkan capture frame={} pm4_ms={} textures_ms={} reads={} bytes={} virtual_queries={} watch_ms={} read_ms={} hash_ms={} copy_ms={} new={} watch_dirty={} revalidated={} changed={} entries={} watch_scan_hits={} watch_scan_misses={}",front_frame_,capture_timings.pm4_us/1000,capture_timings.textures_us/1000,capture_timings.reads,capture_timings.bytes,capture_timings.queries,capture_timings.watch_us/1000,capture_timings.read_us/1000,capture_timings.hash_us/1000,capture_timings.copy_us/1000,capture_timings.new_entries,capture_timings.watch_dirty,capture_timings.revalidated,capture_timings.changed,captured_textures_.size(),capture_timings.watch_scan_hits,capture_timings.watch_scan_misses);
  if (timeline.enabled()) {
    timeline.RecordSpan(graphics::TimelineStage::kGame, swap_number,
                        timeline_game_exit_ns_ ? timeline_game_exit_ns_ : swap_entry_ns, swap_entry_ns, 0);
    // The capture stage is Vulkan-only (packet_sink_ is the Vulkan discriminator).
    if (packet_sink_)
      timeline.RecordBusy(graphics::TimelineStage::kCapture, swap_number,
                          (capture_timings.pm4_us + capture_timings.textures_us) * 1000);
    if (graphics::FrameTimeline::Detail()) {
      const auto& t = capture_timings;
      timeline.RecordBusy(graphics::TimelineStage::kFrontend, swap_number, t.fe_hook_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeBegin, swap_number, t.fe_begin_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeRing, swap_number, t.fe_ring_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeDevice, swap_number, t.fe_device_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeIndex, swap_number, t.fe_index_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeStreams, swap_number, t.fe_streams_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeEnd, swap_number, t.fe_end_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeFlush, swap_number, t.fe_flush_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFdShaders, swap_number, t.fd_shaders_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFsPrep, swap_number, t.fs_prep_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFsPlan, swap_number, t.fs_plan_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFePush, swap_number, t.fe_push_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFePm4, swap_number, t.fe_pm4_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFeTextures, swap_number, t.fe_textures_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFsResolve, swap_number, t.fs_resolve_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFsBuffer, swap_number, t.fs_buffer_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFbRefresh, swap_number, t.fb_refresh_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFbHash, swap_number, t.fb_hash_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFpPrimary, swap_number, t.fp_primary_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFpRead, swap_number, t.fp_read_ns);
      timeline.RecordBusy(graphics::TimelineStage::kFpCopy, swap_number, t.fp_copy_ns);
      if (swap_number % 120 == 0) {
        const auto writes=completed_texture_writes_.load(std::memory_order_acquire);
        const auto write_delta=writes>=profiled_texture_writes_?writes-profiled_texture_writes_:0;
        REXLOG_INFO("native physical write notifications: per_frame={:.1f} completed={}",double(write_delta)/120.0,writes);
        profiled_texture_writes_=writes;
        auto& c = stream_plan_counters;
        REXLOG_INFO("native front-end stream plan (per frame over 120): evaluated={} fast={} slow={} secondary_hits={} | slow reasons: "
                    "untracked={} address={} size={} decl={} stride={} phase={} dirty={} | buffer refresh: calls={} hashes={} hash_kb={} | pm4 deps: reads={} kb={}",
                     c.evaluated/120,c.fast/120,c.slow/120,c.secondary_hits/120,c.untracked/120,c.address/120,c.size/120,c.decl/120,
                    c.stride/120,c.phase/120,c.dirty/120,c.refresh_calls/120,c.refresh_hashes/120,c.refresh_hash_bytes/120/1024,
                    c.pm4_reads/120,c.pm4_bytes/120/1024);
        c={};
      }
    }
  }
  capture_timings={};
  if(front_frame_%120==0) {
    FlushTextureAudit(packet_sink_?"vulkan":"d3d12_capture",front_frame_);
    FlushResourceUnlockAudit(front_frame_);
  }
  if (worker_mode_) {
    FlushBatch();
    // sr_native_worker_lag 0: the frame is recorded before the guest continues.
    // 1: the worker may still record this frame while the guest builds the
    // next one (everything a command reads from guest memory is captured,
    // except texture data - watched - and shader literal tables).
    uint64_t submitted;
    {
      std::lock_guard<std::mutex> qlock(queue_mutex_);
      submitted = batches_submitted_;
    }
    const uint64_t wait_begin_ns = timeline.enabled() ? graphics::FrameTimeline::NowNs() : 0;
    {
      CpuTimer cpu_timer(front_cpu_timings.wait_ns);
      WaitWorkerIdle(REXCVAR_GET(sr_native_worker_lag) ? prev_swap_batches_ : submitted);
    }
    if (timeline.enabled())
      timeline.RecordSpan(graphics::TimelineStage::kFrontWait, swap_number, wait_begin_ns,
                          graphics::FrameTimeline::NowNs(), 0);
    prev_swap_batches_ = submitted;
  }
  if(CpuProfiling() && front_frame_%120==0) {
    REXLOG_INFO("native CPU frontend (ms/frame over 120): api={} capture={:.2f} worker_wait={:.2f}",packet_sink_?"vulkan":"d3d12",front_cpu_timings.capture_ns/120000000.0,front_cpu_timings.wait_ns/120000000.0);
    front_cpu_timings.capture_ns=front_cpu_timings.wait_ns=0;
  }
  if (timeline.enabled()) {
    // The write happens before the exit stamp, so it is not charged to the next frame's game time.
    if (swap_number % 60 == 0) timeline.Flush(swap_number);
    timeline_game_exit_ns_ = graphics::FrameTimeline::NowNs();
  }
}

void Renderer::ExecOnSwap(uint8_t* base, uint32_t front_buffer_texture, uint64_t swap_number) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  swap_number_ = swap_number;
  resolve_in_frame_ = 0;
  draw_calls_in_frame_ = 0;
  // Per-frame SSAO / soft particle state.
  scene_rt_ = scene_ds_ = nullptr;
  scene_depth_key_ = 0;
  camera_valid_ = ssao_done_ = false;
  if (REXCVAR_GET(sr_native_dump_swap) > 0 && swap_number == uint64_t(REXCVAR_GET(sr_native_dump_swap))) {
    mirror_snapshot_.assign(mirror_.regs(), mirror_.regs() + Pm4Mirror::kRegisterCount);
  }
  if (!BeginFrame()) {
    return;
  }
  uint32_t front_buffer_address = 0;
  if (front_buffer_texture) {
    front_buffer_address = GuestPhysical(Load32(base, front_buffer_texture + 0x1C + 4) & 0xFFFFF000u);
  }
  EndFrameAndPresent(front_buffer_address);
}


}  // namespace superman_returns::native


