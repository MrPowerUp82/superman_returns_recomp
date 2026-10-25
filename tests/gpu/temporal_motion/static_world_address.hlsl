// Address slice audited from artifacts/shaders/hlsl/2C45BFFE76D77398.vs.hlsl:
// r0=float(id&0xFFFFFF); r0.z=r0.x+.5; r1.x=trunc(r0.z*reciprocal);
// r0.x=r1.x+r1.x; a0=int(clamp(floor(r0.x+.5),-256,255));
// world XYZ consumes CONST_REL(100+a0) and CONST_REL(101+a0).
// Contract qualification: the shipped VS2C45 DXIL uses precise fdiv 1,stride.
// Audited VS artifact SHA256:
// 3A781EBBDA5B0A8E20B2E6AC9C37DAF190DF7EE076F34B4D65845DB315E43652
// HLSL rcp's intrinsic accuracy is weaker; do NOT generalize the fdiv envelope
// to arbitrary rcp implementations. This fixture pins -Gis and checks its DXIL.
// Re-audit the compiled VS if its compiler/artifact changes; the helper's current
// "1-ULP reciprocal" comment requires that compiled-fdiv qualification.
cbuffer RowTags : register(b0) { float4 rows[256]; }
cbuffer DispatchSize : register(b1) { uint sampleCount; }
StructuredBuffer<uint2> samples : register(t0); // vertex ID, stride float bits
RWStructuredBuffer<uint4> observed : register(u0);

float4 ReadConstant(int index) {
    // Match CONST_REL's clamp + zero selection. A selected zero has no effective
    // runtime-constant dependency, even if the compiler speculates c255's load.
    return (uint)index < 256u ? rows[min((uint)index, 255u)] : 0.0f;
}

[numthreads(64, 1, 1)]
void AddressCS(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= sampleCount) return;
    uint2 input = samples[tid.x];
    precise float vidfloat = float(input.x & 0xFFFFFFu);
    precise float numerator = vidfloat + 0.5f;
    // Artifact's FLT_MIN is negative max finite (0xff7fffff), NOT FLT_MIN
    // from <cfloat>. CPU validation restricts this test to stride >= 1.
    precise float reciprocal = 1.0f / asfloat(input.y);
    reciprocal = clamp(reciprocal, asfloat(0xff7fffffu), asfloat(0x7f7fffffu));
    precise float quotient = numerator * reciprocal;
    precise float instance = trunc(quotient);
    precise float doubled = instance + instance;
    int a0 = (int)clamp(floor(doubled + 0.5f), -256.0f, 255.0f);
    // Read row identity FROM the constant buffer, rather than merely exporting
    // computed indices. Tags are row+1; selected zero encodes UINT_MAX (absent).
    float4 first = ReadConstant(100 + a0);
    float4 second = ReadConstant(101 + a0);
    observed[tid.x] = uint4((uint)first.w - 1u, (uint)second.w - 1u,
                           (uint)a0, asuint(reciprocal));
}
