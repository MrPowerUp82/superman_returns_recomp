// Builds without the NVIDIA NGX SDK (SR_DLSS=OFF): DLSS stays off. dlss.cpp
// is the implementation (docs/rt-dlss-port.md).

#include "native_renderer.h"

namespace superman_returns::native {

struct DlssState {};
void DlssDeleter::operator()(DlssState* state) const { delete state; }

int Renderer::DlssMode() const { return 0; }
bool Renderer::DlssApplied() const { return false; }
float Renderer::DlssSceneRatio() const { return 1.0f; }
uint32_t Renderer::DlssSceneVariant() const { return 0; }
bool Renderer::DlssMotionDraw() const { return false; }
const D3D12_CPU_DESCRIPTOR_HANDLE* Renderer::DlssMotionTarget() { return nullptr; }
void Renderer::DlssDrawKey(uint64_t, uint64_t, uint32_t, uint32_t, uint32_t) { dlss_draw_key_ = 0; }
D3D12_GPU_VIRTUAL_ADDRESS Renderer::DlssPrevVsConstants(bool& valid) {
  valid = false;
  return 0;
}
void Renderer::DlssCaptureCamera() {}
void Renderer::DlssJitter(float[2]) {}
void Renderer::DlssOnDepthResolve(HostSurface&) {}
void Renderer::DlssOnColorResolve(HostSurface&) {}
void Renderer::DlssEndFrame() {}
void Renderer::DlssReleaseFeature() {}

}  // namespace superman_returns::native
