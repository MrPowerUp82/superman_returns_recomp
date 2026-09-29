#pragma once

#include <memory>

namespace rex::system {
class IGraphicsSystem;
}

// The trace backend uses a project-owned command processor. It currently
// delegates drawing to ReXGlue's D3D12 Xenos path after recording each draw.
// It is a migration seam, not the finished native renderer.
std::unique_ptr<rex::system::IGraphicsSystem> CreateSrTraceGraphicsSystem();
