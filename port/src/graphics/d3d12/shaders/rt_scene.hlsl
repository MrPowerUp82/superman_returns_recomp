// Original code; API semantics: https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html
// Build: dxc -T cs_6_5 -E TraceRays/TraceScene -O3 -WX -Fh <header> -Vn <symbol> rt_scene.hlsl
RaytracingAccelerationStructure scene : register(t0);
StructuredBuffer<float4> materials : register(t1);
struct RayInput {
  float3 origin; float t_min;
  float3 direction; float t_max;
  uint mask; uint3 reserved;
};
struct HitOutput {
  uint hit, instance_id, primitive_id, material_valid;
  float distance; float3 albedo;
};
StructuredBuffer<RayInput> rays : register(t2);
Texture2D<float> depth_buffer : register(t3);
Texture2D<float4> color_buffer : register(t4);
RWTexture2D<float> shadow_output : register(u0);
RWTexture2D<float4> reflection_output : register(u1);
RWStructuredBuffer<HitOutput> hits : register(u2);
cbuffer Parameters : register(b0) {
  row_major float4x4 inv_vp;
  float3 camera; uint flags;
  float3 light_direction; uint samples;
  uint width, height, ray_count, geometry_count;
  float bias, max_distance, cone_angle, clear_depth;
  float max_depth_delta; float3 padding;
};

[numthreads(64,1,1)]
void TraceRays(uint3 id : SV_DispatchThreadID) {
  if (id.x >= ray_count) return;
  RayInput input = rays[id.x];
  RayDesc ray;
  ray.Origin = input.origin; ray.TMin = input.t_min;
  ray.Direction = input.direction; ray.TMax = input.t_max;
  RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
  query.TraceRayInline(scene, RAY_FLAG_NONE, input.mask, ray);
  while (query.Proceed()) {}
  HitOutput hit = (HitOutput)0;
  if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
    hit.hit = 1;
    hit.instance_id = query.CommittedInstanceID();
    hit.primitive_id = query.CommittedPrimitiveIndex();
    hit.distance = query.CommittedRayT();
    if (hit.instance_id < geometry_count) {
      float4 material = materials[hit.instance_id];
      hit.material_valid = material.w != 0;
      if (hit.material_valid) hit.albedo = material.xyz;
    }
  }
  hits[id.x] = hit;
}

bool WorldPosition(uint2 pixel, out float3 world, out float depth) {
  depth = depth_buffer.Load(int3(pixel,0));
  float2 uv = (float2(pixel) + 0.5) / float2(width,height);
  float4 h = mul(inv_vp, float4(uv.x*2-1, 1-uv.y*2, depth, 1));
  world = 0;
  if (!isfinite(depth) || depth < 0 || depth > 1 || depth == clear_depth ||
      !all(isfinite(h)) || abs(h.w) < 1e-8) return false;
  world = h.xyz / h.w;
  return all(isfinite(world));
}

[numthreads(8,8,1)]
void TraceScene(uint3 id : SV_DispatchThreadID) {
  uint2 p = id.xy;
  if (p.x >= width || p.y >= height) return;
  shadow_output[p] = -1;
  reflection_output[p] = 0;
  float3 world; float depth;
  if (!WorldPosition(p, world, depth) || !all(isfinite(color_buffer.Load(int3(p,0))))) return;
  // Reconstruct a geometric receiver normal; reject depth discontinuities.
  uint2 px = uint2(p.x + 1 < width ? p.x + 1 : p.x - 1, p.y);
  uint2 py = uint2(p.x, p.y + 1 < height ? p.y + 1 : p.y - 1);
  float3 wx, wy; float dx, dy;
  if (!WorldPosition(px,wx,dx) || !WorldPosition(py,wy,dy) ||
      abs(dx-depth) > max_depth_delta || abs(dy-depth) > max_depth_delta) return;
  float3 normal = cross(wx-world, wy-world);
  float normal_length2 = dot(normal,normal);
  if (!all(isfinite(normal)) || !isfinite(normal_length2) || normal_length2 < 1e-16) return;
  normal = normalize(normal);
  float3 view = camera-world;
  float view_length2 = dot(view,view);
  if (!all(isfinite(view)) || !isfinite(view_length2) || view_length2 < 1e-16) return;
  if (dot(normal,view) < 0) normal = -normal;

  float3 tangent = normalize(cross(abs(light_direction.z)<0.9 ? float3(0,0,1) : float3(0,1,0), light_direction));
  float3 bitangent = cross(light_direction,tangent);
  float visible = 0;
  for (uint i=0; i<samples; ++i) {
    // Deterministic equal-solid-angle cone sampling, low=4/high=16 rays.
    float cos_theta = lerp(1.0,cos(cone_angle),(i+0.5)/samples);
    float sin_theta = sqrt(max(0,1-cos_theta*cos_theta));
    float phi = 2.39996323 * (i + ((p.x*13+p.y*7)%31));
    float3 direction = cos_theta*light_direction + sin_theta*(cos(phi)*tangent+sin(phi)*bitangent);
    RayDesc ray;
    ray.Origin = world+normal*bias; ray.TMin = bias;
    ray.Direction = direction; ray.TMax = max_distance;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(scene,RAY_FLAG_NONE,(flags&1) ? 1 : 3,ray);
    while (query.Proceed()) {}
    visible += query.CommittedStatus() == COMMITTED_NOTHING ? 1.0 : 0.0;
  }
  shadow_output[p] = visible / samples;
  if (!(flags&4)) return;
  RayDesc ray;
  ray.Origin = world+normal*bias; ray.TMin = bias;
  ray.Direction = reflect(-normalize(view),normal); ray.TMax = max_distance;
  RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
  query.TraceRayInline(scene,RAY_FLAG_NONE,(flags&2) ? 1 : 3,ray);
  while (query.Proceed()) {}
  if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT && query.CommittedInstanceID()<geometry_count) {
    float4 material = materials[query.CommittedInstanceID()];
    // One-bounce unlit hit albedo only; no inferred textures, BRDF, sky or game lighting.
    if (material.w != 0) reflection_output[p] = float4(material.xyz,1);
  }
}
