// Same point/linear readback as resolve_sample.hlsl through the production ABI.
#define CONAN_RECOMP 1
#define SR_VULKAN_BUFFERS 1
#include "global_common.h"
float4 PSMain():SV_Target0 {
  return tfetch2D(SR_SHARED_UINT(7*4)&0x7fffu,SR_SHARED_UINT(128+7*4),float2(.5,.5),float2(0,0));
}
