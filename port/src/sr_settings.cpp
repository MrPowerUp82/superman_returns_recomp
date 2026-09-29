#include "sr_settings.h"

REXCVAR_DEFINE_BOOL(sr_skip_intro, false, "Superman Returns",
                    "Skip the publisher logo and legal videos at boot");

REXCVAR_DEFINE_INT32(sr_render_scale, 100, "Superman Returns",
                     "Internal render resolution in percent of 1280x720")
    .range(25, 100)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

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

REXCVAR_DEFINE_STRING(sr_gpu_pass_probe_path, "", "Superman Returns",
                      "Optional CSV of vertex buffers and constants for a selected shader pair")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(sr_gpu_probe_vs_hash, "", "Superman Returns",
                      "Vertex shader hash for the optional pass probe")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(sr_gpu_probe_ps_hash, "", "Superman Returns",
                      "Pixel shader hash for the optional pass probe")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(sr_gpu_skip_vs_hash, "", "Superman Returns",
                      "Diagnostic: restrict sr_gpu_skip_ps_hash to this vertex shader hash (empty = any)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(sr_gpu_skip_ps_hash, "", "Superman Returns",
                      "Diagnostic: skip draws using this pixel shader hash")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(sr_gpu_skip_edram_mode, -1, "Superman Returns",
                     "Diagnostic: restrict skipped draws to this EDRAM mode (-1 = any)")
    .range(-1, 6)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(sr_gpu_skip_rules, "", "Superman Returns",
                      "Diagnostic: skip draws/copies matching rules, e.g. "
                      "\"ps=<hash>;prim=13,mode=4;copy=1,surface=<n>\"")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
