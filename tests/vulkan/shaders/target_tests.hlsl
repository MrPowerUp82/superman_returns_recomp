// Project-owned fullscreen triangle for depth, stencil and MRT readbacks.
float4 VSMain(uint vertex : SV_VertexID) : SV_Position {
    float2 p = vertex == 0 ? float2(-1,-1) : vertex == 1 ? float2(3,-1) : float2(-1,3);
    return float4(p,0.5,1);
}
struct Outputs {float4 a : SV_Target0;float4 b : SV_Target1;};
Outputs PSMain() {Outputs o;o.a=float4(0,1,0,1);o.b=float4(1,1,0,1);return o;}
