#include "test_main.h"
#include "../../port/src/graphics/scene_color_boundary.h"
#include <array>
#include <limits>

using namespace superman_returns::graphics;
namespace {
constexpr SceneColorEpoch kEpoch{1909, 1908, 7, 11};
StrongResourceIdentity Resource(uint32_t id) {
  return {uintptr_t(0x1000u + id * 0x100u), id, kEpoch.device, 0,
          1280, 720, 28, 1, {0, 0, 1280, 720}};
}
SceneColorContent Content(uint32_t id, uint64_t serial) { return {Resource(id), serial}; }
SceneColorCommand Command(uint64_t serial, SceneColorEpoch epoch = kEpoch) { return {epoch, serial}; }
SceneColorDrawProof PostProof() { return {true, true, true, true, true, true, true, true, false}; }
SceneColorDrawProof HudProof() { return {true, true, true, true, true, true, true, false, true}; }
SceneColorCopyProof CopyProof() { return {true, true, true, true, false}; }
SceneColorSources Sources() { return {Content(1, 1), Content(2, 2), Content(3, 3), true, true, true}; }
struct Fixture {
  SceneColorBoundaryTracker tracker;
  Fixture() { (void)tracker.Begin(kEpoch); }
  bool Tone() { return tracker.TonemapIssued(Command(10), Sources(), Content(4, 10), PostProof()); }
  bool Filter() { return tracker.FilterIssued(Command(20), Content(4, 10), Content(6, 20), PostProof()); }
  std::optional<SceneColorBoundary> Hud(uint64_t serial = 30) {
    return tracker.BeforeHud(Command(serial), Content(6, 20), HudProof());
  }
  void Armed() { SR_CHECK(Tone()); SR_CHECK(Filter()); }
};
void Latched(Fixture& fixture) {
  SR_CHECK(fixture.tracker.state() == SceneColorBoundaryTracker::State::kInvalid);
  SR_CHECK(!fixture.tracker.Begin(kEpoch));
  SR_CHECK(!fixture.tracker.TonemapIssued(Command(40), Sources(), Content(4, 40), PostProof()));
  SR_CHECK(!fixture.tracker.FilterIssued(Command(50), Content(4, 40), Content(6, 50), PostProof()));
  SR_CHECK(!fixture.tracker.BeforeHud(Command(60), Content(6, 50), HudProof()));
}
} // namespace

SR_TEST(scene_color_issues_one_boundary_with_distinct_epoch_and_content_serials) {
  Fixture f;
  SR_CHECK(!f.tracker.Begin(kEpoch)); // A duplicate Begin cannot erase progress.
  f.Armed();
  const auto boundary = f.Hud();
  SR_CHECK(boundary.has_value());
  if (boundary) {
    SR_CHECK(boundary->epoch == kEpoch);
    SR_CHECK(boundary->output == Content(6, 20));
    SR_CHECK_EQ(boundary->tonemap_serial, 10u);
    SR_CHECK_EQ(boundary->filter_serial, 20u);
    SR_CHECK_EQ(boundary->before_hud_serial, 30u);
  }
  SR_CHECK(f.tracker.state() == SceneColorBoundaryTracker::State::kEmitted);
  SR_CHECK(!f.Hud(31));
  SR_CHECK(!f.tracker.Begin(kEpoch));
  SR_CHECK(!f.Hud(32));
}

SR_TEST(scene_color_does_not_arm_from_unissued_or_missing_postprocesses) {
  {
    Fixture f;
    SR_CHECK(!f.tracker.TonemapIssued(Command(0), Sources(), Content(4, 10), PostProof()));
    Latched(f);
  }
  {
    Fixture f; // Bind/PSO/shader hash/size observations have no arming API.
    SR_CHECK(!f.Hud());
    Latched(f);
  }
  {
    Fixture f;
    SR_CHECK(f.Tone());
    SR_CHECK(!f.Hud());
    Latched(f);
  }
  {
    Fixture f;
    SR_CHECK(!f.Filter());
    Latched(f);
  }
}

