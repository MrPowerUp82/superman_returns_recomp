#include <rex/hook.h>

// Mid-function hooks named in superman_returns_manifest.toml (8231027C/82310280
// and the vehicle batch queue/append/flush/palette sites). They fed the old
// observed-object motion probe for the previous DLSS adapter, which is gone
// (docs/rt-dlss-port.md); DLSS's objects' motion now comes from patch 0030.
// They stay as no-ops so the recompiled code keeps linking without a codegen
// run; they never modified guest registers or memory.

void SrBeginSingleRigidPart(PPCRegister&, PPCRegister&, PPCRegister&, PPCRegister&, PPCRegister&,
    PPCRegister&, PPCRegister&, PPCRegister&, PPCRegister&, PPCRegister&, PPCRegister&) {}
void SrEndSingleRigidPart() {}
void SrBeginVehicleQueue(PPCRegister&, PPCRegister&, PPCRegister&, PPCRegister&, PPCRegister&,
    PPCRegister&, PPCRegister&) {}
void SrEndVehicleQueue() {}
void SrBeginVehicleAppend(PPCRegister&, PPCRegister&, PPCRegister&, PPCRegister&) {}
void SrEndVehicleAppend(PPCRegister&, PPCRegister&, PPCRegister&, PPCRegister&) {}
void SrBeginVehicleFlush(PPCRegister&, PPCRegister&, PPCRegister&, PPCRegister&, PPCRegister&, PPCRegister&,
    PPCRegister&, PPCRegister&, PPCRegister&) {}
void SrEndVehicleFlush(PPCRegister&) {}
void SrVehiclePaletteUpload(PPCRegister&, PPCRegister&, PPCRegister&) {}
