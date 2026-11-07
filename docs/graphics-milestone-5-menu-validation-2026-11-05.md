> A interface gráfica descrita originalmente foi substituída pela UI EA APT nativa. Consulte graphics-milestone-5-native-apt-validation-2026-11-05.md para a evidência atual; os contratos de preferências/backend abaixo continuam úteis.

# M5 menu integration and verification

The design was written before implementation in graphics-milestone-5-menu-design-2026-11-05.md. Settings persist in the launcher's existing LocalAppData/SupermanReturns/launcher/<installhash>.ini, never in a newly written companion file. SR_LAUNCHER_SETTINGS passes the exact launcher path; standalone fallback hashes the lower-case executable directory with its trailing backslash using SHA256 UTF8 and first 12 uppercase hex digits. The old path argument to InitializeGraphicsSettings is read-only migration input; no app.h patch is needed.

| Row | Option | Values/cvar |
|---|---|---|
| 0 | RT shadows | bool sr_rt_shadows |
| 1 | RT quality | 1 Low / 2 High sr_shadow_softness |
| 2 | Characters | bool sr_character_shadows |
| 3 | Reflections | bool sr_rt_reflections |
| 4 | DLSS | 0 off / 1 quality / 2 balanced / 3 performance / 4 dlaa; sr_dlss string |
| 5 | API | 0 d3d12 / 1 vulkan; sr_native_api; restart |
| 6 | Output | 0 960x540 / 1 1280x720 / 2 1600x900 / 3 1920x1080 / 4 2560x1440 / 5 3840x2160; window_width + window_height |
| 7 | Pacing | 0 unlimited / 1 30 / 2 60 / 3 120; sr_native_fps_limit int |
| 8 | FXAA | bool sr_native_fxaa |
| 9 | MSAA | 0 1 sample / 1 4 samples / 2 8 samples; sr_native_msaa_samples int |
| 10 | Internal multiplier | 0 50 / 1 75 / 2 100 / 3 125 / 4 150 / 5 200 / 6 300 / 7 400 percent; sr_native_render_scale double |

D3D12 integration: native_graphics_options.inc is included inside the renderer namespace. ApplyGraphicsFrameOptions runs after EnsureInitialized and before allocator/list reset in BeginFrame. AppendGraphicsHostReport runs before PublishBackend. Declarations/state are in native_renderer.h through root integration. Root owns mode-aware BeginDlssFrame/RecordDlssFrame, NGX dynamic dimension validation, and actual FXAA scene execution. The helper uses fxaa_scene_applied_ for the FXAA report. It never sets applied RT or DLSS from requests.

Vulkan integration: provider initializes the Vulkan backend epoch, injects GameFrame::output_extent, and publishes API/image extent/pacing after accepted real presentation. GameFrame resamples only the final immutable mailbox snapshot, using format-supported core blits; pool/submission shared ownership protects old snapshots. Unsupported replay enhancements retain honest unavailable reasons. Unsupported Vulkan settings stay out of effective validation cvars while their stored requests survive.

Validation executed:

- `.tools/dotnet-launcher/dotnet.exe build launcher/SupermanReturnsLauncher.csproj --no-restore -v minimal`: success, zero warnings/errors.
- `.tools/dotnet-launcher/dotnet.exe run --project tests/launcher/LauncherChecks.csproj --no-restore`: zero failed checks, including dirty merge, unknown keys/comments, five DLSS modes, exact settings path and migration preservation.
- Isolated C++20 clang executable from main.cpp, test_graphics_options.cpp, test_graphics_options_input.cpp, test_graphics_options_guest_pause.cpp, test_graphics_preferences.cpp, test_graphics_milestone5.cpp and pure options/runtime/preferences sources: 27 tests, zero failed checks.
- CMake build target `sr_vulkan_tests` in build/tests-vulkan followed by its executable: 109 tests, zero failed checks. These use pure policies/injected dispatch; no GPU/game execution.

Root registers test_graphics_milestone5.cpp in tests/native/CMakeLists.txt and runs the full renderer build and runtime validation. Required runtime checks include controller capture/release, API restart persistence, output-size readback, 30/60/120/unlimited pacing, NGX mode execution/fence attestation, invalid multiplier fallback and FXAA/MSAA constraints.

Limits: hot output changes do not resize the desktop HWND; DLSS render extents must lie in the SDK-reported range; Vulkan scene scaling, MSAA overrides, FXAA, DXR and NGX remain explicitly unsupported. The menu reports requested/applied/capability separately for all these cases.
