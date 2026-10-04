// Project-owned Vulkan adapter for the reference depth_copy.hlsl encoding.
// Stencil-aspect Vulkan image views expose their byte in R, unlike D3D's G.
[[vk::binding(0,0)]] Texture2D<float> SourceDepth;
[[vk::binding(1,0)]] Texture2D<uint> SourceStencil;
[[vk::binding(2,0)]] RWByteAddressBuffer Output;
struct Region {int2 source;uint2 size;};
[[vk::push_constant]] ConstantBuffer<Region> region;
[numthreads(8,8,1)]
void CSMain(uint3 id:SV_DispatchThreadID) {
  if(any(id.xy>=region.size)) return;
  int3 p=int3(region.source+int2(id.xy),0);
  float depth=SourceDepth.Load(p);
  uint stencil=SourceStencil.Load(p)&255;
  uint d24=uint(round(saturate(depth)*16777215.0));
  uint pixel=id.y*region.size.x+id.x;
  Output.Store(pixel*4,asuint(depth));
  Output.Store((region.size.x*region.size.y+pixel)*4,stencil|(d24<<8));
}
