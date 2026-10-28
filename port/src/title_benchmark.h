#pragma once
// Title-screen benchmark (--sr_benchmark): boots straight into the title
// flythrough, hides its UI, measures and reports. Rules: title_benchmark_policy.h.
#include "title_benchmark_format.h"
#include <string>

namespace superman_returns::benchmark {
bool Active();
// Renderers (D3D12 worker, Vulkan replay) at the end of each frame.
void NoteRendererFrame(bool scene_drawn);
// FMV player start (sub_8235F2D0): the attract demo video.
void NoteVideoStarted();
// Guest present hook (sub_82112050): advances the clock, saves on completion.
void OnGuestPresent();
void SetGpuName(std::string name);
// Copy for the results screen (UI thread).
Result Snapshot();
// Results are already on disk: flush the log and end the process. The SDK's
// deferred quit does not return from the title screen.
[[noreturn]] void ExitNow(int code);
}  // namespace superman_returns::benchmark
