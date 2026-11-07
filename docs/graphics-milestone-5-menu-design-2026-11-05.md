# Graphics milestone 5: pause menu and shared preferences (2026-11-05)

The authorized implementation extends the existing pause overlay without new guest hooks. Controller/keyboard capture and pause lifetime stay in the existing menu. The first five option indices remain RT shadows, shadow softness, character shadows, reflections and DLSS; new rows append API, output resolution, FPS limit, FXAA, MSAA and internal scale. DLSS persistence is 0 Off, 1 Quality, 2 Balanced, 3 Performance, 4 DLAA.

The menu presents requested and backend-reported applied values separately. Capability remains a backend report, never inferred from a request. API is a restart preference; resource changes are applied only by a renderer at its safe frame boundary, and unsupported values retain a reason. Reflections do not implicitly require shadows. No fatal diagnostic is introduced.

Approaches considered: keep a companion graphics file (simple but divergent); overwrite the launcher INI (loses unrelated edits); share the launcher INI with field-level merging (chosen by implementer under the user's instruction to finish). Each writer reads the latest file under the same exclusive sidecar lock, replaces only its locally changed keys, preserves unknown lines and replaces the file atomically. Legacy companion files are read only for missing RT/DLSS fields; they are never deleted. Invalid individual INI fields leave their defaults/CLI values intact.

The launcher passes its exact INI path through SR_LAUNCHER_SETTINGS. This avoids guessing the launcher directory from the game executable when they differ. Standalone game launches derive the standard installation ID from the lower-case executable directory (including trailing separator), SHA256 UTF8, first 12 upper-case hex digits, in LocalAppData/SupermanReturns/launcher. The legacy path argument is migration input only.

Implementation sequence: extend pure option/value metadata and menu navigation; implement shared INI codec/dirty merge and Windows persistence; expose launcher RT/DLSS controls and pass exact path; add focused pure tests and integration instructions. The worker validates the launcher and pure tests; root owns release builds, GPU execution and renderer integration. Backend integration must attest actual execution, apply safe hot changes and report failures. Resolution means host window dimensions; internal scaling retains the existing 1280x720 surface scale semantics.

## Integration decisions during implementation

Output resolution selects the native presenter output texture; changing it in the menu does not resize the physical desktop window. At launch the same preference initializes the host window. Internal scale is a multiplier of the selected output size with DLSS off, or of the SDK-recommended DLSS render size with DLSS on. The renderer uses a uniform scale in guest 1280x720 coordinates and validates both rounded dimensions against the SDK range. Unsupported multipliers retain intent and return to raster with an explicit reason. DLSS requests retain FXAA/MSAA preferences, but temporal AA owns AA and motion inputs require single-sample scene surfaces; the backend reports these actual constraints.

The native hot-settings helper waits covering native and presenter queue fences before releasing scene/output resources or rewriting descriptors. The presenter holds its own mailbox images; its refresher callback copies from native targets synchronously, while UI paint retains independent mailbox ComPtrs. A failed output allocation leaves previous output targets alive. A failed settings write is retried on a later UI frame. Files over 1 MiB or unreadable current files are not overwritten.

Vulkan integrates its own backend epoch and reports the actual accepted mailbox image dimensions and consumed presentation pacing. Its final immutable snapshot may be resampled to the requested output size using a core Vulkan blit only when the actual image format supports source/destination blits; otherwise it keeps its actual dimensions and reports the mismatch. Captured replay target scale, MSAA overrides, scene FXAA, DXR and NGX remain explicit unavailable capabilities in Vulkan. Its stored enhancement requests are preserved without feeding unsupported values into Vulkan startup validation.

### Viewport fracionário e aplicação DLSS ao vivo

O teste de menu em 1280x720/Quality revelou scene853x480, mas viewport853x479.8125.
O gate full_viewport corretamente recusou captura (1646draws,0capturados).
Arredondar as duas bordas de cada eixo escalado pela mesma regra dos targets
e scissors, preservando adjacência e alinhamento do pixel. Não relaxar o gate
para aceitar clips/depth de viewports diferentes. Testes: saída853x480 e
viewports adjacentes em escalas Quality/Balanced, negativos, upscale e scale1.
