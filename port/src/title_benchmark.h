#pragma once
// Title-screen benchmark (--sr_benchmark): boots straight into the title
// flythrough, hides its UI, measures and reports. Rules: title_benchmark_policy.h.
#include "title_benchmark_format.h"
#include <string>

namespace superman_returns::benchmark {
bool Active();
// Renderers (D3D12 worker, Vulkan replay) at the end of each frame.
void NoteRendererFrame(bool scene_drawn);
// Guest present hook (sub_82112050): advances the clock, saves on completion.
void OnGuestPresent();
void SetGpuName(std::string name);
// Copy for the results screen (UI thread).
Result Snapshot();
}  // namespace superman_returns::benchmark
