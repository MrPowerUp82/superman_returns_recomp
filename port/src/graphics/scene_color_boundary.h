#pragma once
#include <array>
#include <cstdint>
#include <optional>

// Pure CPU contract for the bounded city path audited in
// docs/pre-hud-boundary-evidence-2026-10-25.md. No shader hashes, surface sizes,
// binds, PSO preparation, SDK readiness, or universal HUD classification here.
// All proof flags default false; the caller must establish their semantics.
namespace superman_returns::graphics {
struct SceneColorEpoch {
  // Separate, nonzero counters. Within a device each counter is nondecreasing;
  // at least one must advance for Begin to reset. Device epochs never decrease.
  uint64_t host_frame = 0, guest_swap = 0, device = 0, scene = 0;
  bool operator==(const SceneColorEpoch&) const = default;
};
struct SceneColorRect {
  uint32_t left = 0, top = 0, right = 0, bottom = 0;
  bool operator==(const SceneColorRect&) const = default;
};
struct StrongResourceIdentity {
  // Borrowed COM pointer value, NOT ownership. The caller MUST retain resources
  // and descriptors through submission fences, and assign a fresh lifetime token
  // for every allocation/lifetime (including COM address reuse). No pointer-only
  // identity, guest EDRAM address, surface.last_write, or inferred bind versions.
  uintptr_t resource = 0;
  uint64_t retained_lifetime = 0, device_epoch = 0;
  uint32_t subresource = 0, width = 0, height = 0, format = 0, samples = 1;
  SceneColorRect valid_rect{};
  bool operator==(const StrongResourceIdentity&) const = default;
};
struct SceneColorContent {
  StrongResourceIdentity resource{};
  // Actual last content-producing GPU command's serial in this epoch. Binding
  // bookkeeping never increments it. The caller attests external inputs are
  // current; tone/copy/filter output serials must equal their issuance stamps.
  uint64_t serial = 0;
  bool operator==(const SceneColorContent&) const = default;
};
// Strictly increasing actual GPU command issuance stamps on ONE ordered queue
// / recording timeline, never PrepareDraw counters. Notify every intervening
// write/clear/copy/resolve/alias effect, including unclassified operations.
struct SceneColorCommand { SceneColorEpoch epoch{}; uint64_t serial = 0; };
struct SceneColorDrawProof {
  // audited_class includes the qualified city path and exact audited shader
  // semantics. geometry/viewport attest real, nonempty geometry, complete state
  // preparation and matching valid viewport/scissor. blend/depth attest suitable
  // overwrite state for postprocess, overlay state for HUD. bindings includes
  // current views, input layouts, constants and absence of unproven aliasing;
  // specialization rules out unqualified alpha/soft-particle/debug variants.
  bool audited_class = false, geometry = false, viewport = false;
  bool blend = false, depth = false, bindings = false, specialization = false;
  // Postprocess: full valid-rect overwrite, all RGB channels, no blend/occlusion.
  // HUD: matrix attests finite affine/no-perspective sprite-plane W and actual
  // screen placement, not a guessed orthographic matrix from a shader name.
  bool full_cover_overwrite = false, matrix = false;
};
struct SceneColorSources {
  SceneColorContent scene{}, luminance{}, bloom{};
  // Attested current scene/exposure/bloom ownership and resolve lineage. Merely
  // binding three finite, correctly sized resources does not establish this.
  bool scene_valid = false, luminance_valid = false, bloom_valid = false;
};
struct SceneColorCopyProof {
  // Explicit copy/resolve of precisely this tone version over the whole valid
  // rect. no_alias establishes no hidden effects on other protected versions;
  // source_preserved establishes the copy is read-only for its source.
  bool exact_tone_content = false, full_valid_rect = false;
  bool source_preserved = false, no_alias = false, resolve = false;
};
struct SceneColorBoundary {
  SceneColorEpoch epoch{};
  SceneColorContent output{};
  uint64_t tonemap_serial = 0, filter_serial = 0, before_hud_serial = 0;
};
class SceneColorBoundaryTracker {
 public:
  enum class State { kIdle, kTone, kFiltered, kEmitted, kInvalid };
  // Includes the original tone output. Bounded and allocation-free; exhaustion
  // invalidates rather than forgetting a live content dependency.
  static constexpr uint32_t kMaxToneCopies = 8;

