#pragma once

// Superman Returns options, shown in the F4 settings overlay under
// "Superman Returns" and persisted to the runtime's .toml config.

#include <rex/cvar.h>

REXCVAR_DECLARE(bool, sr_skip_intro);
REXCVAR_DECLARE(int32_t, sr_render_scale);
REXCVAR_DECLARE(bool, sr_post_effects);

// Fills the options sr_preset manages that are still at their default
// (sr_preset.cpp). Call once at startup, before they are read.
void ApplySrPreset();
