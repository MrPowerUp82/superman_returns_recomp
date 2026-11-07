// Original implementation. Build each entry as cs_6_5 with DXC -O3 -WX.
#if defined(CONVERT_CLIPS)
ByteAddressBuffer clip_vertices : register(t0);
RWByteAddressBuffer world_vertices : register(u0);
cbuffer ConvertParameters : register(b0) {
  row_major float4x4 inverse_final_vp;
  uint vertex_count,clip_offset,clip_stride,world_offset;
};
[numthreads(64,1,1)]
void ConvertClips(uint3 id:SV_DispatchThreadID) {
  uint base=id.x*3;
  if(base>=vertex_count) return;
  float3 positions[3];bool valid=true;
  [unroll] for(uint i=0;i<3;++i) {
    float4 clip=asfloat(clip_vertices.Load4(clip_offset+(base+i)*clip_stride));
    float4 h=mul(inverse_final_vp,clip);
    valid=valid && all(isfinite(clip)) && all(isfinite(h)) && abs(h.w)>1e-8;
    positions[i]=h.xyz/(abs(h.w)>1e-8?h.w:1);
    valid=valid && all(isfinite(positions[i])) && all(abs(positions[i])<1e10);
  }
  [unroll] for(uint j=0;j<3;++j)
    world_vertices.Store3(world_offset+(base+j)*12,asuint(valid?positions[j]:float3(0,0,0)));
}
#else
RaytracingAccelerationStructure visible_scene:register(t0);
Texture2D<float> scene_depth:register(t1);
Texture2D<float4> lit_scene:register(t2);
Texture2D<float2> motion_pixels:register(t3);
Texture2D<uint> motion_validity:register(t4);
Texture2D<float> raw_shadow:register(t5);
Texture2D<float4> raw_reflection:register(t6);
Texture2D<float> previous_shadow:register(t7);
Texture2D<float4> previous_reflection:register(t8);
Texture2D<float> previous_depth:register(t9);
Texture2D<uint> reactivity:register(t10);
RWTexture2D<float> shadow_out:register(u0);
RWTexture2D<float4> reflection_out:register(u1);
RWTexture2D<float> depth_out:register(u2);
RWByteAddressBuffer execution_counts:register(u3);
cbuffer Parameters:register(b0) {
  row_major float4x4 inverse_vp;
  row_major float4x4 final_vp;
  row_major float4x4 previous_vp;
  float3 camera;uint flags;
  float3 light;uint shadow_samples;
  uint width,height,trace_width,trace_height;
  float bias,shadow_distance,clear_depth,max_depth_delta;
  float shadow_cone,reflection_distance,reflection_cone,depth_rejection;
  uint reflection_samples;float history_weight;uint frame_seed;uint unused;
  float2 current_jitter,previous_jitter;
};
bool Position(uint2 p,out float3 world,out float depth) {
  depth=scene_depth.Load(int3(p,0));world=0;
  float2 uv=(float2(p)+.5)/float2(width,height);
  float4 h=mul(inverse_vp,float4(uv.x*2-1,1-uv.y*2,depth,1));
  if(!isfinite(depth) || depth<0 || depth>1 || depth==clear_depth ||
     !all(isfinite(h)) || abs(h.w)<1e-8) return false;
  world=h.xyz/h.w;return all(isfinite(world));
}
float3 Cone(float3 direction,float angle,uint index,uint count,uint2 pixel) {
  float3 tangent=normalize(cross(abs(direction.z)<.9?float3(0,0,1):float3(0,1,0),direction));
  float3 bitangent=cross(direction,tangent);
  float c=lerp(1,cos(angle),(index+.5)/count),s=sqrt(max(0,1-c*c));
  float phi=2.39996323*(index+((pixel.x*13+pixel.y*7+frame_seed)%31));
  return c*direction+s*(cos(phi)*tangent+sin(phi)*bitangent);
}
[numthreads(8,8,1)]
void TraceVisible(uint3 id:SV_DispatchThreadID) {
  if(id.x>=trace_width || id.y>=trace_height) return;
  uint2 p=min(uint2((float2(id.xy)+.5)*float2(width,height)/float2(trace_width,trace_height)),uint2(width-1,height-1));
  shadow_out[id.xy]=-1;reflection_out[id.xy]=0;
  float3 world,wx,wy;float depth,dx,dy;
  if(!Position(p,world,depth)) return;
  uint2 px=uint2(p.x+1<width?p.x+1:p.x-1,p.y),py=uint2(p.x,p.y+1<height?p.y+1:p.y-1);
  if(!Position(px,wx,dx) || !Position(py,wy,dy) || abs(dx-depth)>max_depth_delta || abs(dy-depth)>max_depth_delta) return;
  float3 normal=cross(wx-world,wy-world),view=camera-world;
  if(!all(isfinite(normal)) || dot(normal,normal)<1e-16 || dot(view,view)<1e-16) return;
  normal=normalize(normal);view=normalize(view);if(dot(normal,view)<0)normal=-normal;
  if(flags&1) {
    float visible=0;
    for(uint i=0;i<shadow_samples;++i) {
      RayDesc ray;ray.Origin=world+normal*bias;ray.TMin=bias;
      ray.Direction=Cone(light,shadow_cone,i,shadow_samples,p);ray.TMax=shadow_distance;
      RayQuery<RAY_FLAG_FORCE_OPAQUE|RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES|RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
      query.TraceRayInline(visible_scene,RAY_FLAG_NONE,(flags&4)?1:3,ray);
      while(query.Proceed()) {}
      visible+=query.CommittedStatus()==COMMITTED_NOTHING?1:0;
    }
    shadow_out[id.xy]=visible/shadow_samples;
  }
  if(!(flags&2)) return;
  float3 reflected=reflect(-view,normal),color=0;uint accepted=0;
  for(uint sample=0;sample<reflection_samples;++sample) {
    RayDesc ray;ray.Origin=world+normal*bias;ray.TMin=bias;
    ray.Direction=Cone(reflected,reflection_cone,sample,reflection_samples,p);ray.TMax=reflection_distance;
    RayQuery<RAY_FLAG_FORCE_OPAQUE|RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(visible_scene,RAY_FLAG_NONE,(flags&8)?1:3,ray);
    while(query.Proceed()) {}
    if(query.CommittedStatus()!=COMMITTED_TRIANGLE_HIT) continue;
    float3 hit=ray.Origin+ray.Direction*query.CommittedRayT();
    float4 clip=mul(final_vp,float4(hit,1));
    if(!all(isfinite(clip)) || clip.w<=1e-8) continue;
    float3 ndc=clip.xyz/clip.w;
    float2 uv=float2(ndc.x*.5+.5,.5-ndc.y*.5);
    if(any(uv<0) || any(uv>=1) || ndc.z<0 || ndc.z>1) continue;
    uint2 hit_pixel=min(uint2(uv*float2(width,height)),uint2(width-1,height-1));
    float retained_depth=scene_depth.Load(int3(hit_pixel,0));
    // A TLAS hit is not evidence of a visible lit-scene color. Reject hidden
    // hits, clear-depth regions, viewport misses and depth disagreements.
    if(!isfinite(retained_depth) || retained_depth==clear_depth || abs(retained_depth-ndc.z)>depth_rejection) continue;
    float4 lit=lit_scene.Load(int3(hit_pixel,0));if(!all(isfinite(lit)))continue;
    color+=lit.rgb;++accepted;
  }
  if(accepted) reflection_out[id.xy]=float4(color/accepted,float(accepted)/reflection_samples);
}
[numthreads(8,8,1)]
void DenoiseVisible(uint3 id:SV_DispatchThreadID) {
  uint2 p=id.xy;if(p.x>=width || p.y>=height)return;
  float depth=scene_depth.Load(int3(p,0));depth_out[p]=depth;
  if(!isfinite(depth) || depth<0 || depth>1 || depth==clear_depth) {shadow_out[p]=-1;reflection_out[p]=0;return;}
  uint2 tp=min(uint2((float2(p)+.5)*float2(trace_width,trace_height)/float2(width,height)),uint2(trace_width-1,trace_height-1));
  float center_shadow=raw_shadow.Load(int3(tp,0));float4 center_reflection=raw_reflection.Load(int3(tp,0));
  float shadow_sum=0,shadow_weight=0,reflection_weight=0;float4 reflection_sum=0;
  float shadow_min=1,shadow_max=0;float3 color_min=1e30,color_max=-1e30;
  [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
    int2 q=clamp(int2(tp)+int2(x,y),int2(0,0),int2(trace_width-1,trace_height-1));
    uint2 fp=min(uint2((float2(q)+.5)*float2(width,height)/float2(trace_width,trace_height)),uint2(width-1,height-1));
    float d=scene_depth.Load(int3(fp,0));
    if(!isfinite(d) || d==clear_depth || abs(d-depth)>depth_rejection)continue;
    float weight=exp(-float(x*x+y*y)*.5)*exp(-abs(d-depth)/max(depth_rejection*.25,1e-7));
    float s=raw_shadow.Load(int3(q,0));float4 r=raw_reflection.Load(int3(q,0));
    if(s>=0 && center_shadow>=0) {shadow_sum+=s*weight;shadow_weight+=weight;shadow_min=min(shadow_min,s);shadow_max=max(shadow_max,s);}
    // Do not fill misses with nearby reflected color: alpha zero preserves
    // the caller's original environment response.
    if(r.a>0 && center_reflection.a>0) {reflection_sum+=r*weight;reflection_weight+=weight;color_min=min(color_min,r.rgb);color_max=max(color_max,r.rgb);}
  }
  float shadow=shadow_weight>0?shadow_sum/shadow_weight:center_shadow;
  float4 reflected=reflection_weight>0?reflection_sum/reflection_weight:center_reflection;
  if((flags&16) && motion_validity.Load(int3(p,0))!=0 && (!(flags&32) || reactivity.Load(int3(p,0))==0)) {
    float2 velocity=motion_pixels.Load(int3(p,0)),prev=float2(p)+velocity+previous_jitter-current_jitter;
    float3 world;float current_depth;
    if(all(isfinite(velocity)) && all(prev>=0) && all(prev<float2(width,height)) && Position(p,world,current_depth)) {
      float4 previous_clip=mul(previous_vp,float4(world,1));
      if(all(isfinite(previous_clip)) && previous_clip.w>1e-8) {
        int2 pp=int2(prev+.5);pp=clamp(pp,int2(0,0),int2(width-1,height-1));
        float old_depth=previous_depth.Load(int3(pp,0));
        if(isfinite(old_depth) && old_depth!=clear_depth && abs(old_depth-previous_clip.z/previous_clip.w)<=depth_rejection) {
          float old_shadow=previous_shadow.Load(int3(pp,0));float4 old_reflection=previous_reflection.Load(int3(pp,0));
          if(shadow>=0 && old_shadow>=0)shadow=lerp(shadow,clamp(old_shadow,shadow_min,shadow_max),history_weight);
          if(reflected.a>0 && old_reflection.a>0)reflected.rgb=lerp(reflected.rgb,clamp(old_reflection.rgb,color_min,color_max),history_weight);
        }
      }
    }
  }
  shadow_out[p]=shadow;reflection_out[p]=reflected;
  uint ignored;
  if((flags&1) && shadow>=0)execution_counts.InterlockedAdd(0,1,ignored);
  if((flags&2) && reflected.a>0)execution_counts.InterlockedAdd(4,1,ignored);
}
[numthreads(8,8,1)]
void CompositeVisible(uint3 id:SV_DispatchThreadID) {
  uint2 p=id.xy;if(p.x>=width || p.y>=height)return;
  float4 original=lit_scene.Load(int3(p,0));float3 color=original.rgb;
  float visibility=raw_shadow.Load(int3(p,0));
  if((flags&1) && visibility>=0)color*=.35+.65*saturate(visibility);
  float4 reflected=raw_reflection.Load(int3(p,0));
  if((flags&2) && reflected.a>0) {
    float3 world,wx,wy;float depth,dx,dy;
    uint2 px=uint2(p.x+1<width?p.x+1:p.x-1,p.y),py=uint2(p.x,p.y+1<height?p.y+1:p.y-1);
    if(Position(p,world,depth) && Position(px,wx,dx) && Position(py,wy,dy)) {
      float3 normal=cross(wx-world,wy-world),view=camera-world;
      if(dot(normal,normal)>1e-16 && dot(view,view)>1e-16) {
        float cosine=abs(dot(normalize(normal),normalize(view)));
        float fresnel=.04+.96*pow(1-saturate(cosine),5);
        color=lerp(color,reflected.rgb,saturate(fresnel*reflected.a));
      }
    }
  }
  reflection_out[p]=float4(color,original.a);
}
#endif
