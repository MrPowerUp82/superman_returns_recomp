// SrSceneResolution v1: exact host-to-guest pixels and per-binding guest grids.
cbuffer SrSceneResolution : register(b6, space4)
{
    float2 g_SrScenePixelScale;
    uint2 g_SrSceneReserved;
    uint4 g_SrSceneTextureGrids[16];
};

#define CONAN_RECOMP 1
#ifndef SHADER_COMMON_H_INCLUDED
#define SHADER_COMMON_H_INCLUDED

#define SPEC_CONSTANT_R11G11B10_NORMAL  (1 << 0)
#define SPEC_CONSTANT_ALPHA_TEST        (1 << 1)

#ifdef CONAN_RECOMP
    // Alpha test converted to alpha-to-coverage (foliage antialiasing option).
    #define SPEC_CONSTANT_ALPHA_TO_COVERAGE (1 << 3)
    #define SPEC_CONSTANT_SOFT_PARTICLE_RGBA (1 << 4)
    #define SPEC_CONSTANT_SOFT_PARTICLE_ALPHA (1 << 5)
#endif
#ifdef UNLEASHED_RECOMP
    #define SPEC_CONSTANT_BICUBIC_GI_FILTER (1 << 2)
    #define SPEC_CONSTANT_ALPHA_TO_COVERAGE (1 << 3)
    #define SPEC_CONSTANT_REVERSE_Z         (1 << 4)
#endif

// SPIR-V vertex input locations, shared with reblue's host input-layout
// builder. A semantic missing here gets no location from the emitter.
#define REBLUE_VERTEX_INPUT_LOCATIONS(X) \
    X(Position,  0,  0) \
    X(Position,  1,  1) \
    X(Position,  2,  2) \
    X(Position,  3,  3) \
    X(Position,  4,  4) \
    X(Normal,    0,  5) \
    X(Tangent,   0,  6) \
    X(TexCoord,  0,  7) \
    X(TexCoord,  1,  8) \
    X(TexCoord,  2,  9) \
    X(Color,     0, 10)

// SPEC_CONSTANTS_ONLY keeps the HLSL below out of host C++ TUs, which IntelliSense would otherwise parse.
#if (!defined(__cplusplus) || defined(__INTELLISENSE__)) && !defined(SHADER_COMMON_SPEC_CONSTANTS_ONLY)

#define FLT_MIN asfloat(0xff7fffff)
#define FLT_MAX asfloat(0x7f7fffff)

#ifdef __spirv__

#ifdef SR_VULKAN_BUFFERS
[[vk::binding(0,0)]] ByteAddressBuffer g_VertexShaderConstants;
[[vk::binding(1,0)]] ByteAddressBuffer g_PixelShaderConstants;
[[vk::binding(2,0)]] ByteAddressBuffer g_SharedConstants;
#define SR_LOAD_FLOAT4(STAGE,OFFSET) asfloat(g_ ## STAGE ## ShaderConstants.Load4(OFFSET))
#define SR_SHARED_UINT(OFFSET) g_SharedConstants.Load(OFFSET)
#define SR_SHARED_UINT4(OFFSET) g_SharedConstants.Load4(OFFSET)
#define SR_SHARED_FLOAT(OFFSET) asfloat(g_SharedConstants.Load(OFFSET))
#define SR_SHARED_FLOAT2(OFFSET) asfloat(g_SharedConstants.Load2(OFFSET))
#define SR_SHARED_FLOAT4(OFFSET) asfloat(g_SharedConstants.Load4(OFFSET))
#else