  bool Begin(SceneColorEpoch epoch) {
    if (!ValidEpoch(epoch)) return Fail();
    if (begun_) {
      if (epoch == epoch_) return false; // Never rearm the same epoch, even invalid/emitted.
      if (epoch.device < epoch_.device ||
          (epoch.device == epoch_.device &&
           (epoch.host_frame < epoch_.host_frame || epoch.guest_swap < epoch_.guest_swap ||
            epoch.scene < epoch_.scene))) return Fail();
    }
    epoch_ = epoch;
    begun_ = true;
    last_command_ = tonemap_serial_ = filter_serial_ = 0;
    tone_count_ = 0;
    tone_ = {};
    final_ = {};
    state_ = State::kIdle;
    return true;
  }

  bool TonemapIssued(SceneColorCommand command, const SceneColorSources& sources,
                     SceneColorContent output, SceneColorDrawProof proof) {
    if (!Accept(command)) return false;
    if (state_ != State::kIdle || !PostProof(proof) ||
        !sources.scene_valid || !sources.luminance_valid || !sources.bloom_valid ||
        !Readable(sources.scene, command) || !Readable(sources.luminance, command) ||
        !Readable(sources.bloom, command) || !Written(output, command) ||
        SameSubresource(output.resource, sources.scene.resource) ||
        SameSubresource(output.resource, sources.luminance.resource) ||
        SameSubresource(output.resource, sources.bloom.resource)) return Fail();
    tone_[0] = output;
    tone_count_ = 1;
    tonemap_serial_ = command.serial;
    state_ = State::kTone;
    return true;
  }

  bool CopyIssued(SceneColorCommand command, SceneColorContent source,
                  SceneColorContent output, SceneColorCopyProof proof) {
    if (!Accept(command)) return false;
    if (state_ != State::kTone || !proof.exact_tone_content || !proof.full_valid_rect ||
        !proof.source_preserved || !proof.no_alias || !Readable(source, command) ||
        !Written(output, command) || !KnownTone(source) ||
        !SameExtent(source.resource, output.resource) ||
        SameSubresource(source.resource, output.resource) ||
        (proof.resolve ? (source.resource.samples <= 1 || output.resource.samples != 1)
                       : source.resource.samples != output.resource.samples)) return Fail();
    // Replacing an existing clone is safe only with exactly the same retained
    // view identity; invalidate that clone's old serial before publishing the new.
    for (uint32_t index = 0; index < tone_count_; ++index) {
      if (!SameSubresource(tone_[index].resource, output.resource)) continue;
      if (tone_[index].resource != output.resource) return Fail();
      tone_[index] = output;
      return true;
    }
    if (tone_count_ == kMaxToneCopies) return Fail();
    tone_[tone_count_++] = output;
    return true;
  }

  bool FilterIssued(SceneColorCommand command, SceneColorContent source,
                    SceneColorContent output, SceneColorDrawProof proof) {
    if (!Accept(command)) return false;
    if (state_ != State::kTone || !PostProof(proof) || !Readable(source, command) ||
        !Written(output, command) || !KnownTone(source) ||
        !SameExtent(source.resource, output.resource) ||
        source.resource.samples != output.resource.samples ||
        SameSubresource(source.resource, output.resource)) return Fail();
    for (uint32_t index = 0; index < tone_count_; ++index)
      if (SameSubresource(tone_[index].resource, output.resource) &&
          tone_[index].resource != output.resource) return Fail();
    final_ = output;
    filter_serial_ = command.serial;
    tone_count_ = 0; // Only the now-filtered output must remain immutable before HUD.
    state_ = State::kFiltered;
    return true;
  }

  // Every other ACTUALLY ISSUED write/clear/alias/copy/resolve must come here.
  // disjoint_from_other_protected is an explicit caller proof of no alias/effect
  // on protected content OTHER than this named subresource, not pointer inequality.
  // A root/clone can be overwritten before filtering if another proven tone copy
  // survives. Writes to the final filtered output ALWAYS invalidate. Other-target
  // work is preserved only with this explicit proof. Use Invalidate for unknown
  // alias effects whose affected subresource cannot be identified.
  bool WriteIssued(SceneColorCommand command, StrongResourceIdentity affected,
                   bool disjoint_from_other_protected = false) {
    if (!Accept(command)) return false;
    if (!ValidResource(affected) || !disjoint_from_other_protected) return Fail();
    if ((state_ == State::kFiltered || state_ == State::kEmitted) &&
        SameSubresource(affected, final_.resource)) return Fail();
    if (state_ == State::kTone) {
      for (uint32_t index = 0; index < tone_count_; ++index) {
        if (!SameSubresource(affected, tone_[index].resource)) continue;
        if (affected != tone_[index].resource) return Fail();
        tone_[index] = tone_[--tone_count_];
        if (!tone_count_) return Fail();
        break;
      }
    }
    return true;
  }

