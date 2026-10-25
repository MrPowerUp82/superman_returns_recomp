// Own post-VS extraction. VS output already includes the host screen/pixel fix.
#ifndef WORLD_TEXCOORD
#define WORLD_TEXCOORD 0
#endif
#if WORLD_TEXCOORD == 0
cbuffer CasterConstants : register(b0,space4) { float4 caster[256]; };
cbuffer Shared : register(b2,space4) {
  float2 halfPixel : packoffset(c18.y);
  float4 screenTransform : packoffset(c27);
};
#endif
// Match the translated VS's complete declared output register order. D3D12
// stage linkage does not remap a TEXCOORD2 in GS register1 onto VS register3.
struct Input {
  float4 position : SV_Position;
  float4 texcoord[16] : TEXCOORD0;
  float4 color[2] : COLOR0;
  float4 normal[2] : NORMAL0;
};
struct Output { float4 position : SV_Position; float3 world : TEXCOORD15; };
[maxvertexcount(3)]
void ExtractTriangle(triangle Input v[3], inout PointStream<Output> stream) {
  float3 positions[3];
  bool valid=true;
  [unroll] for(uint i=0;i<3;++i) {
#if WORLD_TEXCOORD == 0
    // Audited 57A00/C7C0 affine shadow export. Undo raster pixel transform,
    // constant clip Z and all atlas transforms using the original VS bank.
    float4 clip=v[i].position;
    clip.xy=(clip.xy-(screenTransform.zw+halfPixel)*clip.w)/screenTransform.xy;
    float3 zvu=float3(v[i].texcoord[0].x/32-caster[10].z,(1-clip.y)/2,(clip.x+1)/2);
    valid=valid && abs(clip.w-1)<1e-6 && all(abs(caster[3]-float4(0,0,0,1))<1e-5) &&
          all(isfinite(screenTransform)) && all(abs(screenTransform.xy)>1e-12) &&
          all(isfinite(halfPixel)) && isfinite(caster[10].x) && isfinite(caster[10].z);
    [unroll] for(int atlas=2;atlas>=0;--atlas) if(caster[10].x>atlas) {
      const float4 scale=caster[7+atlas];
      valid=valid && all(isfinite(scale)) && abs(scale.w)>1e-12;
      zvu=(zvu-scale.zyx)/scale.w;
    }
    const float3 u=caster[4].xyz,vrow=caster[5].xyz,z=caster[6].xyz;
    const float3 vz=cross(vrow,z),zu=cross(z,u),uv=cross(u,vrow);
    const float determinant=dot(u,vz);
    valid=valid && all(isfinite(caster[4])) && all(isfinite(caster[5])) && all(isfinite(caster[6])) &&
          abs(determinant)>max(1e-30,length(u)*length(vrow)*length(z)*1e-6);
    positions[i]=((zvu.z-caster[4].w)*vz+(zvu.y-caster[5].w)*zu+(zvu.x-caster[6].w)*uv)/determinant;
    valid=valid && all(isfinite(positions[i]));
#else
    positions[i]=v[i].texcoord[WORLD_TEXCOORD].xyz;
    valid=valid && all(isfinite(positions[i]));
#endif
  }
  [unroll] for(uint i=0;i<3;++i) {
    Output o;
    o.position=v[i].position;
    o.world=valid ? positions[i] : float3(0,0,0);
    stream.Append(o);
  }
}