struct PushConstants
{
    uint64_t VertexShaderConstants;
    uint64_t PixelShaderConstants;
    uint64_t SharedConstants;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> g_PushConstants;

#define SR_LOAD_FLOAT4(STAGE,OFFSET) vk::RawBufferLoad<float4>(g_PushConstants.STAGE ## ShaderConstants + (OFFSET), 0x10)
#define SR_SHARED_UINT(OFFSET) vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + (OFFSET))
#define SR_SHARED_UINT4(OFFSET) vk::RawBufferLoad<uint4>(g_PushConstants.SharedConstants + (OFFSET))
#define SR_SHARED_FLOAT(OFFSET) vk::RawBufferLoad<float>(g_PushConstants.SharedConstants + (OFFSET))
#define SR_SHARED_FLOAT2(OFFSET) vk::RawBufferLoad<float2>(g_PushConstants.SharedConstants + (OFFSET))
#define SR_SHARED_FLOAT4(OFFSET) vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + (OFFSET))
#endif

#ifdef REBLUE_RECOMP
// 256-bit boolean register file (BD bool addresses reach ~158), then per-usage 16-bit-pair swap masks.
#define g_Booleans(i)              SR_SHARED_UINT(256 + (i)*4)
#define g_SwappedTexcoords         SR_SHARED_UINT(288)
#define g_HalfPixelOffset          SR_SHARED_FLOAT2(292)
#define g_AlphaThreshold           SR_SHARED_FLOAT(300)
#define g_SwappedNormals           SR_SHARED_UINT(304)
#define g_SwappedBinormals         SR_SHARED_UINT(308)
#define g_SwappedTangents          SR_SHARED_UINT(312)
#define g_SwappedBlendWeights      SR_SHARED_UINT(316)
#define g_SwappedPositions         SR_SHARED_UINT(320)
#define g_SintTexcoords            SR_SHARED_UINT(324)
#define g_ShadowPcfScale           SR_SHARED_FLOAT(328)
#elif defined(CONAN_RECOMP)
// Conan layout (tools/xenosrecomp/patches): c16-c17 = the 256-bit Xenos bool file
// (VS 0..127, PS 128..255, LSB-first per dword like SHADER_CONSTANT_BOOL_*),
// c18 = misc, c19-c26 = the 32 Xenos loop constants (SHADER_CONSTANT_LOOP_00..31:
// count bits 0-7, start bits 8-15, signed step bits 16-23; VS i0-15 = 0-15,
// PS i0-15 = 16-31).
#define g_Booleans(i)              SR_SHARED_UINT(256 + (i)*4)
#define g_SwappedTexcoords         SR_SHARED_UINT(288)
#define g_HalfPixelOffset          SR_SHARED_FLOAT2(292)
#define g_AlphaThreshold           SR_SHARED_FLOAT(300)
#define g_VertexFetch(i)           SR_SHARED_UINT4(512 + (i)*16)
#define g_LoopConstant(i)          SR_SHARED_UINT(304 + (i)*4)
#define g_ScreenXform              SR_SHARED_FLOAT4(432)
#define g_SpecConstantsRuntime     SR_SHARED_UINT(448)
#else
#define g_Booleans                 SR_SHARED_UINT(256)
#define g_SwappedTexcoords         SR_SHARED_UINT(260)
#define g_HalfPixelOffset          SR_SHARED_FLOAT2(264)
#define g_AlphaThreshold           SR_SHARED_FLOAT(272)
#endif

#ifdef SR_VULKAN_BUFFERS
#define g_PixelPosScale SR_SHARED_FLOAT(452)
#define g_ShadowAtlasTexelScale SR_SHARED_FLOAT(456)
#define g_ShadowSoftness SR_SHARED_FLOAT(460)
#define g_SoftParticleW SR_SHARED_FLOAT2(464)
#define g_SoftParticleDistance SR_SHARED_FLOAT(472)
#define g_SoftParticleTexelScale SR_SHARED_FLOAT(476)
#define g_SoftParticleDepth SR_SHARED_UINT(480)
#define g_SpecConstants() g_SpecConstantsRuntime
#else
[[vk::constant_id(0)]] const uint g_SpecConstants = 0;
#define g_SpecConstants() g_SpecConstants
#endif

#else

#ifdef REBLUE_RECOMP
#define DEFINE_SHARED_CONSTANTS() \
    uint4 g_BooleansArr[2] : packoffset(c16); \
    uint g_SwappedTexcoords : packoffset(c18.x); \
    float2 g_HalfPixelOffset : packoffset(c18.y); \
    float g_AlphaThreshold : packoffset(c18.w); \
    uint g_SwappedNormals : packoffset(c19.x); \
    uint g_SwappedBinormals : packoffset(c19.y); \
    uint g_SwappedTangents : packoffset(c19.z); \
    uint g_SwappedBlendWeights : packoffset(c19.w); \
    uint g_SwappedPositions : packoffset(c20.x); \
    uint g_SintTexcoords : packoffset(c20.y); \
    float g_ShadowPcfScale : packoffset(c20.z);

#define g_Booleans(i) (g_BooleansArr[(i) / 4][(i) % 4])
#elif defined(CONAN_RECOMP)
#define DEFINE_SHARED_CONSTANTS() \
    uint4 g_BooleansArr[2] : packoffset(c16); \
    uint g_SwappedTexcoords : packoffset(c18.x); \
    float2 g_HalfPixelOffset : packoffset(c18.y); \
    float g_AlphaThreshold : packoffset(c18.w); \
    uint4 g_VertexFetchArr[224] : packoffset(c32); \
    uint4 g_LoopConstantsArr[8] : packoffset(c19); \
    float4 g_ScreenXform : packoffset(c27);     uint g_SpecConstantsRuntime : packoffset(c28.x); float g_PixelPosScale : packoffset(c28.y); float g_ShadowAtlasTexelScale : packoffset(c28.z); float g_ShadowSoftness : packoffset(c28.w); float2 g_SoftParticleW : packoffset(c29.x); float g_SoftParticleDistance : packoffset(c29.z); float g_SoftParticleTexelScale : packoffset(c29.w); uint g_SoftParticleDepth : packoffset(c30.x);

#define g_Booleans(i) (g_BooleansArr[(i) / 4][(i) % 4])
#define g_VertexFetch(i) (g_VertexFetchArr[i])
#define g_LoopConstant(i) (g_LoopConstantsArr[(i) / 4][(i) % 4])
#else
#define DEFINE_SHARED_CONSTANTS() \
    uint g_Booleans : packoffset(c16.x); \
    uint g_SwappedTexcoords : packoffset(c16.y); \
    float2 g_HalfPixelOffset : packoffset(c16.z); \
    float g_AlphaThreshold : packoffset(c17.x);
#endif

uint g_SpecConstants();

#endif

#if defined(REBLUE_RECOMP) || defined(CONAN_RECOMP)
// Test Xenos boolean register N in the unified VS(0..127)/PS(128..255) file.
#define BOOL_BIT(n) ((g_Booleans((n) / 32u) & (1u << ((n) & 31u))) != 0)
#endif

#ifdef CONAN_RECOMP
// xenos::LoopConstant: aL = start + iteration * step.
// Macros take the 32-bit loop constant value: g_LoopConstant(id), or a literal
// from the shader's definition table (defi).
#define LOOP_COUNT(c) ((c) & 0xFFu)
#define LOOP_START(c) int(((c) >> 8) & 0xFFu)
#define LOOP_STEP(c)  (int((c) << 8) >> 24)
#endif

#ifdef CONAN_RECOMP
// Bit 31 of a texture descriptor index = the fetch constant's RGB sign is GAMMA:
// Xenos returns linear values, converted with its piecewise-linear curve
// (xenos::PWLGammaToLinear, applied after filtering like the Xenia/ReXGlue
// Xenos backend).
// Bits 16-27 = host scale of a texture rendered larger than its guest size
// (render_scale resolve targets, 8.8 fixed point, 0 = 1:1): texel offsets and
// filter weights stay in guest texels, like on Xenos. SRV indices use bits 0-14.
#define TEX_INDEX(i) ((i) & 0x7FFFu)
#define TEX_SCALE(i) ((((i) >> 16) & 0xFFFu) ? float(((i) >> 16) & 0xFFFu) / 256.0 : 1.0)
float3 conanPwlGammaToLinear(float3 g)
{
    g = saturate(g);
    float3 hi = select(g >= 192.0f / 255.0f, 8.0f / 1024.0f, 4.0f / 1024.0f);
    float3 lo = select(g >= 64.0f / 255.0f, 2.0f / 1024.0f, 1.0f / 1024.0f);
    float3 scale = select(g >= 96.0f / 255.0f, hi, lo);
    float3 offset = select(g >= 96.0f / 255.0f, select(g >= 192.0f / 255.0f, -1024.0f, -256.0f),
                           select(g >= 64.0f / 255.0f, -64.0f, 0.0f));
    float3 l = g * ((255.0 * 1024.0) * scale) + offset;
    l += trunc(l * scale);
    return l * (1.0 / 1023.0);
}
float4 conanGamma(uint resourceDescriptorIndex, float4 value)
{
    if (resourceDescriptorIndex & 0x80000000u)
        value.rgb = conanPwlGammaToLinear(value.rgb);
    return value;
}
#else
#define TEX_INDEX(i) (i)
#define TEX_SCALE(i) 1.0
#define conanGamma(i, v) (v)
#endif

#ifdef SR_VULKAN_BUFFERS
[[vk::binding(0,1)]] Texture2D<float4> g_Texture2DDescriptorHeap[32];
#else
Texture2D<float4> g_Texture2DDescriptorHeap[] : register(t0, space0);
#endif
#ifdef SR_VULKAN_BUFFERS
[[vk::binding(1,1)]] Texture3D<float4> g_Texture3DDescriptorHeap[32];
#else
Texture3D<float4> g_Texture3DDescriptorHeap[] : register(t0, space1);
#endif
#ifdef SR_VULKAN_BUFFERS
[[vk::binding(2,1)]] TextureCube<float4> g_TextureCubeDescriptorHeap[32];
#else
TextureCube<float4> g_TextureCubeDescriptorHeap[] : register(t0, space2);
#endif
#ifdef SR_VULKAN_BUFFERS
[[vk::binding(0,2)]] SamplerState g_SamplerDescriptorHeap[32];
#else
SamplerState g_SamplerDescriptorHeap[] : register(s0, space3);
#endif

uint2 getTexture2DDimensions(Texture2D<float4> texture)
{
    uint2 dimensions;
    texture.GetDimensions(dimensions.x, dimensions.y);
    return dimensions;
}

// Texel grid the guest shader addresses (the guest texture size).
float2 getGuestTexture2DDimensions(uint resourceDescriptorIndex, Texture2D<float4> texture)
{
    if (resourceDescriptorIndex & 0x40000000u)
        return float2(g_SrSceneTextureGrids[(resourceDescriptorIndex >> 16) & 15u].xy);
    return float2(getTexture2DDimensions(texture)) / TEX_SCALE(resourceDescriptorIndex);
}

float4 tfetch2DBicubic(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset);

float4 tfetch2D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
#ifdef CONAN_RECOMP
    // Bit 28: smooth (cubic B-spline) magnification, set by the host for small
    // textures of blended effects (particles, glows) - bilinear magnification
    // of those shows diamond/block patterns at high render resolutions.
    if (resourceDescriptorIndex & 0x10000000u)
        return conanGamma(resourceDescriptorIndex, tfetch2DBicubic(resourceDescriptorIndex, samplerDescriptorIndex, texCoord, offset));
#endif
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[TEX_INDEX(resourceDescriptorIndex)];
    return conanGamma(resourceDescriptorIndex, texture.Sample(g_SamplerDescriptorHeap[samplerDescriptorIndex], texCoord + offset / getGuestTexture2DDimensions(resourceDescriptorIndex, texture)));
}

