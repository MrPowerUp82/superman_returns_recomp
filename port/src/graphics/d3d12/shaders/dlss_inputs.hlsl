// Scene inputs for DLSS. Build:
// dxc -T cs_6_0 -E DepthMotion -O3 -WX -Fh dlss_depth_motion_cs.h -Vn kDlssDepthMotionCS dlss_inputs.hlsl
// dxc -T ps_6_0 -E Composite -O3 -WX -Fh dlss_composite_ps.h -Vn kDlssCompositePS dlss_inputs.hlsl
//
// Own implementation. Motion comes from depth and the camera only: objects and
// deformation get the motion of a static point at their depth.
Texture2D<float> g_Depth : register(t0);
RWTexture2D<float2> g_Motion : register(u0);
RWTexture2D<float> g_DepthOut : register(u1);
cbuffer DepthMotionConstants : register(b0) {
  row_major float4x4 g_InverseCurrent;
  row_major float4x4 g_Previous;
  float2 g_Size;
};

[numthreads(8, 8, 1)]
void DepthMotion(uint3 id : SV_DispatchThreadID) {
  if (id.x >= uint(g_Size.x) || id.y >= uint(g_Size.y)) return;
  const float depth = g_Depth.Load(int3(id.xy, 0));
  g_DepthOut[id.xy] = depth;
  const float2 pixel = float2(id.xy) + 0.5;
  const float2 ndc = float2(pixel.x / g_Size.x * 2.0 - 1.0, 1.0 - pixel.y / g_Size.y * 2.0);
  float2 motion = float2(0.0, 0.0);
  const float4 world = mul(g_InverseCurrent, float4(ndc, depth, 1.0));
  if (abs(world.w) > 1e-6) {
    const float4 clip = mul(g_Previous, float4(world.xyz / world.w, 1.0));
    if (clip.w > 1e-6) {
      const float2 previous = clip.xy / clip.w;
      const float2 previous_pixel = float2(previous.x * 0.5 + 0.5, 0.5 - previous.y * 0.5) * g_Size;
      motion = previous_pixel - pixel;
    }
  }
  g_Motion[id.xy] = all(isfinite(motion)) ? motion : float2(0.0, 0.0);
}

// Pixels that changed after the scene filter (the HUD) keep the normal image.
Texture2D<float4> g_Final : register(t0);
Texture2D<float4> g_Scene : register(t1);
Texture2D<float4> g_Upscaled : register(t2);
SamplerState g_Sampler : register(s0);
cbuffer GammaRamp : register(b0) {
  uint4 g_GammaTable[64];  // Same layout and use as blit.hlsl.
  uint g_GammaEnabled;
};

uint GammaEntry(float v) {
  uint i = uint(round(saturate(v) * 255.0));
  return g_GammaTable[i >> 2][i & 3];
}

float4 Composite(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
  const float3 final_color = g_Final.SampleLevel(g_Sampler, uv, 0.0).rgb;
  const float3 scene = g_Scene.SampleLevel(g_Sampler, uv, 0.0).rgb;
  const bool changed = any(abs(final_color - scene) > 2.0 / 255.0);
  float3 c = changed ? final_color : g_Upscaled.SampleLevel(g_Sampler, uv, 0.0).rgb;
  if (g_GammaEnabled != 0) {
    c = float3((GammaEntry(c.r) >> 20) & 0x3FF, (GammaEntry(c.g) >> 10) & 0x3FF,
               GammaEntry(c.b) & 0x3FF) / 1023.0;
  }
  return float4(c, 1.0);
}
