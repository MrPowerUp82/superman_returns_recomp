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
void ReportInstanceProbe() noexcept;
void NoteInstanceProbeDraw(uint8_t* base,uint32_t device,const char* kind,uint32_t count,uint32_t caller,uint32_t stack) noexcept;
}