float2 getWeights2D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[TEX_INDEX(resourceDescriptorIndex)];
    return select(isnan(texCoord), 0.0f, frac(texCoord * getGuestTexture2DDimensions(resourceDescriptorIndex, texture) + offset - 0.5));
}

#ifdef REBLUE_RECOMP
// Bilinear-filtered shadow compare over the four neighboring depth texels.
float shadowCmp2D(uint resourceDescriptorIndex, float2 texCoord, float ref)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[TEX_INDEX(resourceDescriptorIndex)];
    int2 dimensions = int2(getTexture2DDimensions(texture));
    float2 coord = texCoord * dimensions - 0.5;
    float2 weights = frac(coord);
    int2 base = int2(floor(coord));
    int2 c0 = clamp(base, int2(0, 0), dimensions - 1);
    int2 c1 = clamp(base + 1, int2(0, 0), dimensions - 1);
    float4 taps = float4(
        texture.Load(int3(c0, 0)).x,
        texture.Load(int3(c1.x, c0.y, 0)).x,
        texture.Load(int3(c0.x, c1.y, 0)).x,
        texture.Load(int3(c1, 0)).x) > ref;
    return lerp(lerp(taps.x, taps.y, weights.x), lerp(taps.z, taps.w, weights.x), weights.y);
}
#endif

float w0(float a)
{
    return (1.0f / 6.0f) * (a * (a * (-a + 3.0f) - 3.0f) + 1.0f);
}

float w1(float a)
{
    return (1.0f / 6.0f) * (a * a * (3.0f * a - 6.0f) + 4.0f);
}

