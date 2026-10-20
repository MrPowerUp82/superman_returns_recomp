// Project-owned GPU contract fixture using the real shared implementation.
#define CONAN_RECOMP 1
#define SR_VULKAN_BUFFERS 1
#ifdef SR_GLOBAL_TEST
#include "global_common.h"
#else
#include "shader_common.h"
#endif
struct Output {
 float4 position : SV_Position;
#ifdef SR_BATCH_PROBE
 nointerpolation uint draw_id : TEXCOORD0;
#endif
};
Output VSMain(uint vertex : SV_VertexID
#ifdef SR_GLOBAL_IA
 , [[vk::location(0)]] float2 input_position : POSITION0
#endif
#ifdef SR_BATCH_PROBE
 , [[vk::builtin("DrawIndex")]] uint draw_index : DRAWINDEX
#endif
) {
#ifdef SR_BATCH_PROBE
 sr_BatchDrawID=sr_BatchPush.base_draw+draw_index;
#ifdef SR_BATCH_PROBE_FORCE_DRAW0
 sr_BatchDrawID=0;
#endif
#endif
#ifdef SR_GLOBAL_IA
 float2 position=srVertexInput(0,float4(input_position,0,1)).xy;
#else
 float2 position=srVertexFetch(223,float(vertex),false).xy;
#endif
 float4 constants=SR_LOAD_FLOAT4(Vertex,0);
 Output output;output.position=float4(position+constants.xy,0,1);
#ifdef SR_BATCH_PROBE
 output.draw_id=sr_BatchDrawID;
#endif
 return output;
}
float4 PSMain(Output input) : SV_Target0 {
#ifdef SR_BATCH_PROBE
 sr_BatchDrawID=input.draw_id;
#endif
 float4 color=SR_LOAD_FLOAT4(Pixel,0);
 bool flag=BOOL_BIT(128);
#ifdef SR_BATCH_PROBE_ZERO_BOOL
 flag=false;
#endif
 uint loop=LOOP_COUNT(g_LoopConstant(16));
#ifdef SR_BATCH_PROBE_ZERO_LOOP
 loop=0;
#endif
 if(flag && loop==1) color.rgb*=0.5;
 uint texture=SR_SHARED_UINT(7*4)&0x7fffu;
#if SR_GLOBAL_TEXTURE_MODE == 1
 return color*tfetch2D(texture,SR_SHARED_UINT(128+7*4),float2(0.5,0.5),float2(0,0));
#elif SR_GLOBAL_TEXTURE_MODE == 2
 return color*tfetch3D(texture,SR_SHARED_UINT(128+7*4),float3(0.5,0.5,0.5));
#elif SR_GLOBAL_TEXTURE_MODE == 3
 CubeMapData data=(CubeMapData)0;data.cubeMapDirections[0]=float3(1,0,0);
 return color*tfetchCube(texture,SR_SHARED_UINT(128+7*4),float3(0,0,0),data);
#else
 return color*g_Texture2DDescriptorHeap[NonUniformResourceIndex(texture)].Load(int3(0,0,0));
#endif
}
