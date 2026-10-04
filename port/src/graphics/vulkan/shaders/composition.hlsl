// Project-owned presentation shader, matching native blit.hlsl's DC_LUT packing.
[[vk::binding(0,1)]] Texture2D<float4> Images[32];
[[vk::binding(0,2)]] SamplerState Samplers[32];
[[vk::binding(2,0)]] StructuredBuffer<uint> Display;
void VSMain(uint id:SV_VertexID,out float4 pos:SV_Position,out float2 uv:TEXCOORD0) {
  uv=float2((id<<1)&2,id&2);
  pos=float4(uv*float2(2,2)-float2(1,1),0,1);
}
uint GammaEntry(float value) {return Display[uint(round(saturate(value)*255.0))];}
float3 DecodeSRGB(float3 value) {return select(value<=.04045,value/12.92,pow((value+.055)/1.055,2.4));}
float4 PSMain(float4 pos:SV_Position,float2 uv:TEXCOORD0):SV_Target {
  float3 color=Images[0].SampleLevel(Samplers[0],uv,0).rgb;
  if(Display[256]) color=float3((GammaEntry(color.r)>>20)&1023,(GammaEntry(color.g)>>10)&1023,GammaEntry(color.b)&1023)/1023.0;
  // An sRGB attachment encodes on write; guest scan-out colors are already encoded.
  if(Display[257]) color=DecodeSRGB(color);
  return float4(color,1);
}
