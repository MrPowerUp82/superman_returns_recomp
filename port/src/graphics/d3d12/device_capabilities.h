#pragma once

struct ID3D12Device;

namespace superman_returns::graphics::d3d12 {
// Reports the actual selected device. Does not enable any rendering feature.
void LogDeviceCapabilities(ID3D12Device* device);
}
