# M5 implementation plan
1. Extend graphics_options.h/cpp metadata, preserving first five indices and existing input capture. Validate ranges and separate restart request from backend applied state.
2. Extend graphics_preferences.h/cpp with canonical launcher INI keys, partial reads, legacy v1 compatibility and dirty field merges. Add tests for DLSS modes, unknown line retention and concurrent field changes.
3. Update graphics_settings.cpp to use the exact launcher path environment override or the standard installation hash fallback, lock shared writes, migrate only missing keys, retry failed writes and synchronize cvars.
4. Update graphics_options_dialog.cpp to show a scrolling row window and requested/applied/capability detail for every option.
5. Update launcher settings and controls with DLSS/RT modes, preserve unknown keys under an exclusive lock and pass SR_LAUNCHER_SETTINGS in CreateLaunch. Provide backend integration contract to root.
Worker validation: launcher build and tests, isolated native graphics tests, Vulkan foundation tests. Root validation: release renderer build, runtime controller menu and device execution capture. No game/GPU execution is performed by this worker.
