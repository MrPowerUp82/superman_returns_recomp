#pragma once
#include <cstdint>
#include <rex/ppc.h>

namespace superman_returns::native {
// sub_823055A8 by SR_NATIVE_INSTANCE_LOD mode (native_guest_rewrites.cpp).
void RunInstanceLod(PPCContext& ctx, uint8_t* base);
}  // namespace superman_returns::native
