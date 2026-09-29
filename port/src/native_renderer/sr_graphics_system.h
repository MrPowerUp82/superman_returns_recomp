#pragma once

#include <memory>

namespace rex::system {
class IGraphicsSystem;
}

// The trace backend uses a project-owned command processor. It currently
// delegates drawing to ReXGlue's D3D12 Xenos path after recording each draw.
// It is a migration seam, not the finished native renderer.
std::unique_ptr<rex::system::IGraphicsSystem> CreateSrTraceGraphicsSystem();

// The stock D3D12 Xenos backend, built from the same in-process GPU sources.
// When those sources are linked, the executable registers the GPU cvars
// itself; loading the Xenos plugin DLL too would register them a second time,
// the runtime ignores the duplicates, and the plugin would silently run with
// defaults (e.g. render_target_path_d3d12=rov at ~2 FPS on Intel UHD).
std::unique_ptr<rex::system::IGraphicsSystem> CreateSrXenosGraphicsSystem();
