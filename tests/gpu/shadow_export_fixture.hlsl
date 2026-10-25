cbuffer Caster : register(b0,space4) {float4 c[256];};
cbuffer Shared : register(b2,space4) {
  float2 halfPixel : packoffset(c18.y);
  float4 screenTransform : packoffset(c27);
};
struct Output {
  float4 position : SV_Position;
  float4 texcoord[16] : TEXCOORD0;
  float4 color[2] : COLOR0;
  float4 normal[2] : NORMAL0;
};
Output ExportCaster(float3 world : POSITION) {
  Output o=(Output)0;
  float3 zvu=float3(dot(c[6],float4(world,1)),dot(c[5],float4(world,1)),dot(c[4],float4(world,1)));
  [unroll] for(int atlas=0;atlas<3;++atlas) if(c[10].x>atlas)
    zvu=zvu*c[7+atlas].w+c[7+atlas].zyx;
  o.position=float4(2*zvu.z-1,1-2*zvu.y,0.5,1);
  o.position.xy=o.position.xy*screenTransform.xy+screenTransform.zw+halfPixel;
  o.texcoord[0].x=32*(zvu.x+c[10].z);
  return o;
}