float w2(float a)
{
    return (1.0f / 6.0f) * (a * (a * (-3.0f * a + 3.0f) + 3.0f) + 1.0f);
}

float w3(float a)
{
    return (1.0f / 6.0f) * (a * a * a);
}

float g0(float a)
{
    return w0(a) + w1(a);
}

float g1(float a)
{
    return w2(a) + w3(a);
}

float h0(float a)
{
    return -1.0f + w1(a) / (w0(a) + w1(a)) + 0.5f;
}

float h1(float a)
{
    return 1.0f + w3(a) / (w2(a) + w3(a)) + 0.5f;
}

float4 tfetch2DBicubic(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[TEX_INDEX(resourceDescriptorIndex)];
    SamplerState samplerState = g_SamplerDescriptorHeap[samplerDescriptorIndex];
    float2 dimensions = getGuestTexture2DDimensions(resourceDescriptorIndex, texture);
    
    float x = texCoord.x * dimensions.x + offset.x;
    float y = texCoord.y * dimensions.y + offset.y;

    x -= 0.5f;
    y -= 0.5f;
    float px = floor(x);
    float py = floor(y);
    float fx = x - px;
    float fy = y - py;

    float g0x = g0(fx);
    float g1x = g1(fx);
    float h0x = h0(fx);
    float h1x = h1(fx);
    float h0y = h0(fy);
    float h1y = h1(fy);

    float4 r =
        g0(fy) * (g0x * texture.Sample(samplerState, float2(px + h0x, py + h0y) / float2(dimensions)) +
            g1x * texture.Sample(samplerState, float2(px + h1x, py + h0y) / float2(dimensions))) +
        g1(fy) * (g0x * texture.Sample(samplerState, float2(px + h0x, py + h1y) / float2(dimensions)) +
            g1x * texture.Sample(samplerState, float2(px + h1x, py + h1y) / float2(dimensions)));

    return r;
}

float4 tfetch3D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float3 texCoord)
{
    return conanGamma(resourceDescriptorIndex, g_Texture3DDescriptorHeap[TEX_INDEX(resourceDescriptorIndex)].Sample(g_SamplerDescriptorHeap[samplerDescriptorIndex], texCoord));
}

struct CubeMapData
{
	#ifdef REBLUE_RECOMP
    // BD's cube-shadow PCF issues up to 9 cube ops per shader (bd_mirror_cs_ps /
    // bd_glass_cs_ps); overflowing this array drops the stored directions and
    // every tfetchCube reads OOB -> point-light shadows vanish.
    float3 cubeMapDirections[9];
    #else
    float3 cubeMapDirections[16];
    #endif
    uint cubeMapIndex;
};

float4 tfetchCube(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float3 texCoord, inout CubeMapData cubeMapData)
{
    return conanGamma(resourceDescriptorIndex, g_TextureCubeDescriptorHeap[TEX_INDEX(resourceDescriptorIndex)].Sample(g_SamplerDescriptorHeap[samplerDescriptorIndex], cubeMapData.cubeMapDirections[texCoord.z]));
}

#ifdef CONAN_RECOMP
// D3D12 has no unsigned scaled 16-bit IA format. Bind USHORT2 as UNORM
// and restore its original integer values before the guest VS uses them.
#define srVertexInput(semantic, value) srVertexInputImpl(g_VertexFetch(semantic).w, value)
float4 srVertexInputImpl(uint type, float4 value)
{
    if (type == 0x2C2259)
        return float4(round(value.xy * 65535.0f), 0.0f, 1.0f);
    return value;
}
// Dynamically indexed Xenos fetches (instancing): host-order, interleaved
// vertex streams. Descriptor metadata comes from the actual D3D declaration.
#ifdef SR_VULKAN_BUFFERS
[[vk::binding(0,3)]] ByteAddressBuffer g_VertexBufferHeap[32];
#else
ByteAddressBuffer g_VertexBufferHeap[] : register(t0, space5);
#endif
#define srVertexFetch(semantic, index, packed) srVertexFetchImpl(g_VertexFetch(semantic), index, packed)
float4 srVertexFetchImpl(uint4 meta, float index, bool packed)
{
    // meta: SRV, byte offset, stride, XDK type
    if (meta.z == 0 || index < 0.0f) return 0.0f;
    uint address = meta.y + uint(index) * meta.z;
    uint bytes;
    g_VertexBufferHeap[meta.x].GetDimensions(bytes);
    uint type = meta.w;
    uint count = (type == 0x1A23A6 || type == 0x1A235A || type == 0x1A215A ||
                  type == 0x1A205A || type == 0x1A2360) ? 4u :
                 (type == 0x2A23B9 ? 3u :
                 (type == 0x2C23A5 || type == 0x2C2359 || type == 0x2C2159 ||
                  type == 0x2C2059 || type == 0x2C2259 || type == 0x2C235F) ? 2u : 1u);
    bool half = type == 0x1A235A || type == 0x1A215A || type == 0x1A205A ||
                type == 0x1A2360 || type == 0x2C2359 || type == 0x2C2159 ||
                type == 0x2C2059 || type == 0x2C2259 || type == 0x2C235F;
    uint required = count * (half ? 2u : 4u);
    if (address > bytes || required > bytes - address) return 0.0f;
    float4 value = float4(0.0f, 0.0f, 0.0f, 1.0f);
    for (uint c = 0; c < count; ++c)
    {
        uint at = address + c * (half ? 2u : 4u);
        uint word = g_VertexBufferHeap[meta.x].Load(at & ~3u);
        if (half)
        {
            uint h = (word >> ((at & 2u) * 8u)) & 65535u;
            int signedH = int(h << 16u) >> 16;
            if (type == 0x2C235F || type == 0x1A2360) value[c] = f16tof32(h);
            else if (type == 0x2C2059 || type == 0x1A205A) value[c] = float(h) / 65535.0f;
            else if (type == 0x2C2259) value[c] = float(h);
            else if (type == 0x2C2359) value[c] = float(signedH);
            else value[c] = max(float(signedH) / 32767.0f, -1.0f);
        }
        else if (type == 0x2C82A1 || type == 0x2A2187 || type == 0x2A2190 || type == 0x2A2390)
            value[c] = packed ? asfloat(word) : float(word);
        else if (type == 0x182886 || type == 0x1A2286 || type == 0x1A2386 || type == 0x1A2086 || type == 0x1A2186)
        {
            value = float4(word & 255u, (word >> 8u) & 255u, (word >> 16u) & 255u, word >> 24u);
            if (type == 0x182886) value = value.zyxw;
            if (type != 0x1A2286 && type != 0x1A2386) value /= 255.0f;
        }
        else value[c] = asfloat(word);
    }
    return value;
}
#endif