SR_TEST(scene_color_tonemap_and_filter_require_every_audited_postprocess_attestation) {
  for (auto flag : {&SceneColorDrawProof::audited_class, &SceneColorDrawProof::geometry,
                    &SceneColorDrawProof::viewport, &SceneColorDrawProof::blend,
                    &SceneColorDrawProof::depth, &SceneColorDrawProof::bindings,
                    &SceneColorDrawProof::specialization, &SceneColorDrawProof::full_cover_overwrite}) {
    auto proof = PostProof();
    proof.*flag = false;
    Fixture tone;
    SR_CHECK(!tone.tracker.TonemapIssued(Command(10), Sources(), Content(4, 10), proof));
    Latched(tone);
    Fixture filter;
    SR_CHECK(filter.Tone());
    SR_CHECK(!filter.tracker.FilterIssued(Command(20), Content(4, 10), Content(6, 20), proof));
    Latched(filter);
  }
}

SR_TEST(scene_color_hud_requires_audited_sprite_geometry_state_bindings_and_runtime_matrix) {
  for (auto flag : {&SceneColorDrawProof::audited_class, &SceneColorDrawProof::geometry,
                    &SceneColorDrawProof::viewport, &SceneColorDrawProof::blend,
                    &SceneColorDrawProof::depth, &SceneColorDrawProof::bindings,
                    &SceneColorDrawProof::specialization, &SceneColorDrawProof::matrix}) {
    Fixture f;
    f.Armed();
    auto proof = HudProof();
    proof.*flag = false;
    SR_CHECK(!f.tracker.BeforeHud(Command(30), Content(6, 20), proof));
    Latched(f);
  }
}

SR_TEST(scene_color_requires_valid_scene_luminance_and_bloom_content_provenance) {
  for (auto flag : {&SceneColorSources::scene_valid, &SceneColorSources::luminance_valid,
                    &SceneColorSources::bloom_valid}) {
    Fixture f;
    auto sources = Sources();
    sources.*flag = false;
    SR_CHECK(!f.tracker.TonemapIssued(Command(10), sources, Content(4, 10), PostProof()));
    Latched(f);
  }
  for (auto field : {&SceneColorSources::scene, &SceneColorSources::luminance, &SceneColorSources::bloom}) {
    for (uint64_t serial : {uint64_t(0), uint64_t(10), uint64_t(100)}) {
      Fixture f;
      auto sources = Sources();
      (sources.*field).serial = serial;
      SR_CHECK(!f.tracker.TonemapIssued(Command(10), sources, Content(4, 10), PostProof()));
      Latched(f);
    }
    Fixture f;
    auto sources = Sources();
    (sources.*field).resource.device_epoch++;
    SR_CHECK(!f.tracker.TonemapIssued(Command(10), sources, Content(4, 10), PostProof()));
    Latched(f);
  }
}

SR_TEST(scene_color_rejects_ambiguous_second_tonemap_or_filter_in_the_epoch) {
  for (bool after_filter : {false, true}) {
    Fixture f;
    SR_CHECK(f.Tone());
    if (after_filter) SR_CHECK(f.Filter());
    SR_CHECK(!f.tracker.TonemapIssued(Command(21), Sources(), Content(7, 21), PostProof()));
    Latched(f);
  }
  Fixture f;
  f.Armed();
  SR_CHECK(!f.tracker.FilterIssued(Command(21), Content(4, 10), Content(7, 21), PostProof()));
  Latched(f);
}

SR_TEST(scene_color_first_hud_on_another_target_or_before_filter_poison_the_epoch) {
  for (int stage = 0; stage < 3; ++stage) {
    Fixture f;
    if (stage >= 1) SR_CHECK(f.Tone());
    if (stage >= 2) SR_CHECK(f.Filter());
    SR_CHECK(!f.tracker.BeforeHud(Command(30), Content(9, 20), HudProof()));
    Latched(f);
  }
}

SR_TEST(scene_color_hud_requires_exact_resource_lifetime_device_subresource_rect_and_content) {
  const auto original = Content(6, 20);
  std::array<SceneColorContent, 13> changed;
  changed.fill(original);
  changed[0].resource.resource += 0x100;
  changed[1].resource.retained_lifetime++;
  changed[2].resource.device_epoch++;
  changed[3].resource.subresource++;
  changed[4].resource.width++;
  changed[5].resource.height++;
  changed[6].resource.format++;
  changed[7].resource.samples++;
  changed[8].resource.valid_rect.left++;
  changed[9].resource.valid_rect.top++;
  changed[10].resource.valid_rect.right--;
  changed[11].resource.valid_rect.bottom--;
  changed[12].serial++;
  for (auto content : changed) {
    Fixture f;
    f.Armed();
    SR_CHECK(!f.tracker.BeforeHud(Command(30), content, HudProof()));
    Latched(f);
  }
}

