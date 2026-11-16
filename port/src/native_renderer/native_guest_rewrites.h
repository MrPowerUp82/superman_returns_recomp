#pragma once
#include <cstdint>
#include <rex/ppc.h>

namespace superman_returns::native {
// sub_823055A8 by SR_NATIVE_INSTANCE_LOD mode (native_guest_rewrites.cpp).
void RunInstanceLod(PPCContext& ctx, uint8_t* base);
// sub_82319108 by SR_NATIVE_FRUSTUM_CULL mode.
void RunFrustumCull(PPCContext& ctx, uint8_t* base);
// sub_82305B88 by SR_NATIVE_OBJECT_LOD mode.
void RunObjectLod(PPCContext& ctx, uint8_t* base);
}  // namespace superman_returns::native