#ifdef REBLUE_RECOMP
// DEC3N normal decode; IA binds as R32_UINT so lane .x carries the raw bits (asuint recovers them).

float4 tfetchR11G11B10(float4 value)
{
#ifdef CONAN_RECOMP
    if (g_SpecConstants() & (1u << 6))
    {
        uint v = asuint(value.x);
        int3 n = int3(int(v << 22) >> 22,
                      int(v << 12) >> 22,
                      int(v << 2) >> 22);
        return float4(max(float3(n) / 511.0f, -1.0f), 1.0f);
    }
#endif
    if (g_SpecConstants() & SPEC_CONSTANT_R11G11B10_NORMAL)
    {
        uint v = asuint(value.x);
        return float4(
            (v & 0x00000400 ? -1.0 : 0.0) + ((v & 0x3FF) / 1024.0),
            (v & 0x00200000 ? -1.0 : 0.0) + (((v >> 11) & 0x3FF) / 1024.0),
            (v & 0x80000000 ? -1.0 : 0.0) + (((v >> 22) & 0x1FF) / 512.0),
            0.0);
    }
    return value;
}

// Undo the engine bswap32 16-bit-pair swap (.yxwz) for any 16-bit-packed semantic flagged in the mask.
float4 swapFloats(uint swappedMask, float4 value, uint semanticIndex)
{
    return (swappedMask & (1u << semanticIndex)) != 0 ? value.yxwz : value;
}

// Recover X360 integer-cast-to-float TEXCOORDs from R16G16(B16A16)_UINT bindings (sign-extend low 16 bits).
float4 sintTexcoord(uint mask, float4 value, uint semanticIndex)
{
    if ((mask & (1u << semanticIndex)) != 0)
    {
        int4 si = (int4(asuint(value)) << 16) >> 16;
        return float4(si);
    }
    return value;
}
#else
float4 tfetchR11G11B10(uint4 value)
{
#ifdef CONAN_RECOMP
    if (g_SpecConstants() & (1u << 6))
    {
        uint v = value.x;
        int3 n = int3(int(v << 22) >> 22,
                      int(v << 12) >> 22,
                      int(v << 2) >> 22);
        return float4(max(float3(n) / 511.0f, -1.0f), 1.0f);
    }
#endif
    if (g_SpecConstants() & SPEC_CONSTANT_R11G11B10_NORMAL)
    {
        return float4(
            (value.x & 0x00000400 ? -1.0 : 0.0) + ((value.x & 0x3FF) / 1024.0),
            (value.x & 0x00200000 ? -1.0 : 0.0) + (((value.x >> 11) & 0x3FF) / 1024.0),
            (value.x & 0x80000000 ? -1.0 : 0.0) + (((value.x >> 22) & 0x1FF) / 512.0),
            0.0);
    }
    else
    {
        return asfloat(value);
    }
}

float4 tfetchTexcoord(uint swappedTexcoords, float4 value, uint semanticIndex)
{
#ifdef SR_VULKAN_BUFFERS
    return semanticIndex < 32 && (swappedTexcoords & (1u << (semanticIndex & 31u))) != 0 ? value.yxwz : value;
#else
    return (swappedTexcoords & (1ull << semanticIndex)) != 0 ? value.yxwz : value;
#endif
}
#endif

float4 cube(float4 value, inout CubeMapData cubeMapData)
{
    uint index = cubeMapData.cubeMapIndex;
    cubeMapData.cubeMapDirections[index] = value.xyz;
    ++cubeMapData.cubeMapIndex;
    
    return float4(0.0, 0.0, 0.0, index);
}

float4 dst(float4 src0, float4 src1)
{
    float4 dest;
    dest.x = 1.0;
    dest.y = src0.y * src1.y;
    dest.z = src0.z;
    dest.w = src1.w;
    return dest;
}

float4 max4(float4 src0)
{
    return max(max(src0.x, src0.y), max(src0.z, src0.w));
}

float2 getPixelCoord(uint resourceDescriptorIndex, float2 texCoord)
{
    return getTexture2DDimensions(g_Texture2DDescriptorHeap[TEX_INDEX(resourceDescriptorIndex)]) * texCoord;
}

float computeMipLevel(float2 pixelCoord)
{
    float2 dx = ddx(pixelCoord);
    float2 dy = ddy(pixelCoord);
    float deltaMaxSqr = max(dot(dx, dx), dot(dy, dy));
    return max(0.0, 0.5 * log2(deltaMaxSqr));
}

#endif

#endif

#ifdef __spirv__