SR_TEST(scene_color_epoch_or_command_order_mismatch_is_fail_closed) {
  std::array<SceneColorEpoch, 4> changed;
  changed.fill(kEpoch);
  changed[0].host_frame++;
  changed[1].guest_swap++;
  changed[2].device++;
  changed[3].scene++;
  for (auto epoch : changed) {
    Fixture f;
    f.Armed();
    SR_CHECK(!f.tracker.BeforeHud(Command(30, epoch), Content(6, 20), HudProof()));
    Latched(f);
  }
  for (uint64_t serial : {uint64_t(0), uint64_t(9), uint64_t(10)}) {
    Fixture f;
    SR_CHECK(f.Tone());
    SR_CHECK(!f.tracker.FilterIssued(Command(serial), Content(4, 10), Content(6, serial), PostProof()));
    Latched(f);
  }
  for (uint64_t serial : {uint64_t(19), uint64_t(20)}) {
    Fixture f;
    f.Armed();
    SR_CHECK(!f.Hud(serial));
    Latched(f);
  }
}

SR_TEST(scene_color_real_new_epoch_can_reset_but_same_or_rolled_back_epoch_cannot) {
  Fixture f;
  f.tracker.Invalidate();
  Latched(f);
  auto next = kEpoch;
  next.host_frame++;
  SR_CHECK(f.tracker.Begin(next));
  SR_CHECK(f.tracker.state() == SceneColorBoundaryTracker::State::kIdle);
  SR_CHECK(!f.tracker.Begin(kEpoch));
  SR_CHECK(f.tracker.state() == SceneColorBoundaryTracker::State::kInvalid);
  SR_CHECK(!f.tracker.Begin(next));
  next.guest_swap++;
  SR_CHECK(f.tracker.Begin(next));
  next.scene++;
  SR_CHECK(f.tracker.Begin(next));
  next = {1, 2, kEpoch.device + 1, 1};
  SR_CHECK(f.tracker.Begin(next)); // A fresh device can restart local counters.
  SR_CHECK(!f.tracker.Begin(kEpoch));
}

SR_TEST(scene_color_rejects_incomplete_resource_identity_and_bad_rect) {
  std::array<StrongResourceIdentity, 9> invalid;
  invalid.fill(Resource(4));
  invalid[0].resource = 0;
  invalid[1].retained_lifetime = 0;
  invalid[2].device_epoch = 0;
  invalid[3].width = 0;
  invalid[4].height = 0;
  invalid[5].format = 0;
  invalid[6].samples = 0;
  invalid[7].valid_rect.right = 1281;
  invalid[8].valid_rect.bottom = 0;
  for (auto resource : invalid) {
    Fixture f;
    SR_CHECK(!f.tracker.TonemapIssued(Command(10), Sources(), {resource, 10}, PostProof()));
    Latched(f);
  }
  for (uint64_t serial : {uint64_t(0), uint64_t(9), uint64_t(11)}) {
    Fixture f;
    SR_CHECK(!f.tracker.TonemapIssued(Command(10), Sources(), Content(4, serial), PostProof()));
    Latched(f);
  }
  for (int field = 0; field < 4; ++field) {
    SceneColorEpoch epoch = kEpoch;
    if (field == 0) epoch.host_frame = 0;
    if (field == 1) epoch.guest_swap = 0;
    if (field == 2) epoch.device = 0;
    if (field == 3) epoch.scene = 0;
    SceneColorBoundaryTracker tracker;
    SR_CHECK(!tracker.Begin(epoch));
  }
}

