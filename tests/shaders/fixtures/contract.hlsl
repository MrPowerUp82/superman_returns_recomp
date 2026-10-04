// Project-owned fixture. No game shader container or generated code.
#define CONAN_RECOMP 1
#include "shader_common.h"
#ifndef __spirv__
cbuffer VertexShaderConstants : register(b0,space4) { float4 g_VertexShaderConstantsArr[256]; };
cbuffer PixelShaderConstants : register(b1,space4) { float4 g_PixelShaderConstantsArr[256]; };
cbuffer SharedConstants : register(b2,space4) { DEFINE_SHARED_CONSTANTS() };
#define SR_LOAD_FLOAT4(STAGE,OFFSET) g_ ## STAGE ## ShaderConstantsArr[(OFFSET)/16]
uint g_SpecConstants() { return g_SpecConstantsRuntime; }
#endif
float4 VSMain(uint vertex : SV_VertexID) : SV_Position {
 float2 position=srVertexFetch(0,float(vertex),false).xy;
 float4 constants=SR_LOAD_FLOAT4(Vertex,0);
 return float4(position.x/32-1,1-position.y/32,constants.z,1);
}
float4 PSMain() : SV_Target0 {
 float4 color=SR_LOAD_FLOAT4(Pixel,0);
 if(BOOL_BIT(0) && LOOP_COUNT(g_LoopConstant(0))==1)color=float4(1,0,0,1);
#ifdef SR_VULKAN_BUFFERS
 uint texture=SR_SHARED_UINT(7*4)&0x7fffu;
 uint sampler=SR_SHARED_UINT(128+7*4)&0x7fffu;
 return color*g_Texture2DDescriptorHeap[texture].Sample(g_SamplerDescriptorHeap[sampler],float2(0.5,0.5));
#else
 return color*g_Texture2DDescriptorHeap[0].Sample(g_SamplerDescriptorHeap[0],float2(0.5,0.5));
#endif
}
