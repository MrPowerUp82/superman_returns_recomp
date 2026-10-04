[[vk::binding(2,0)]] StructuredBuffer<uint> Options;
[[vk::binding(0,1)]] Texture2D<float4> Textures[32];
[[vk::binding(0,2)]] SamplerState Samplers[32];
struct Input {
  [[vk::location(0)]] float2 position : POSITION0;
  [[vk::location(11)]] float4 color : COLOR0;
  [[vk::location(13)]] float2 uv : TEXCOORD0;
};
struct Output {
  float4 position : SV_Position;
  [[vk::location(0)]] float4 color : COLOR0;
  [[vk::location(1)]] float2 uv : TEXCOORD0;
};
Output VSMain(Input input) {
  Output result;result.position=float4(input.position / float2(asfloat(Options[0]),asfloat(Options[1]))*2-1,0,1);result.color=input.color;result.uv=input.uv;return result;
}
float4 PSMain(Output input) : SV_Target0 {
  float4 color=input.color;
  if(Options[2]) color*=Textures[0].Sample(Samplers[0],input.uv);
  if(Options[3]) color.rgb=select(color.rgb<=0.04045,color.rgb/12.92,pow((color.rgb+0.055)/1.055,2.4));
  return color;
}