SR_TEST(scene_color_filter_consumes_current_exact_tone_content_not_lookalike_or_stale_version) {
  std::array<SceneColorContent, 5> bad;
  bad.fill(Content(4, 10));
  bad[0].serial = 9;
  bad[1].resource = Resource(5);
  bad[2].resource.retained_lifetime++;
  bad[3].resource.subresource++;
  bad[4].resource.valid_rect.right--;
  for (auto input : bad) {
    Fixture f;
    SR_CHECK(f.Tone());
    SR_CHECK(!f.tracker.FilterIssued(Command(20), input, Content(6, 20), PostProof()));
    Latched(f);
  }
  Fixture f;
  SR_CHECK(f.Tone());
  SR_CHECK(!f.tracker.FilterIssued(Command(20), Content(4, 10), Content(6, 19), PostProof()));
  Latched(f);
}

SR_TEST(scene_color_explicit_copy_propagates_tone_to_clone_and_survives_source_clear) {
  Fixture f;
  SR_CHECK(f.Tone());
  SR_CHECK(f.tracker.CopyIssued(Command(11), Content(4, 10), Content(5, 11), CopyProof()));
  SR_CHECK(f.tracker.WriteIssued(Command(12), Resource(4), true));
  SR_CHECK(f.tracker.FilterIssued(Command(20), Content(5, 11), Content(6, 20), PostProof()));
  const auto boundary = f.Hud();
  SR_CHECK(boundary.has_value());
  if (boundary) SR_CHECK_EQ(boundary->tonemap_serial, 10u);
}

SR_TEST(scene_color_clone_chain_preserves_exact_content_after_original_and_intermediate_writes) {
  Fixture f;
  SR_CHECK(f.Tone());
  SR_CHECK(f.tracker.CopyIssued(Command(11), Content(4, 10), Content(5, 11), CopyProof()));
  SR_CHECK(f.tracker.CopyIssued(Command(12), Content(5, 11), Content(8, 12), CopyProof()));
  SR_CHECK(f.tracker.WriteIssued(Command(13), Resource(4), true));
  SR_CHECK(f.tracker.WriteIssued(Command(14), Resource(5), true));
  SR_CHECK(f.tracker.FilterIssued(Command(20), Content(8, 12), Content(6, 20), PostProof()));
  SR_CHECK(f.Hud().has_value());
}

SR_TEST(scene_color_resolve_requires_explicit_exact_content_and_compatible_sample_counts) {
  Fixture f;
  auto multisampled = Content(4, 10);
  multisampled.resource.samples = 4;
  SR_CHECK(f.tracker.TonemapIssued(Command(10), Sources(), multisampled, PostProof()));
  auto proof = CopyProof();
  proof.resolve = true;
  SR_CHECK(f.tracker.CopyIssued(Command(11), multisampled, Content(5, 11), proof));
  SR_CHECK(f.tracker.FilterIssued(Command(20), Content(5, 11), Content(6, 20), PostProof()));
  SR_CHECK(f.Hud().has_value());
  Fixture bad;
  SR_CHECK(bad.Tone());
  SR_CHECK(!bad.tracker.CopyIssued(Command(11), Content(4, 10), Content(5, 11), proof));
  Latched(bad);
}

SR_TEST(scene_color_copy_requires_every_proof_and_rejects_missing_stale_or_changed_lineage) {
  for (auto flag : {&SceneColorCopyProof::exact_tone_content, &SceneColorCopyProof::full_valid_rect,
                    &SceneColorCopyProof::source_preserved, &SceneColorCopyProof::no_alias}) {
    Fixture f;
    SR_CHECK(f.Tone());
    auto proof = CopyProof();
    proof.*flag = false;
    SR_CHECK(!f.tracker.CopyIssued(Command(11), Content(4, 10), Content(5, 11), proof));
    Latched(f);
  }
  for (int kind = 0; kind < 9; ++kind) {
    Fixture f;
    SR_CHECK(f.Tone());
    auto source = Content(4, 10), destination = Content(5, 11);
    if (kind == 0) source.serial = 9;
    if (kind == 1) source.resource = Resource(9);
    if (kind == 2) destination.serial = 10;
    if (kind == 3) destination.resource.valid_rect.right--;
    if (kind == 4) destination.resource.format++;
    if (kind == 5) destination.resource.width++;
    if (kind == 6) destination.resource.samples++;
    if (kind == 7) destination.resource = source.resource;
    if (kind == 8) destination.resource.device_epoch++;
    SR_CHECK(!f.tracker.CopyIssued(Command(11), source, destination, CopyProof()));
    Latched(f);
  }
  Fixture f;
  SR_CHECK(!f.tracker.CopyIssued(Command(11), Content(4, 10), Content(5, 11), CopyProof()));
  Latched(f);
}