  // Call on the FIRST semantic HUD sprite, even if it targets another surface.
  // This is the committed before-command point after ALL preparation succeeds:
  // command.serial must identify the actual HUD issuance immediately following
  // this call. Never call speculatively from PrepareDraw/PSO creation. If recording
  // aborts or submission is dropped, Invalidate and discard any returned event.
  //
  // Returned content is borrowed and still mutable. Consume/snapshot it HERE in
  // queue order BEFORE issuing HUD; do not save its pointer for present/end-frame.
  // Retain the snapshot/resources/descriptors through the exact fence. Cross-queue
  // consumption requires caller synchronization. This proves no DLSS readiness.
  std::optional<SceneColorBoundary> BeforeHud(SceneColorCommand command,
                                             SceneColorContent current_output,
                                             SceneColorDrawProof proof) {
    if (!Accept(command)) return {};
    if (state_ == State::kEmitted) return {};
    if (state_ != State::kFiltered || !DrawProof(proof) || !proof.matrix ||
        !Readable(current_output, command) || current_output != final_) {
      (void)Fail();
      return {};
    }
    state_ = State::kEmitted;
    return SceneColorBoundary{epoch_, final_, tonemap_serial_, filter_serial_, command.serial};
  }

  void Invalidate() { (void)Fail(); }
  State state() const { return state_; }

 private:
  static bool ValidEpoch(SceneColorEpoch epoch) {
    return epoch.host_frame && epoch.guest_swap && epoch.device && epoch.scene;
  }
  bool ValidResource(const StrongResourceIdentity& resource) const {
    const auto& rect = resource.valid_rect;
    return resource.resource && resource.retained_lifetime &&
        resource.device_epoch == epoch_.device && resource.width && resource.height &&
        resource.format && resource.samples && rect.left < rect.right && rect.top < rect.bottom &&
        rect.right <= resource.width && rect.bottom <= resource.height;
  }
  bool Readable(const SceneColorContent& content, SceneColorCommand command) const {
    return ValidResource(content.resource) && content.serial && content.serial < command.serial;
  }
  bool Written(const SceneColorContent& content, SceneColorCommand command) const {
    return ValidResource(content.resource) && content.serial == command.serial;
  }
  static bool SameSubresource(const StrongResourceIdentity& a, const StrongResourceIdentity& b) {
    // Deliberately exclude lifetime/view metadata here: an apparent replacement
    // at the same pointer/subresource cannot masquerade as unrelated target work.
    return a.resource == b.resource && a.device_epoch == b.device_epoch && a.subresource == b.subresource;
  }
  static bool SameExtent(const StrongResourceIdentity& a, const StrongResourceIdentity& b) {
    return a.width == b.width && a.height == b.height && a.format == b.format && a.valid_rect == b.valid_rect;
  }
  static bool DrawProof(SceneColorDrawProof proof) {
    return proof.audited_class && proof.geometry && proof.viewport && proof.blend &&
        proof.depth && proof.bindings && proof.specialization;
  }
  static bool PostProof(SceneColorDrawProof proof) {
    return DrawProof(proof) && proof.full_cover_overwrite;
  }
  bool KnownTone(const SceneColorContent& content) const {
    for (uint32_t index = 0; index < tone_count_; ++index)
      if (tone_[index] == content) return true;
    return false;
  }
  bool Accept(SceneColorCommand command) {
    if (state_ == State::kInvalid) return false;
    if (!begun_ || command.epoch != epoch_ || !command.serial || command.serial <= last_command_)
      return Fail();
    last_command_ = command.serial;
    return true;
  }
  bool Fail() { state_ = State::kInvalid; return false; }

  SceneColorEpoch epoch_{};
  State state_ = State::kIdle;
  bool begun_ = false;
  uint64_t last_command_ = 0, tonemap_serial_ = 0, filter_serial_ = 0;
  std::array<SceneColorContent, kMaxToneCopies> tone_{};
  uint32_t tone_count_ = 0;
  SceneColorContent final_{};
};
} // namespace superman_returns::graphics
