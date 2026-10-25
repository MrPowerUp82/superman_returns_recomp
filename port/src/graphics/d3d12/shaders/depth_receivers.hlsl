// Compare candidate depth receivers with actual current-scene triangle hits.
RaytracingAccelerationStructure scene : register(t0);
Texture2D<float> depth_buffer : register(t3);
struct ReceiverHit {
  uint valid,hit,primitive_id,reserved;
  float depth,expected_distance,measured_distance,relative_error;
};
RWStructuredBuffer<ReceiverHit> results : register(u2);
cbuffer Parameters : register(b0) {
  row_major float4x4 inv_vp;
  float3 camera; uint grid_width;
  float3 unused_light; uint grid_height;
  uint width,height,unused_ray_count,unused_geometry_count;
  float unused_bias,unused_distance,unused_cone,clear_depth;
  float4 unused_padding;
};
[numthreads(8,8,1)]
void ProbeDepth(uint3 id : SV_DispatchThreadID) {
  if(id.x>=grid_width || id.y>=grid_height) return;
  const uint index=id.y*grid_width+id.x;
  ReceiverHit hit=(ReceiverHit)0;
  const uint2 p=min(uint2((float2(id.xy)+0.5)*float2(width,height)/float2(grid_width,grid_height)),
                    uint2(width-1,height-1));
  const float depth=depth_buffer.Load(int3(p,0));
  hit.depth=depth;
  const float2 uv=(float2(p)+0.5)/float2(width,height);
  const float4 h=mul(inv_vp,float4(uv.x*2-1,1-uv.y*2,depth,1));
  if(isfinite(depth) && depth>=0 && depth<=1 && depth!=clear_depth && all(isfinite(h)) && abs(h.w)>1e-8) {
    const float3 direction=h.xyz/h.w-camera;
    const float distance=length(direction);
    if(isfinite(distance) && distance>0.001 && distance<1e7) {
      hit.valid=1; hit.expected_distance=distance; hit.relative_error=1;
      RayDesc ray;
      ray.Origin=camera; ray.TMin=0.001;
      ray.Direction=direction/distance; ray.TMax=distance*1.1+0.1;
      RayQuery<RAY_FLAG_FORCE_OPAQUE|RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
      query.TraceRayInline(scene,RAY_FLAG_NONE,3,ray);
      while(query.Proceed()) {}
      if(query.CommittedStatus()==COMMITTED_TRIANGLE_HIT) {
        hit.hit=1; hit.primitive_id=query.CommittedPrimitiveIndex();
        hit.measured_distance=query.CommittedRayT();
        hit.relative_error=abs(hit.measured_distance-distance)/max(distance,1.0);
      }
    }
  }
  results[index]=hit;
}
