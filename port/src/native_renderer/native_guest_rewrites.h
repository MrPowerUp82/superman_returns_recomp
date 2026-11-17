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
// sub_822F5290 by SR_NATIVE_DRAW_SEGMENTS (native_draw_segments.cpp).
void RunDrawSegments(PPCContext& ctx, uint8_t* base);
// sub_826092B8 by SR_NATIVE_RECORD_BITS (native_record_bits.cpp).
void RunRecordBits(PPCContext& ctx, uint8_t* base);
// sub_820F4920 / sub_820F4A30 by SR_NATIVE_CONSTANT_COPY (native_constant_copy.cpp).
// Returns false in mode 0 so the caller keeps its existing path.
bool RunConstantCopy(PPCContext& ctx, uint8_t* base, bool pixel,
                     void (*original)(PPCContext&, uint8_t*));
// sub_82783948 by SR_NATIVE_LAYER_WALK (native_layer_walk.cpp, main target).
void RunLayerWalk(PPCContext& ctx, uint8_t* base);
}  // namespace superman_returns::native
