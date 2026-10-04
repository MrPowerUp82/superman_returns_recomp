// Reuse the kit-authored 32bpp pack/unpack arithmetic and its source attribution.
// The reference graphics entry points are unreachable from this compute entry.
#include "../../../native_renderer/shaders/edram_alias.hlsl"
[[vk::binding(0,0)]] Texture2D<float4> SourceColor;
[[vk::binding(2,0)]] RWByteAddressBuffer Output;
struct Region {uint source_kind;uint destination_kind;uint2 size;};
[[vk::push_constant]] ConstantBuffer<Region> region;
[numthreads(8,8,1)]
void CSMain(uint3 id:SV_DispatchThreadID) {
  if(any(id.xy>=region.size)) return;
  float4 color=Unpack(Pack(SourceColor.Load(int3(id.xy,0)),region.source_kind),region.destination_kind);
  uint pixel=id.y*region.size.x+id.x;
  if(region.destination_kind==2) {
    uint4 half_value=f32tof16(color);
    Output.Store2(pixel*8,uint2(half_value.x|(half_value.y<<16),half_value.z|(half_value.w<<16)));
  } else Output.Store(pixel*4,Pack(color,region.destination_kind));
}
