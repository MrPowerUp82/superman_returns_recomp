#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(sr_lsfg, false, "Superman Returns",
                   "Experimental 2x frame generation from a local Lossless.dll")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(sr_lsfg_dll, "", "Superman Returns",
                     "Path to the user-provided Lossless.dll; used only when sr_lsfg is on")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(sr_lsfg_mode, "performance", "Superman Returns",
                     "LSFG processing mode")
    .allowed({"performance", "quality"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