#define Kramp_lo_hi_Kao SR_LOAD_FLOAT4(Vertex, 3664)
#define gKSEsD SR_LOAD_FLOAT4(Vertex, 3696)
#define gLightPos SR_LOAD_FLOAT4(Vertex, 3680)
#define gViewInverse(INDEX) select((INDEX) < 251, SR_LOAD_FLOAT4(Vertex, (5 + min(INDEX, 250)) * 16), 0.0)
#define gViewProjectionXform(INDEX) select((INDEX) < 256, SR_LOAD_FLOAT4(Vertex, (0 + min(INDEX, 255)) * 16), 0.0)
#define instance_data SR_LOAD_FLOAT4(Vertex, 64)
#define instance_transforms(INDEX) select((INDEX) < 156, SR_LOAD_FLOAT4(Vertex, (100 + min(INDEX, 155)) * 16), 0.0)
#define shadowMapU SR_LOAD_FLOAT4(Vertex, 144)
#define shadowMapV SR_LOAD_FLOAT4(Vertex, 160)
#define shadowMapZ SR_LOAD_FLOAT4(Vertex, 176)
#define shadow_dens_vis SR_LOAD_FLOAT4(Vertex, 128)
#define CONST_REL(INDEX) select((uint)(INDEX) < 256, SR_LOAD_FLOAT4(Vertex, min((uint)(INDEX), 255) * 16), 0.0)

#else

cbuffer VertexShaderConstants : register(b0, space4)
{
	float4 g_VertexShaderConstantsArr[256] : packoffset(c0);
};

#define CONST_REL(INDEX) select((uint)(INDEX) < 256, g_VertexShaderConstantsArr[min((uint)(INDEX), 255)], 0.0)
#define Kramp_lo_hi_Kao g_VertexShaderConstantsArr[229]
#define gKSEsD g_VertexShaderConstantsArr[231]
#define gLightPos g_VertexShaderConstantsArr[230]
#define gViewInverse(INDEX) CONST_REL(5 + (INDEX))
#define gViewProjectionXform(INDEX) CONST_REL(0 + (INDEX))
#define instance_data g_VertexShaderConstantsArr[4]
#define instance_transforms(INDEX) CONST_REL(100 + (INDEX))
#define shadowMapU g_VertexShaderConstantsArr[9]
#define shadowMapV g_VertexShaderConstantsArr[10]
#define shadowMapZ g_VertexShaderConstantsArr[11]
#define shadow_dens_vis g_VertexShaderConstantsArr[8]

cbuffer SharedConstants : register(b2, space4)
{
	DEFINE_SHARED_CONSTANTS();
};

#endif

#ifndef __spirv__
[shader("vertex")]
#endif
void main(
	in uint iVertexId : SV_VertexID,
	out precise float4 oPos : SV_Position,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(0)]]
#endif
	out float4 oTexCoord0 : TEXCOORD0,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(1)]]
#endif
	out float4 oTexCoord1 : TEXCOORD1,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(2)]]
#endif
	out float4 oTexCoord2 : TEXCOORD2,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(3)]]
#endif
	out float4 oTexCoord3 : TEXCOORD3,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(4)]]
#endif
	out float4 oTexCoord4 : TEXCOORD4,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(5)]]
#endif
	out float4 oTexCoord5 : TEXCOORD5,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(6)]]
#endif
	out float4 oTexCoord6 : TEXCOORD6,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(7)]]
#endif
	out float4 oTexCoord7 : TEXCOORD7,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(8)]]
#endif
	out float4 oTexCoord8 : TEXCOORD8,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(9)]]
#endif
	out float4 oTexCoord9 : TEXCOORD9,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(10)]]
#endif
	out float4 oTexCoord10 : TEXCOORD10,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(11)]]
#endif
	out float4 oTexCoord11 : TEXCOORD11,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(12)]]
#endif
	out float4 oTexCoord12 : TEXCOORD12,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(13)]]
#endif
	out float4 oTexCoord13 : TEXCOORD13,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(14)]]
#endif
	out float4 oTexCoord14 : TEXCOORD14,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(15)]]
#endif
	out float4 oTexCoord15 : TEXCOORD15,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(16)]]
#endif
	out float4 oColor0 : COLOR0,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(17)]]
#endif
	out float4 oColor1 : COLOR1,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(18)]]
#endif
	out float4 oNormal0 : NORMAL0,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(19)]]
