#pragma once

#include <cstdint>
#include <memory>

namespace rex::system {
class IGraphicsSystem;
}

// The trace backend uses a project-owned command processor. It currently
// delegates drawing to ReXGlue's D3D12 Xenos path after recording each draw.
// It is a migration seam, not the finished native renderer.
std::unique_ptr<rex::system::IGraphicsSystem> CreateSrTraceGraphicsSystem();

// The same project-owned command processor without the CSV trace, for the
// native renderer's A/B mode (sr_renderer=native, sr_native_ab_mode): Xenos
// renders and presents; after each guest swap it notifies the observer below
// and, at the swaps listed in sr_native_ab_swaps, writes its presented output
// to sr_native_dump_dir as s<swap>_xenos_output.raw (same header as the
// native renderer's dumps: u32 width, height, DXGI format, row pitch).
std::unique_ptr<rex::system::IGraphicsSystem> CreateSrAbGraphicsSystem();

// Called on the GPU thread after the project command processor processes
// guest swap N (1 = first) with its register file (kit SDK fork:
// rex::perf::RegisterGpuSwapCallback). One observer; nullptr clears it.
using SrXenosSwapObserver = void (*)(uint64_t swap_number, const uint32_t* regs, uint32_t count);
void SetSrXenosSwapObserver(SrXenosSwapObserver observer);

// DC_LUT_30_COLOR gamma ramp (256 entries) of the running project command
// processor; false when none runs (kit fork: IGraphicsSystem::GetGammaRamp256).
bool GetSrXenosGammaRamp256(uint32_t* out_entries);

// The stock D3D12 Xenos backend, built from the same in-process GPU sources.
// When those sources are linked, the executable registers the GPU cvars
// itself; loading the Xenos plugin DLL too would register them a second time,
// the runtime ignores the duplicates, and the plugin would silently run with
// defaults (e.g. render_target_path_d3d12=rov at ~2 FPS on Intel UHD).
std::unique_ptr<rex::system::IGraphicsSystem> CreateSrXenosGraphicsSystem();