SR_TEST(scene_color_overwritten_tone_version_cannot_be_used_even_when_another_clone_survives) {
  Fixture f;
  SR_CHECK(f.Tone());
  SR_CHECK(f.tracker.CopyIssued(Command(11), Content(4, 10), Content(5, 11), CopyProof()));
  SR_CHECK(f.tracker.WriteIssued(Command(12), Resource(4), true));
  SR_CHECK(!f.Filter());
  Latched(f);
}

SR_TEST(scene_color_unknown_write_clear_alias_or_copy_cannot_preserve_armed_output) {
  for (bool disjoint : {false, true}) {
    Fixture f;
    f.Armed();
    SR_CHECK(!f.tracker.WriteIssued(Command(21), Resource(6), disjoint));
    Latched(f);
  }
  Fixture f;
  f.Armed();
  SR_CHECK(!f.tracker.CopyIssued(Command(21), Content(4, 10), Content(6, 21), CopyProof()));
  Latched(f);
}

SR_TEST(scene_color_other_target_work_survives_only_explicit_disjoint_attestation) {
  for (bool disjoint : {false, true}) {
    Fixture f;
    SR_CHECK(f.Tone());
    SR_CHECK(f.tracker.WriteIssued(Command(11), Resource(9), disjoint) == disjoint);
    if (disjoint) {
      SR_CHECK(f.Filter());
      SR_CHECK(f.tracker.WriteIssued(Command(21), Resource(9), true));
      SR_CHECK(f.Hud().has_value());
    } else Latched(f);
  }
  Fixture f;
  f.Armed();
  SR_CHECK(!f.tracker.WriteIssued(Command(21), Resource(9))); // No implicit no-alias inference.
  Latched(f);
}

SR_TEST(scene_color_loss_of_last_tone_copy_or_unproven_source_clear_invalidates_epoch) {
  Fixture last;
  SR_CHECK(last.Tone());
  SR_CHECK(!last.tracker.WriteIssued(Command(11), Resource(4), true));
  Latched(last);
  Fixture aliases;
  SR_CHECK(aliases.Tone());
  SR_CHECK(aliases.tracker.CopyIssued(Command(11), Content(4, 10), Content(5, 11), CopyProof()));
  SR_CHECK(!aliases.tracker.WriteIssued(Command(12), Resource(4), false));
  Latched(aliases);
}

SR_TEST(scene_color_identity_change_cannot_be_disguised_as_disjoint_other_target_work) {
  for (bool filtered : {false, true}) {
    Fixture f;
    SR_CHECK(f.Tone());
    if (filtered) SR_CHECK(f.Filter());
    auto changed = Resource(filtered ? 6u : 4u);
    changed.retained_lifetime++;
    SR_CHECK(!f.tracker.WriteIssued(Command(21), changed, true));
    Latched(f);
  }
}

SR_TEST(scene_color_bounded_lineage_capacity_fails_closed_without_rearming) {
  Fixture f;
  SR_CHECK(f.Tone());
  for (uint32_t index = 1; index < SceneColorBoundaryTracker::kMaxToneCopies; ++index) {
    const uint64_t serial = 10 + index;
    SR_CHECK(f.tracker.CopyIssued(Command(serial), Content(4, 10), Content(20 + index, serial), CopyProof()));
  }
  SR_CHECK(!f.tracker.CopyIssued(Command(100), Content(4, 10), Content(99, 100), CopyProof()));
  Latched(f);
}

SR_TEST(scene_color_recording_abort_or_dropped_submission_explicitly_invalidates) {
  Fixture f;
  f.Armed();
  f.tracker.Invalidate();
  SR_CHECK(!f.Hud());
  Latched(f);
}

