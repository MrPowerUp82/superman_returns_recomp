// Project-owned fixture: production per-draw PS float4 c[256] bank.
// Each register occupies 16 bytes in set 0, binding 1.
[[vk::binding(1,0)]] ByteAddressBuffer PSConstants;

float4 ReadConstant(uint reg) {
    return asfloat(PSConstants.Load4(reg * 16));
}

float4 PS0() : SV_Target0 { return ReadConstant(0); }
float4 PS1() : SV_Target0 { return ReadConstant(1); }
float4 PSDynamic() : SV_Target0 {
    uint index = (uint)ReadConstant(2).x;
    return ReadConstant(index);
}
