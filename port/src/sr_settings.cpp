#include "sr_settings.h"

REXCVAR_DEFINE_BOOL(sr_skip_intro, false, "Superman Returns",
                    "Skip the publisher logo and legal videos at boot");

REXCVAR_DEFINE_STRING(sr_renderer, "xenos", "Superman Returns",
                      "Graphics backend: xenos or trace (project command processor)")
    .allowed({"xenos", "trace"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(sr_gpu_trace_path, "gpu_trace.csv", "Superman Returns",
                      "CSV output path for the trace renderer")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(sr_gpu_trace_frames, 120, "Superman Returns",
                     "Number of guest frames to record in trace mode")
    .range(1, 10000)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(sr_gpu_trace_start_frame, 0, "Superman Returns",
                     "Guest frame at which trace recording starts")
    .range(0, 1000000)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(sr_gpu_trace_trigger_path, "", "Superman Returns",
                      "Begin tracing after this file appears (overrides start frame)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(sr_gpu_skip_vs_hash, "", "Superman Returns",
                      "Diagnostic: skip draws using this vertex shader hash and sr_gpu_skip_ps_hash")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(sr_gpu_skip_ps_hash, "", "Superman Returns",
                      "Diagnostic: skip draws using this pixel shader hash and sr_gpu_skip_vs_hash")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(sr_gpu_skip_edram_mode, -1, "Superman Returns",
                     "Diagnostic: restrict skipped draws to this EDRAM mode (-1 = any)")
    .range(-1, 6)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
