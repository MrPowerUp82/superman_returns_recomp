#include <rex/hook.h>
#if defined(SR_HAS_NATIVE) && SR_HAS_NATIVE
#include "../native_renderer/instance_probe.h"
#endif

// Paired manifest hooks at 8231027C/82310280. They observe the original
// synchronous count-one call; no guest registers or memory are modified.
void SrBeginSingleRigidPart(PPCRegister& stack,PPCRegister& packet,PPCRegister& counts,
    PPCRegister& matrices,PPCRegister& groups,PPCRegister& rows,PPCRegister& extra,
    PPCRegister& enabled,PPCRegister& slot,PPCRegister& cursor,PPCRegister& owner) {
#if defined(SR_HAS_NATIVE) && SR_HAS_NATIVE
  if(!superman_returns::native::InstanceProbeEnabled()) return;
  PPCContext ctx{};ctx.r1=stack;ctx.r3=packet;ctx.r5=counts;ctx.r6=matrices;ctx.r7=groups;
  ctx.r8=rows;ctx.r9=extra;ctx.r10=enabled;ctx.r24=slot;ctx.r29=cursor;ctx.r30=owner;
  ctx.lr=0x82310280;
  auto* base=rex::system::kernel_state()->memory()->virtual_membase();
  superman_returns::native::BeginSingleInstanceProbe(base,ctx);
#endif
}
void SrEndSingleRigidPart() {
#if defined(SR_HAS_NATIVE) && SR_HAS_NATIVE
  superman_returns::native::EndSingleInstanceProbe();
#endif
}

void SrBeginVehicleQueue(PPCRegister& stack,PPCRegister& packet,PPCRegister& descriptor,
    PPCRegister& count,PPCRegister& slot,PPCRegister& cursor,PPCRegister& owner) {
#if defined(SR_HAS_NATIVE) && SR_HAS_NATIVE
  if(!superman_returns::native::InstanceProbeEnabled()) return;
  PPCContext ctx{};ctx.r1=stack;ctx.r4=packet;ctx.r6=descriptor;ctx.r14=count;
  ctx.r23=slot;ctx.r29=cursor;ctx.r30=owner;
  superman_returns::native::BeginVehicleEnqueueProbe(rex::system::kernel_state()->memory()->virtual_membase(),ctx);
#endif
}
void SrEndVehicleQueue() {
#if defined(SR_HAS_NATIVE) && SR_HAS_NATIVE
  superman_returns::native::EndVehicleEnqueueProbe();
#endif
}
void SrBeginVehicleAppend(PPCRegister& stack,PPCRegister& entry,PPCRegister& descriptor,PPCRegister& group) {
#if defined(SR_HAS_NATIVE) && SR_HAS_NATIVE
  if(!superman_returns::native::InstanceProbeEnabled()) return;
  PPCContext ctx{};ctx.r1=stack;ctx.r3=entry;ctx.r4=descriptor;ctx.r5=group;
  superman_returns::native::BeginVehicleAppendProbe(rex::system::kernel_state()->memory()->virtual_membase(),ctx);
#endif
}
void SrEndVehicleAppend(PPCRegister& stack,PPCRegister& entry,PPCRegister& descriptor,PPCRegister& count_offset) {
#if defined(SR_HAS_NATIVE) && SR_HAS_NATIVE
  if(!superman_returns::native::InstanceProbeEnabled()) return;
  PPCContext ctx{};ctx.r1=stack;ctx.r30=entry;ctx.r31=descriptor;ctx.r27=count_offset;
  superman_returns::native::EndVehicleAppendProbe(rex::system::kernel_state()->memory()->virtual_membase(),ctx);
#endif
}
void SrBeginVehicleFlush(PPCRegister& packet,PPCRegister& asset,PPCRegister& counts,PPCRegister& matrices,
    PPCRegister& groups,PPCRegister& rows,PPCRegister& extra,PPCRegister& enabled,PPCRegister& entry) {
#if defined(SR_HAS_NATIVE) && SR_HAS_NATIVE
  if(!superman_returns::native::InstanceProbeEnabled()) return;
  PPCContext ctx{};ctx.r3=packet;ctx.r4=asset;ctx.r5=counts;ctx.r6=matrices;ctx.r7=groups;
  ctx.r8=rows;ctx.r9=extra;ctx.r10=enabled;ctx.r31=entry;
  superman_returns::native::BeginVehicleFlushProbe(rex::system::kernel_state()->memory()->virtual_membase(),ctx);
#endif
}
void SrEndVehicleFlush(PPCRegister& entry) {
#if defined(SR_HAS_NATIVE) && SR_HAS_NATIVE
  if(superman_returns::native::InstanceProbeEnabled()) superman_returns::native::EndVehicleFlushProbe(entry.u32);
#endif
}
void SrVehiclePaletteUpload(PPCRegister& first,PPCRegister& data,PPCRegister& rows) {
#if defined(SR_HAS_NATIVE) && SR_HAS_NATIVE
  if(!superman_returns::native::InstanceProbeEnabled() || first.u32!=100) return;
  superman_returns::native::ObserveVehiclePaletteProbe(rex::system::kernel_state()->memory()->virtual_membase(),first.u32,data.u32,rows.u32);
#endif
}
