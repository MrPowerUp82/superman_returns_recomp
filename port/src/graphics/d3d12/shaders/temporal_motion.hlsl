// Exact post-VS clips supplied by caller; no transform or identity inference.
ByteAddressBuffer currentClips : register(t0);
ByteAddressBuffer previousClips : register(t1);
cbuffer MotionConstants : register(b0) {
    float2 dimensions;
    float2 currentJitter;
    float2 previousJitter;
    uint correspondenceValid;
};
struct RasterVertex {
    float4 position : SV_Position;
    // Default perspective interpolation is essential with unequal current W.
    float4 previousClip : TEXCOORD0;
};
RasterVertex MotionVS(uint id : SV_VertexID) {
    RasterVertex v;
    v.position = asfloat(currentClips.Load4(id * 16));
    v.previousClip = correspondenceValid ? asfloat(previousClips.Load4(id * 16)) : 0;
    return v;
}
struct MotionPixel {
    float2 velocity : SV_Target0;
    uint validity : SV_Target1;
};
MotionPixel MotionPS(RasterVertex v) {
    MotionPixel result;
    result.velocity = 0;
    result.validity = 0;
    if (!correspondenceValid || !all(isfinite(v.previousClip)) || v.previousClip.w <= 0)
        return result;
    const float2 currentUV = v.position.xy / dimensions - currentJitter / dimensions;
    const float2 previousUV = (v.previousClip.xy / v.previousClip.w) * float2(.5, -.5)
                             + .5 - previousJitter / dimensions;
    const float2 velocity = (previousUV - currentUV) * dimensions;
    if (!all(isfinite(velocity)) || any(abs(velocity) > 65504.0)) return result;
    result.velocity = velocity;
    result.validity = 1;
    return result;
}
