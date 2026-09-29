// sr_preset (docs/performance-design.md): quality, performance or custom.
//
// A preset only fills the options it manages that are still at their
// compiled-in default (GetFlagSource == kDefault); a value from the command
// line, a REX_* environment variable or the .toml always wins, so one option
// can be changed without leaving the preset. "custom" touches nothing.
//
// The runtime's "Save to config" (F4) writes every option whose value differs
// from its default, including the ones a preset filled. Such a saved value
// then outranks the preset on the next start: after switching presets, remove
// the managed options below from the .toml (or set them explicitly).

#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "sr_settings.h"

REXCVAR_DEFINE_STRING(sr_preset, "quality", "Superman Returns",
                      "Preset: quality (current look), performance (post effects off) or "
                      "custom (no change). Only fills options still at their default")
    .allowed({"quality", "performance", "custom"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace {

struct PresetValue {
  const char* cvar;
  const char* quality;
  const char* performance;
};

// quality must stay the compiled-in default of every option: the Quality
// preset is the current look (docs/performance-design.md, decision 7).
constexpr PresetValue kPresetValues[] = {
    {"sr_post_effects", "true", "false"},
    // Hook for the internal resolution, not enabled yet: sr_render_scale is
    // experimental (render_scale.cpp; HUD and resolves at lower sizes are
    // unverified, and post_effects.h only matches the 1280x720 surfaces).
    // Once validated, set the performance value here, e.g. "75".
    {"sr_render_scale", "100", "100"},
};

}  // namespace

void ApplySrPreset() {
  const std::string preset = rex::cvar::GetFlagByName("sr_preset");
  if (preset != "quality" && preset != "performance") {
    REXLOG_INFO("sr_preset={}: options left as configured", preset);
    return;
  }
  const bool performance = preset == "performance";
  for (const PresetValue& v : kPresetValues) {
    const char* value = performance ? v.performance : v.quality;
    if (rex::cvar::GetFlagSource(v.cvar) != rex::cvar::Source::kDefault) {
      REXLOG_INFO("sr_preset={}: {} keeps its configured value {} (preset: {})", preset,
                  v.cvar, rex::cvar::GetFlagByName(v.cvar), value);
      continue;
    }
    if (rex::cvar::GetFlagByName(v.cvar) == value) continue;
    if (rex::cvar::SetFlagByName(v.cvar, value)) {
      REXLOG_INFO("sr_preset={}: {}={}", preset, v.cvar, value);
    } else {
      REXLOG_WARN("sr_preset={}: could not set {}={}", preset, v.cvar, value);
    }
  }
}