#endif
	out float4 oNormal1 : NORMAL1)
{
	float4 c252 = asfloat(uint4(0x0, 0x0, 0x0, 0x0));
	float4 c253 = asfloat(uint4(0x0, 0x0, 0x0, 0x0));
	float4 c254 = asfloat(uint4(0x0, 0x0, 0x0, 0x0));
	float4 c255 = asfloat(uint4(0x3F000000, 0x0, 0x3E000000, 0x3F800000));

	oPos = 0.0;
	oTexCoord0 = 0.0;
	oTexCoord1 = 0.0;
	oTexCoord2 = 0.0;
	oTexCoord3 = 0.0;
	oTexCoord4 = 0.0;
	oTexCoord5 = 0.0;
	oTexCoord6 = 0.0;
	oTexCoord7 = 0.0;
	oTexCoord8 = 0.0;
	oTexCoord9 = 0.0;
	oTexCoord10 = 0.0;
	oTexCoord11 = 0.0;
	oTexCoord12 = 0.0;
	oTexCoord13 = 0.0;
	oTexCoord14 = 0.0;
	oTexCoord15 = 0.0;
	oColor0 = 0.0;
	oColor1 = 0.0;
	oNormal0 = 0.0;
	oNormal1 = 0.0;

	float4 r0 = float4(float(iVertexId & 0xFFFFFFu), 0.0f, 0.0f, 0.0f);
	float4 r1 = 0.0;
	float4 r2 = 0.0;
	float4 r3 = 0.0;
	float4 r4 = 0.0;
	float4 r5 = 0.0;
	float4 r6 = 0.0;
	float4 r7 = 0.0;
	float4 r8 = 0.0;
	float4 r9 = 0.0;
	float4 r10 = 0.0;
	float4 r11 = 0.0;
	float4 r12 = 0.0;
	float4 r13 = 0.0;
	float4 r14 = 0.0;
	float4 r15 = 0.0;
	float4 r16 = 0.0;
	float4 r17 = 0.0;
	float4 r18 = 0.0;
	float4 r19 = 0.0;
	float4 r20 = 0.0;
	float4 r21 = 0.0;
	float4 r22 = 0.0;
	float4 r23 = 0.0;
	float4 r24 = 0.0;
	float4 r25 = 0.0;
	float4 r26 = 0.0;
	float4 r27 = 0.0;
	float4 r28 = 0.0;
	float4 r29 = 0.0;
	float4 r30 = 0.0;
	float4 r31 = 0.0;
	float4 r32 = 0.0;
	float4 r33 = 0.0;
	float4 r34 = 0.0;
	float4 r35 = 0.0;
	float4 r36 = 0.0;
	float4 r37 = 0.0;
	float4 r38 = 0.0;
	float4 r39 = 0.0;
	float4 r40 = 0.0;
	float4 r41 = 0.0;
	float4 r42 = 0.0;
	float4 r43 = 0.0;
	float4 r44 = 0.0;
	float4 r45 = 0.0;
	float4 r46 = 0.0;
	float4 r47 = 0.0;
	float4 r48 = 0.0;
	float4 r49 = 0.0;
	float4 r50 = 0.0;
	float4 r51 = 0.0;
	float4 r52 = 0.0;
	float4 r53 = 0.0;
	float4 r54 = 0.0;
	float4 r55 = 0.0;
	float4 r56 = 0.0;
	float4 r57 = 0.0;
	float4 r58 = 0.0;
	float4 r59 = 0.0;
	float4 r60 = 0.0;
	float4 r61 = 0.0;
	float4 r62 = 0.0;
	float4 r63 = 0.0;
	float4 r64 = 0.0;
	float4 r65 = 0.0;
	float4 r66 = 0.0;
	float4 r67 = 0.0;
	float4 r68 = 0.0;
	float4 r69 = 0.0;
	float4 r70 = 0.0;
	float4 r71 = 0.0;
	float4 r72 = 0.0;
	float4 r73 = 0.0;
	float4 r74 = 0.0;
	float4 r75 = 0.0;
	float4 r76 = 0.0;
	float4 r77 = 0.0;
	float4 r78 = 0.0;
	float4 r79 = 0.0;
	float4 r80 = 0.0;
	float4 r81 = 0.0;
	float4 r82 = 0.0;
	float4 r83 = 0.0;
	float4 r84 = 0.0;
	float4 r85 = 0.0;
	float4 r86 = 0.0;
	float4 r87 = 0.0;
	float4 r88 = 0.0;
	float4 r89 = 0.0;
	float4 r90 = 0.0;
	float4 r91 = 0.0;
	float4 r92 = 0.0;
	float4 r93 = 0.0;
	float4 r94 = 0.0;
	float4 r95 = 0.0;
	float4 r96 = 0.0;
	float4 r97 = 0.0;
	float4 r98 = 0.0;
	float4 r99 = 0.0;
	float4 r100 = 0.0;
	float4 r101 = 0.0;
	float4 r102 = 0.0;
	float4 r103 = 0.0;
	float4 r104 = 0.0;
	float4 r105 = 0.0;
	float4 r106 = 0.0;
	float4 r107 = 0.0;
	float4 r108 = 0.0;
	float4 r109 = 0.0;
	float4 r110 = 0.0;
	float4 r111 = 0.0;
	float4 r112 = 0.0;
	float4 r113 = 0.0;
	float4 r114 = 0.0;
	float4 r115 = 0.0;
	float4 r116 = 0.0;
	float4 r117 = 0.0;
	float4 r118 = 0.0;
	float4 r119 = 0.0;
	float4 r120 = 0.0;
	float4 r121 = 0.0;
	float4 r122 = 0.0;
	float4 r123 = 0.0;
	float4 r124 = 0.0;
	float4 r125 = 0.0;
	float4 r126 = 0.0;
	float4 r127 = 0.0;
	float srVertexIndex = 0.0f;
	int a0 = 0;
	int aL = 0;
	bool p0 = false;
	float ps = 0.0;

	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.z = r0.x + c255.x;
		ps = select(abs(instance_data.x) < 1.17549435e-38, select((asuint(instance_data.x) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(instance_data.x), FLT_MIN, FLT_MAX));
		r0.y = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.y = r0.z * r0.y;
	}
	{
		int aluAddress = a0;
		ps = trunc(r0.y);
		r1.x = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = -r1.x * instance_data.x + r0.x;
	}
	{
		int aluAddress = a0;
		ps = instance_data.y + r0.x;
		r0.x = ps;
	}
	{
		int aluAddress = a0;
		ps = trunc(r0.x);
		r0.x = ps;
	}
	srVertexIndex = floor(r0.x);
	r0.x = srVertexFetch(1, srVertexIndex, false).x;
	srVertexIndex = floor(r0.x);
	r2.xyz = srVertexFetch(0, srVertexIndex, false).xyz;
	srVertexIndex = floor(r0.x);
	r0.yzw = tfetchR11G11B10(asuint(srVertexFetch(48, srVertexIndex, true))).xyz;
	srVertexIndex = floor(r0.x);
	r4.xyz = tfetchR11G11B10(asuint(srVertexFetch(96, srVertexIndex, true))).xyz;
	srVertexIndex = floor(r0.x);
	r3.xy = tfetchTexcoord(g_SwappedTexcoords, srVertexFetch(80, srVertexIndex, false), 0).xy;
	srVertexIndex = floor(r0.x);
	r3.zw = tfetchTexcoord(g_SwappedTexcoords, srVertexFetch(81, srVertexIndex, false), 1).xy;
	{
		int aluAddress = a0;
		ps = r1.x + r1.x;
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.w = abs(r0.x) >= c255.y;
		ps = max(r0.x, r0.x);
		a0 = (int)clamp(floor(r0.x + 0.5), -256.0, 255.0);
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.x = r2.y + CONST_REL(100 + a0).y;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.z = dot(CONST_REL(101 + a0).yx, r2.xz) + c255.y;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.y = dot(CONST_REL(101 + a0).xz, r2.xz) + c255.y;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.yz = r1.yz + CONST_REL(100 + a0).xz;
	}
	{
		int aluAddress = a0;
		oPos.x = dot(gViewProjectionXform(0).zxyw, r1.zyxw);
	}
	{
		int aluAddress = a0;
		oPos.y = dot(gViewProjectionXform(1).zxyw, r1.zyxw);
	}
	{
		int aluAddress = a0;
		oPos.z = dot(gViewProjectionXform(2).zxyw, r1.zyxw);
	}
	{
		int aluAddress = a0;
		oPos.w = dot(gViewProjectionXform(3).zxyw, r1.zyxw);
	}
	{
		int aluAddress = a0;
		ps = max(r0.x, r0.x);
		a0 = (int)clamp(floor(r0.x + 0.5), -256.0, 255.0);
	}
	{
		int aluAddress = a0;
		ps = Kramp_lo_hi_Kao.z - Kramp_lo_hi_Kao.y;
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r2;
		int aluAddress = a0;
		r2.x = r2.y + -Kramp_lo_hi_Kao.y;
		ps = select(abs(r0.x) < 1.17549435e-38, select((asuint(r0.x) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(r0.x), FLT_MIN, FLT_MAX));
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r2;
		int aluAddress = a0;
		r2.w = saturate(r2.x * r0.x);
	}
	{
		int aluAddress = a0;
		ps = max(abs(CONST_REL(100 + aluAddress).x), abs(CONST_REL(100 + aluAddress).y));
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = max(r0.x, abs(CONST_REL(100 + a0).z));
	}
	{
		float4 aluVectorSource = r2;
		int aluAddress = a0;
		r2.xyz = -r1.xzy + gLightPos.yzx;
	}
	{
		int aluAddress = a0;
		oTexCoord1.w = -r2.w + c255.w;
		ps = max(r4.y, r4.y);
		oTexCoord1.y = ps;
	}
	{
		int aluAddress = a0;
		oTexCoord4.x = -r1.y + gViewInverse(0).w;
	}
	{
		int aluAddress = a0;
		oTexCoord4.z = -r1.z + gViewInverse(2).w;
		ps = gViewInverse(1).w - r1.x;
		oTexCoord4.y = ps;
	}
	{
		int aluAddress = a0;
		oTexCoord6.x = dot(shadowMapU.zxyw, r1.zyxw);
	}
	{
		int aluAddress = a0;
		oTexCoord6.y = dot(shadowMapV.zxyw, r1.zyxw);
	}
	{
		int aluAddress = a0;
		oTexCoord6.z = dot(shadowMapZ.zxyw, r1.zyxw);
	}
	{
		int aluAddress = a0;
		ps = max(CONST_REL(100 + aluAddress).w, CONST_REL(100 + aluAddress).w);
		oTexCoord4.w = ps;
	}
	{
		int aluAddress = a0;
		oTexCoord1.x = dot(CONST_REL(101 + a0).xz, r4.xz) + c255.y;
	}
	{
		int aluAddress = a0;
		oTexCoord1.z = dot(CONST_REL(101 + a0).yx, r4.xz) + c255.y;
	}
	{
		int aluAddress = a0;
		oTexCoord0.w = shadow_dens_vis.x * gKSEsD.z;
		ps = max(r0.z, r0.z);
		oTexCoord0.y = ps;
	}
	{
		int aluAddress = a0;
		oTexCoord2.xyz = max(r1.yxzz, r1.yxzz);
		oTexCoord2.w = 1.0;
	}
	{
		int aluAddress = a0;
		oTexCoord3.xyzw = max(r3.xyzw, r3.xyzw);
	}
	{
		int aluAddress = a0;
		oTexCoord0.x = dot(CONST_REL(101 + a0).xz, r0.yw) + c255.y;
	}
	{
		int aluAddress = a0;
		oTexCoord0.z = dot(CONST_REL(101 + a0).yx, r0.yw) + c255.y;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.y = dot(r2.yzx, r2.yzx);
		ps = c255.z * aluVectorSource.x;
		r0.x = ps;
	}
	{
		int aluAddress = a0;
		ps = select(abs(abs(r0.y)) < 1.17549435e-38, FLT_MAX, clamp(rsqrt(abs(r0.y)), FLT_MIN, FLT_MAX));
		r0.y = ps;
	}
	{
		int aluAddress = a0;
		oTexCoord5.xyz = r2.zxy * r0.yyy;
		ps = frac(abs(r0.x));
		oTexCoord5.w = ps;
	}
#ifdef CONAN_RECOMP
	oPos.xy = oPos.xy * g_ScreenXform.xy + g_ScreenXform.zw * oPos.w;
#endif
	oPos.xy += g_HalfPixelOffset * oPos.w;
	return;
}
uint g_SpecConstants() { return g_SpecConstantsRuntime; }
