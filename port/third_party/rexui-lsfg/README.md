# Project-owned D3D12 presenter

Source: ReXGlue v0.10.0, f5337cdc947ff6d4c4196737e2c807a48f2a1fc2,
src/ui/d3d12/d3d12_presenter.cpp, its generated guest-output shaders,
and src/graphics/graphics_system.cpp.
License: BSD-3-Clause; see LICENSE and original source notices.

The class declaration and ABI remain those in the pinned SDK. The executable
supplies the presenter implementation and explicitly constructs it from the
in-process graphics system. The SDK is a DLL; linking an implementation alone
does not replace the presenter constructed inside it. No DLL injection,
vtable patch, header layout change or duplicate-symbol linker option is used.

Local additions: notify LSFG of new guest frames, try direct-DLL presentation
after the normal frame is composed, and release LSFG resources on destruction.
When LSFG is disabled or initialization fails, the original Present path runs.

All files here are open-source SDK code. No Lossless proprietary shader is
embedded; those are read from the user-provided DLL at runtime.