SR_TEST(scene_color_filter_rejects_resized_reformatted_or_in_place_outputs) {
  for (int kind = 0; kind < 6; ++kind) {
    Fixture f;
    SR_CHECK(f.Tone());
    auto output = Content(6, 20);
    if (kind == 0) output.resource.width++;
    if (kind == 1) output.resource.height++;
    if (kind == 2) output.resource.format++;
    if (kind == 3) output.resource.valid_rect.right--;
    if (kind == 4) output.resource.samples++;
    if (kind == 5) output.resource = Resource(4);
    SR_CHECK(!f.tracker.FilterIssued(Command(20), Content(4, 10), output, PostProof()));
    Latched(f);
  }
  Fixture f;
  auto sources = Sources();
  sources.scene = Content(4, 1);
  SR_CHECK(!f.tracker.TonemapIssued(Command(10), sources, Content(4, 10), PostProof()));
  Latched(f);
}

SR_TEST(scene_color_copy_replacement_keeps_only_new_clone_content_serial) {
  for (uint64_t input_serial : {uint64_t(11), uint64_t(12)}) {
    Fixture f;
    SR_CHECK(f.Tone());
    SR_CHECK(f.tracker.CopyIssued(Command(11), Content(4, 10), Content(5, 11), CopyProof()));
    SR_CHECK(f.tracker.CopyIssued(Command(12), Content(4, 10), Content(5, 12), CopyProof()));
    SR_CHECK(f.tracker.FilterIssued(Command(20), Content(5, input_serial), Content(6, 20), PostProof())
             == (input_serial == 12));
    if (input_serial == 12) SR_CHECK(f.Hud().has_value());
    else Latched(f);
  }
}

SR_TEST(scene_color_final_output_is_independent_of_old_inputs_but_not_of_its_own_next_write) {
  Fixture f;
  f.Armed();
  SR_CHECK(f.tracker.WriteIssued(Command(21), Resource(4), true));
  SR_CHECK(f.Hud().has_value());
  SR_CHECK(!f.tracker.WriteIssued(Command(31), Resource(6), true));
  SR_CHECK(!f.Hud(32)); // A mutable boundary pointer is never a later-frame snapshot.
}

SR_TEST(scene_color_new_epoch_must_qualify_fresh_outputs_and_does_not_carry_prior_filter) {
  Fixture f;
  f.Armed();
  auto epoch = kEpoch;
  epoch.host_frame++;
  epoch.guest_swap++;
  SR_CHECK(f.tracker.Begin(epoch));
  SR_CHECK(!f.tracker.BeforeHud(Command(30, epoch), Content(6, 20), HudProof()));
  SR_CHECK(f.tracker.state() == SceneColorBoundaryTracker::State::kInvalid);
  epoch.scene++;
  SR_CHECK(f.tracker.Begin(epoch));
  SR_CHECK(f.tracker.TonemapIssued(Command(10, epoch), Sources(), Content(4, 10), PostProof()));
  SR_CHECK(f.tracker.FilterIssued(Command(20, epoch), Content(4, 10), Content(6, 20), PostProof()));
  const auto boundary = f.tracker.BeforeHud(Command(30, epoch), Content(6, 20), HudProof());
  SR_CHECK(boundary.has_value());
  if (boundary) SR_CHECK(boundary->epoch == epoch);
}

SR_TEST(scene_color_issuance_serial_wrap_or_events_without_begin_fail_closed) {
  SceneColorBoundaryTracker tracker;
  SR_CHECK(!tracker.TonemapIssued(Command(10), Sources(), Content(4, 10), PostProof()));
  Fixture f;
  f.Armed();
  SR_CHECK(f.tracker.WriteIssued(Command(std::numeric_limits<uint64_t>::max()), Resource(9), true));
  SR_CHECK(!f.Hud(1));
  Latched(f);
}

SR_TEST(scene_color_filter_cannot_reuse_a_still_retained_tone_pointer_as_another_lifetime) {
  Fixture f;
  SR_CHECK(f.Tone());
  SR_CHECK(f.tracker.CopyIssued(Command(11), Content(4, 10), Content(5, 11), CopyProof()));
  auto output = Content(4, 20);
  output.resource.retained_lifetime++;
  SR_CHECK(!f.tracker.FilterIssued(Command(20), Content(5, 11), output, PostProof()));
  Latched(f);
}
