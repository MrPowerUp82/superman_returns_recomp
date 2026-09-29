// sr_post_effects=false: which guest GPU work belongs to the post-processing
// chain (bloom / light rays) and is skipped by the project command processor
// (sr_graphics_system.cpp). Pure header without SDK types, so it is unit
// tested in tests/native; tests/tools/test_post_effects.py checks every entry
// against the trace CSV below.
//
// DERIVED FROM THE TRACE, NOT FROM THE GAME CODE. Every entry comes from
// docs/data/gpu_groups_gameplay_intel_uhd.csv (sr_renderer=trace, 120 frames
// in the first street of a new game, i5-13420H + Intel UHD). Semantics
// (downsample, blur...) are guesses from the render-target sizes; none was
// confirmed in RenderDoc, and the effect of skipping them on the image and on
// the frame rate has NOT been measured.
//
// How the chain shows up in the trace:
//   * a quad-list draw (xenos primitive 13 = kQuadList; 8 is kRectangleList,
//     the primitive of the resolves themselves), EDRAM mode 4 (color+depth),
//     no depth/stencil test, one draw per frame, on a surface narrower than
//     the 1280-pixel scene (RB_SURFACE_INFO pitch 640, 320, 160 or 80);
//   * followed by exactly one resolve (IssueCopy) of that surface, which the
//     trace records with the same pixel shader still bound. The resolve copies
//     the pass result into the texture the next pass samples.
// Only those small-surface passes are listed. Everything on the 1280 scene
// surface stays, on purpose: one of those quads is the composite that puts
// the scene (and the bloom) back on screen, and the trace alone cannot tell
// which one, so skipping them could leave the screen black. Kept, for the
// record (VS/PS, surface pitch):
//   871A3860C63CB28D/C65187574559AE17 1280  depth-tested (light-ray source?)
//   871A3860C63CB28D/AF8203A45EE4AE8E 1280  followed by two resolves
//   871A3860C63CB28D/2679C172F83DE85D 1280  resolved
//   118071C8F97830EA/DB6A2996BA148383 1280  resolved
//   7F34D50B9C8D2872/72F8FE27BDDD58FD 1280  not resolved (composite?)
//   46557DE64BFC4377/9B16C6A8418AE1AB 1280  stencil-tested
//   7EDFECB4E4B0E179/AFAE26CC336B4559 1280  0.3 per frame, resolved
//   F5C270349BB0B44C, FC2E703B8BD61A63       several per frame (HUD?)
//   BEB1B8798714B72D, CE2D505A0E7B9767       640 with 4x MSAA (scene-sized)
// Consequence to check at home: the kept passes still sample the textures the
// skipped resolves used to fill. Those now keep whatever the guest memory
// held (zero if never written), so a wrong image (black, white, frozen or
// garbage glow) means a kept pass depends on them.
//
// With sr_render_scale below 100 the surfaces are smaller, nothing matches
// and the post effects stay on (the command processor logs a warning).

#pragma once

#include <cstddef>
#include <cstdint>

namespace superman_returns::post_effects {

// RB_SURFACE_INFO values seen in the trace (pitch = low 14 bits).
inline constexpr uint32_t kSurface640 = 167772800;   // 0x0A000280
inline constexpr uint32_t kSurface320 = 83886400;    // 0x05000140
inline constexpr uint32_t kSurface320b = 67109184;   // 0x04000140
inline constexpr uint32_t kSurface160 = 41943200;    // 0x028000A0
inline constexpr uint32_t kSurface80 = 16777296;     // 0x01000050
inline constexpr uint32_t kSurface80b = 8388688;     // 0x00800050

// xenos::PrimitiveType::kQuadList and EDRAM modes (RB_MODECONTROL & 7).
inline constexpr uint32_t kQuadList = 13;
inline constexpr uint32_t kModeColorDepth = 4;
inline constexpr uint32_t kModeCopy = 6;

struct Pass {
  uint64_t vs;
  uint64_t ps;
  uint32_t surface;
};

// The chain, ordered by surface size (the aggregated CSV does not keep the
// draw order). Comment: surface pitch, draws in the 120 traced frames.
inline constexpr Pass kPasses[] = {
    {0x871A3860C63CB28Dull, 0x059EEADD2781BA31ull, kSurface640},   // 640, 120
    {0x871A3860C63CB28Dull, 0x1E70EB9513D670C9ull, kSurface320},   // 320, 120
    {0x871A3860C63CB28Dull, 0x287160844A17DEA4ull, kSurface320},   // 320, 120
    {0x118071C8F97830EAull, 0xB8B3F881054E85FBull, kSurface320},   // 320, 120
    {0x871A3860C63CB28Dull, 0x1E70EB9513D670C9ull, kSurface320b},  // 320, 34
    {0x118071C8F97830EAull, 0xF9D7929DB6AD1126ull, kSurface320b},  // 320, 68
    {0x118071C8F97830EAull, 0x1276813A772FDBFCull, kSurface160},   // 160, 120
    {0x118071C8F97830EAull, 0x7407441295D9968Full, kSurface160},   // 160, 120
    {0x871A3860C63CB28Dull, 0x651FD4D1C0591654ull, kSurface80},    //  80, 120
    {0x871A3860C63CB28Dull, 0xB39672E79A9A31DAull, kSurface80b},   //  80, 120
    {0x871A3860C63CB28Dull, 0x28113F6D03B7E996ull, kSurface80b},   //  80, 120
    {0x871A3860C63CB28Dull, 0xAA51172941D47B1Bull, kSurface80b},   //  80, 120
    {0xD3A3A38DF63A3145ull, 0x040D6633B0AA5D6Dull, kSurface80b},   //  80, 120
};
inline constexpr size_t kPassCount = sizeof(kPasses) / sizeof(kPasses[0]);

inline bool IsChainPass(uint64_t vs, uint64_t ps, uint32_t surface) {
  for (const Pass& p : kPasses) {
    if (p.vs == vs && p.ps == ps && p.surface == surface) return true;
  }
  return false;
}

// Per-command-processor state: a listed quad is skipped, and so are the
// resolves that follow it while its pixel shader and surface are still bound
// (the next non-resolve draw ends that window). A resolve anywhere else, e.g.
// of the 1280 scene surface, is never skipped.
class Filter {
 public:
  // Every draw goes through here, including the resolve draws (mode 6,
  // which the D3D12 backend turns into IssueCopy).
  bool SkipDraw(uint32_t primitive, uint32_t edram_mode, uint64_t vs, uint64_t ps,
                uint32_t surface) {
    if (edram_mode == kModeCopy) return false;
    if (primitive == kQuadList && edram_mode == kModeColorDepth &&
        IsChainPass(vs, ps, surface)) {
      pending_ = true;
      pending_ps_ = ps;
      pending_surface_ = surface;
      ++skipped_draws_;
      return true;
    }
    pending_ = false;
    return false;
  }

  bool SkipCopy(uint64_t ps, uint32_t surface) {
    if (!pending_ || ps != pending_ps_ || surface != pending_surface_) return false;
    ++skipped_copies_;
    return true;
  }

  uint64_t skipped_draws() const { return skipped_draws_; }
  uint64_t skipped_copies() const { return skipped_copies_; }

 private:
  bool pending_ = false;
  uint64_t pending_ps_ = 0;
  uint32_t pending_surface_ = 0;
  uint64_t skipped_draws_ = 0;
  uint64_t skipped_copies_ = 0;
};

}  // namespace superman_returns::post_effects
