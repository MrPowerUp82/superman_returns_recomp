#pragma once
#include <cstdint>
#include <rex/ppc.h>
#include "../graphics/observed_instance_lifetimes.h"
namespace superman_returns::native {
// Opt-in association diagnostics only. These never attest TemporalGeometrySample
// identity or classify a renderable as a character.
bool InstanceProbeEnabled() noexcept;
graphics::ObservedRenderInvocation CaptureInstanceProbeInvocation(uint8_t* base) noexcept;
void InvokeInstanceProbeRender(uint32_t address,PPCFunc* original,PPCContext& ctx,uint8_t* base);
void ArmInstanceProbe() noexcept;
void BeginSingleInstanceProbe(uint8_t* base,PPCContext& ctx) noexcept;
void EndSingleInstanceProbe() noexcept;
void BeginVehicleEnqueueProbe(uint8_t* base,PPCContext& ctx) noexcept;
void EndVehicleEnqueueProbe() noexcept;
void BeginVehicleAppendProbe(uint8_t* base,PPCContext& ctx) noexcept;
void EndVehicleAppendProbe(uint8_t* base,PPCContext& ctx) noexcept;
void BeginVehicleFlushProbe(uint8_t* base,PPCContext& ctx) noexcept;
void EndVehicleFlushProbe(uint32_t entry) noexcept;
void ObserveVehiclePaletteProbe(uint8_t* base,uint32_t first,uint32_t source,uint32_t rows) noexcept;
void ReportInstanceProbe() noexcept;
void NoteInstanceProbeDraw(uint8_t* base,uint32_t device,const char* kind,uint32_t count,uint32_t caller,uint32_t stack) noexcept;
}
