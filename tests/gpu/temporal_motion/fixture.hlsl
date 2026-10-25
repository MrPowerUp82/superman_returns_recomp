ByteAddressBuffer clip : register(t0);
float4 DepthVS(uint id : SV_VertexID) : SV_Position {
    return asfloat(clip.Load4(id * 16));
}
uint CoveragePS() : SV_Target0 { return 1; }
