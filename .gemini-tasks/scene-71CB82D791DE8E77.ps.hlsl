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

#define L0_KaS SR_LOAD_FLOAT4(Pixel, 192)
#define L0_KdC1 SR_LOAD_FLOAT4(Pixel, 384)
#define L0_KdC2 SR_LOAD_FLOAT4(Pixel, 432)
#define L0_KdE1 SR_LOAD_FLOAT4(Pixel, 256)
#define L0_KdE2 SR_LOAD_FLOAT4(Pixel, 304)
#define L0_KdL SR_LOAD_FLOAT4(Pixel, 208)
#define L0_KdM SR_LOAD_FLOAT4(Pixel, 224)
#define L0_Krim_far SR_LOAD_FLOAT4(Pixel, 672)
#define L0_Krim_near SR_LOAD_FLOAT4(Pixel, 624)
#define L0_KsG SR_LOAD_FLOAT4(Pixel, 528)
#define L0_KsS SR_LOAD_FLOAT4(Pixel, 480)
#define L0_cape_col SR_LOAD_FLOAT4(Pixel, 720)
#define L0_col_a SR_LOAD_FLOAT4(Pixel, 176)
#define L0_col_map_Texture2DDescriptorIndex SR_SHARED_UINT(0)
#define L0_col_map_Texture3DDescriptorIndex SR_SHARED_UINT(64)
#define L0_col_map_TextureCubeDescriptorIndex SR_SHARED_UINT(128)
#define L0_col_map_SamplerDescriptorIndex SR_SHARED_UINT(192)
#define L0_dC1_col SR_LOAD_FLOAT4(Pixel, 416)
#define L0_dC1_exp SR_LOAD_FLOAT4(Pixel, 400)
#define L0_dC2_col SR_LOAD_FLOAT4(Pixel, 464)
#define L0_dC2_exp SR_LOAD_FLOAT4(Pixel, 448)
#define L0_dE1_col SR_LOAD_FLOAT4(Pixel, 288)
#define L0_dE1_exp SR_LOAD_FLOAT4(Pixel, 272)
#define L0_dE2_col SR_LOAD_FLOAT4(Pixel, 336)
#define L0_dE2_exp SR_LOAD_FLOAT4(Pixel, 320)
#define L0_dM_exp SR_LOAD_FLOAT4(Pixel, 240)
#define L0_mod_scal SR_LOAD_FLOAT4(Pixel, 352)
#define L0_mod_tran SR_LOAD_FLOAT4(Pixel, 368)
#define L0_rim_exp_far SR_LOAD_FLOAT4(Pixel, 704)
#define L0_rim_exp_near SR_LOAD_FLOAT4(Pixel, 656)
#define L0_rim_far SR_LOAD_FLOAT4(Pixel, 608)
#define L0_rim_near SR_LOAD_FLOAT4(Pixel, 592)
#define L0_rim_sha_far SR_LOAD_FLOAT4(Pixel, 688)
#define L0_rim_sha_near SR_LOAD_FLOAT4(Pixel, 640)
#define L0_sG_col SR_LOAD_FLOAT4(Pixel, 576)
#define L0_sG_exp SR_LOAD_FLOAT4(Pixel, 544)
#define L0_sG_sha SR_LOAD_FLOAT4(Pixel, 560)
#define L0_sS_col SR_LOAD_FLOAT4(Pixel, 512)
#define L0_sS_exp SR_LOAD_FLOAT4(Pixel, 496)
#define gEyePos SR_LOAD_FLOAT4(Pixel, 0)
#define gLightPos SR_LOAD_FLOAT4(Pixel, 16)
#define gSupesFil5 SR_LOAD_FLOAT4(Pixel, 96)
#define gSupesFil6 SR_LOAD_FLOAT4(Pixel, 112)
#define gSupesFil7 SR_LOAD_FLOAT4(Pixel, 128)
#define gSupesKADkmf SR_LOAD_FLOAT4(Pixel, 144)
#define gSupesKey1 SR_LOAD_FLOAT4(Pixel, 32)
#define gSupesKey2 SR_LOAD_FLOAT4(Pixel, 48)
#define gSupesKey3 SR_LOAD_FLOAT4(Pixel, 64)
#define gSupesMid4 SR_LOAD_FLOAT4(Pixel, 80)
#define gSupes_rim_col SR_LOAD_FLOAT4(Pixel, 160)
#define CONST_REL(INDEX) select((uint)(INDEX) < 256, SR_LOAD_FLOAT4(Pixel, min((uint)(INDEX), 255) * 16), 0.0)

#else

cbuffer PixelShaderConstants : register(b1, space4)
{
	float4 g_PixelShaderConstantsArr[256] : packoffset(c0);
};

#define CONST_REL(INDEX) select((uint)(INDEX) < 256, g_PixelShaderConstantsArr[min((uint)(INDEX), 255)], 0.0)
#define L0_KaS g_PixelShaderConstantsArr[12]
#define L0_KdC1 g_PixelShaderConstantsArr[24]
#define L0_KdC2 g_PixelShaderConstantsArr[27]
#define L0_KdE1 g_PixelShaderConstantsArr[16]
#define L0_KdE2 g_PixelShaderConstantsArr[19]
#define L0_KdL g_PixelShaderConstantsArr[13]
#define L0_KdM g_PixelShaderConstantsArr[14]
#define L0_Krim_far g_PixelShaderConstantsArr[42]
#define L0_Krim_near g_PixelShaderConstantsArr[39]
#define L0_KsG g_PixelShaderConstantsArr[33]
#define L0_KsS g_PixelShaderConstantsArr[30]
#define L0_cape_col g_PixelShaderConstantsArr[45]
#define L0_col_a g_PixelShaderConstantsArr[11]
#define L0_dC1_col g_PixelShaderConstantsArr[26]
#define L0_dC1_exp g_PixelShaderConstantsArr[25]
#define L0_dC2_col g_PixelShaderConstantsArr[29]
#define L0_dC2_exp g_PixelShaderConstantsArr[28]
#define L0_dE1_col g_PixelShaderConstantsArr[18]
#define L0_dE1_exp g_PixelShaderConstantsArr[17]
#define L0_dE2_col g_PixelShaderConstantsArr[21]
#define L0_dE2_exp g_PixelShaderConstantsArr[20]
#define L0_dM_exp g_PixelShaderConstantsArr[15]
#define L0_mod_scal g_PixelShaderConstantsArr[22]
#define L0_mod_tran g_PixelShaderConstantsArr[23]
#define L0_rim_exp_far g_PixelShaderConstantsArr[44]
#define L0_rim_exp_near g_PixelShaderConstantsArr[41]
#define L0_rim_far g_PixelShaderConstantsArr[38]
#define L0_rim_near g_PixelShaderConstantsArr[37]
#define L0_rim_sha_far g_PixelShaderConstantsArr[43]
#define L0_rim_sha_near g_PixelShaderConstantsArr[40]
#define L0_sG_col g_PixelShaderConstantsArr[36]
#define L0_sG_exp g_PixelShaderConstantsArr[34]
#define L0_sG_sha g_PixelShaderConstantsArr[35]
#define L0_sS_col g_PixelShaderConstantsArr[32]
#define L0_sS_exp g_PixelShaderConstantsArr[31]
#define gEyePos g_PixelShaderConstantsArr[0]
#define gLightPos g_PixelShaderConstantsArr[1]
#define gSupesFil5 g_PixelShaderConstantsArr[6]
#define gSupesFil6 g_PixelShaderConstantsArr[7]
#define gSupesFil7 g_PixelShaderConstantsArr[8]
#define gSupesKADkmf g_PixelShaderConstantsArr[9]
#define gSupesKey1 g_PixelShaderConstantsArr[2]
#define gSupesKey2 g_PixelShaderConstantsArr[3]
#define gSupesKey3 g_PixelShaderConstantsArr[4]
#define gSupesMid4 g_PixelShaderConstantsArr[5]
#define gSupes_rim_col g_PixelShaderConstantsArr[10]

cbuffer SharedConstants : register(b2, space4)
{
	uint L0_col_map_Texture2DDescriptorIndex : packoffset(c0.x);
	uint L0_col_map_Texture3DDescriptorIndex : packoffset(c4.x);
	uint L0_col_map_TextureCubeDescriptorIndex : packoffset(c8.x);
	uint L0_col_map_SamplerDescriptorIndex : packoffset(c12.x);
	DEFINE_SHARED_CONSTANTS();
};

#endif

#ifndef __spirv__
[shader("pixel")]
#endif
void main(
	in float4 iPos : SV_Position,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(0)]]
#endif
	in float4 iTexCoord0 : TEXCOORD0,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(1)]]
#endif
	in float4 iTexCoord1 : TEXCOORD1,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(2)]]
#endif
	in float4 iTexCoord2 : TEXCOORD2,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(3)]]
#endif
	in float4 iTexCoord3 : TEXCOORD3,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(4)]]
#endif
	in float4 iTexCoord4 : TEXCOORD4,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(5)]]
#endif
	in float4 iTexCoord5 : TEXCOORD5,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(6)]]
#endif
	in float4 iTexCoord6 : TEXCOORD6,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(7)]]
#endif
	in float4 iTexCoord7 : TEXCOORD7,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(8)]]
#endif
	in float4 iTexCoord8 : TEXCOORD8,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(9)]]
#endif
	in float4 iTexCoord9 : TEXCOORD9,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(10)]]
#endif
	in float4 iTexCoord10 : TEXCOORD10,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(11)]]
#endif
	in float4 iTexCoord11 : TEXCOORD11,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(12)]]
#endif
	in float4 iTexCoord12 : TEXCOORD12,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(13)]]
#endif
	in float4 iTexCoord13 : TEXCOORD13,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(14)]]
#endif
	in float4 iTexCoord14 : TEXCOORD14,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(15)]]
#endif
	in float4 iTexCoord15 : TEXCOORD15,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(16)]]
#endif
	in float4 iColor0 : COLOR0,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(17)]]
#endif
	in float4 iColor1 : COLOR1,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(18)]]
#endif
	in float4 iNormal0 : NORMAL0,
#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)
[[vk::location(19)]]
#endif
	in float4 iNormal1 : NORMAL1,
#ifdef __spirv__
	in bool iFace : SV_IsFrontFace
#else
	in uint iFace : SV_IsFrontFace
#endif
,
	out float4 oC0 : SV_Target0)
{
	float4 c248 = asfloat(uint4(0x0, 0x0, 0x0, 0x0));
	float4 c249 = asfloat(uint4(0xC0490FDB, 0x3FA00000, 0x3F19999A, 0x40A03828));
	float4 c250 = asfloat(uint4(0x3F8CCCCD, 0x3F3851EC, 0x3C535A86, 0x3FA66666));
	float4 c251 = asfloat(uint4(0x3F3851EC, 0xBE4CCCCD, 0x3F800000, 0x40400000));
	float4 c252 = asfloat(uint4(0xBF000000, 0xBF800000, 0x0, 0x3F800000));
	float4 c253 = asfloat(uint4(0x0, 0x3ECCCCCD, 0x3E3851EC, 0x3F6E147B));
	float4 c254 = asfloat(uint4(0x3D8F5C29, 0x3F3851EC, 0x3F800000, 0x3F000000));
	float4 c255 = asfloat(uint4(0x40C90FDB, 0x0, 0x0, 0x0));

	float4 r0 = iTexCoord0;
	float4 r1 = iTexCoord1;
	float4 r2 = iTexCoord2;
	float4 r3 = iTexCoord3;
	float4 r4 = iTexCoord4;
	float4 r5 = 
#ifdef CONAN_RECOMP
		float4((iPos.xy * g_SrScenePixelScale - 0.5) * float2(iFace ? 1.0 : -1.0, 1.0), 0.0, 0.0);
#else
		float4((iPos.xy - 0.5) * float2(iFace ? 1.0 : -1.0, 1.0), 0.0, 0.0);
#endif
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
	float2 pixelCoord = 0.0;
	CubeMapData cubeMapData = (CubeMapData)0;

// conan_tfetch slot=0 mag=3 min=3 mip=3 aniso=7
	pixelCoord = getPixelCoord(L0_col_map_Texture2DDescriptorIndex, r3.xy);
	r11.xyzw = tfetch2D(L0_col_map_Texture2DDescriptorIndex, L0_col_map_SamplerDescriptorIndex, r3.xy, float2(0, 0)).xyzw;
	{
		float4 aluVectorSource = r10;
		int aluAddress = a0;
		r10.yzw = select(c252.zzw == 0.0, L0_KdL.zxx, c254.zzz);
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.w = dot(r0.zxy, r0.zxy);
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.z = r3.y + -c250.z;
	}
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.yz = r3.xx * c252.xy;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.x = L0_rim_exp_far.x + -L0_rim_exp_near.x;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.y = L0_rim_sha_far.x + -L0_rim_sha_near.x;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.z = L0_Krim_far.x + -L0_Krim_near.x;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.xyz = -r1.xyz + gLightPos.xyz;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.w = L0_rim_far.x + -L0_rim_near.x;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.xyz = r1.xyz + -gEyePos.xyz;
		ps = select(abs(r5.x) < 1.17549435e-38, select((asuint(r5.x) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(r5.x), FLT_MIN, FLT_MAX));
		r2.w = ps;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.x = dot(r1.zxy, r1.zxy);
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.z = r11.w + -c254.z;
		ps = select(abs(aluVectorSource.w) < 1.17549435e-38, select((asuint(aluVectorSource.w) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(aluVectorSource.w), FLT_MIN, FLT_MAX));
		r1.y = ps;
	}
	{
		int aluAddress = a0;
		oC0.w = saturate(r1.z * L0_col_a.x + c254.z);
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.y = dot(r6.zxy, r6.zxy);
		ps = sqrt(abs(r1.x));
		r1.x = ps;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.x = saturate(r1.y * r1.x);
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.yzw = r1.xxx * r7.xyz;
	}
	{
		int aluAddress = a0;
		ps = L0_rim_sha_near.x + r1.z;
		r1.x = ps;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.x = -r1.x + c254.z;
		ps = saturate(r3.x + r3.x);
		r4.w = ps;
	}
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.xw = r4.xw * c253.zw;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.xyzw = r3.wxzy + c254.xyzw;
		ps = c254.y - r3.x;
		r4.x = ps;
	}
	{
		float4 aluVectorSource = r5;
		int aluAddress = a0;
		r5.y = r7.y + -r4.x;
		ps = select(abs(r7.x) < 1.17549435e-38, select((asuint(r7.x) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(r7.x), FLT_MIN, FLT_MAX));
		r1.x = ps;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.z = r4.z * r1.x;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.w = r4.z * r7.z;
		ps = max(r11.w, r11.w);
		r1.x = ps;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.w = r4.w * c249.w + c254.w;
	}
	{
		float4 aluVectorSource = r5;
		int aluAddress = a0;
		r5.x = frac(r4.w);
		ps = saturate(c250.w - r1.x);
		r4.w = ps;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.x = r5.x * c255.x + c249.x;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.z = saturate(-r4.z + c249.z);
		ps = sin(r1.x);
		r1.x = ps;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.w = r7.w * r1.x;
		ps = select(abs(abs(r4.y)) < 1.17549435e-38, FLT_MAX, clamp(rsqrt(abs(r4.y)), FLT_MIN, FLT_MAX));
		r4.y = ps;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.xyzw = r6.zxyw * r4.yyyw;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.yzw = r16.zxy + r2.yzx;
		ps = select(abs(abs(r0.w)) < 1.17549435e-38, FLT_MAX, clamp(rsqrt(abs(r0.w)), FLT_MIN, FLT_MAX));
		r0.w = ps;
	}
	{
		float4 aluVectorSource = r5;
		int aluAddress = a0;
		r5.xzw = r0.www * r0.zxy + r16.www;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = dot(r16.xyz, r2.zxy);
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.y = saturate(r0.x * L0_mod_scal.x + L0_mod_tran.x);
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = dot(r5.xzw, r5.xzw);
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.w = dot(r8.zwy, r8.zwy);
		ps = select(abs(abs(aluVectorSource.x)) < 1.17549435e-38, FLT_MAX, clamp(rsqrt(abs(aluVectorSource.x)), FLT_MIN, FLT_MAX));
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyz = r5.wxz * r0.xxx;
	}
	{
		float4 aluVectorSource = r5;
		int aluAddress = a0;
		r5.x = dot(-r16.xyz, r9.yzx);
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.y = dot(r9.yzx, r2.zxy);
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.x = dot(r9.yzx, r16.xyz);
	}
	{
		float4 aluVectorSource = r12;
		int aluAddress = a0;
		r12.z = gSupesFil5.w > r15.x;
		ps = max(-L0_sG_sha.x, -L0_sG_sha.x);
	}
	{
		float4 aluVectorSource = r12;
		int aluAddress = a0;
		r12.w = gSupesMid4.w > r15.x;
		ps = c254.z + ps;
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.z = r15.x >= gSupesFil6.w;
		ps = c253.z * r0.x;
		r13.y = ps;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.w = r15.x >= gSupesFil5.w;
		ps = r2.w >= 0.0;
		r5.z = ps;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.w = gSupesFil6.w > r15.x;
		ps = L0_Krim_near.x + r1.w;
		r10.x = ps;
	}
	{
		float4 aluVectorSource = r5;
		int aluAddress = a0;
		r5.w = r15.x >= gSupesFil7.w;
		ps = L0_rim_exp_near.x + r1.y;
		r8.x = ps;
	}
	{
		float4 aluVectorSource = r14;
		int aluAddress = a0;
		r14.x = saturate(max(-r15.x, -r15.x));
		ps = saturate(max(r15.x, r15.x));
		r13.w = ps;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.x = saturate(r14.x + r3.x);
		ps = max(r4.y, r4.y);
	}
	{
		float4 aluVectorSource = r5;
		int aluAddress = a0;
		r5.xz = r5.xz + r5.xz;
		ps = r15.x * ps;
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r13;
		int aluAddress = a0;
		r13.xz = max(r4.yz, c253.xy);
		ps = select(abs(abs(r0.x)) < 1.17549435e-38, FLT_MIN, clamp(log2(abs(r0.x)), FLT_MIN, FLT_MAX));
		r0.z = ps;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.yzw = r13.zzz * L0_cape_col.xyz + -r11.xyz;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.x = r5.z + -c254.z;
		ps = max(r0.y, r0.y);
	}
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.xy = r13.wy + c251.yx;
		ps = r13.x * ps;
		r0.y = ps;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.yz = -r13.xy + c250.xy;
		ps = saturate(max(-r1.x, -r1.x));
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r13;
		int aluAddress = a0;
		r13.xyzw = r9.zxyw * r5.xxxw;
		ps = select(abs(abs(r0.y)) < 1.17549435e-38, FLT_MIN, clamp(log2(abs(r0.y)), FLT_MIN, FLT_MAX));
		r0.y = ps;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.yzw = -r16.xyz + -r13.zxy;
		ps = select(abs(abs(r0.w)) < 1.17549435e-38, FLT_MAX, clamp(rsqrt(abs(r0.w)), FLT_MIN, FLT_MAX));
		r3.w = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.w = r0.y * L0_dC2_exp.x;
		ps = select(abs(abs(r4.y)) < 1.17549435e-38, FLT_MIN, clamp(log2(abs(r4.y)), FLT_MIN, FLT_MAX));
		r3.z = ps;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.yzw = r1.zwy * r0.xxx + r11.yzx;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = dot(r6.yzw, r6.yzw);
		ps = exp2(aluVectorSource.w);
		r0.w = ps;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xyzw = r8.xwyz * r3.zwww;
		ps = L0_dM_exp.x * r0.z;
		r3.z = ps;
	}
	{
		float4 aluVectorSource = r5;
		int aluAddress = a0;
		r5.z = dot(r9.yzx, r8.wyz);
		ps = select(abs(abs(r0.x)) < 1.17549435e-38, FLT_MAX, clamp(rsqrt(abs(r0.x)), FLT_MIN, FLT_MAX));
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.yzw = r6.wyz * r0.xxx;
		ps = L0_dC1_exp.x * r0.y;
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r5;
		int aluAddress = a0;
		r5.x = dot(r6.zwy, r2.zxy);
		ps = saturate(c249.y * r3.x);
		r15.w = ps;
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.yz = max(r5.xz, c253.xx);
		ps = exp2(r0.x);
		r0.y = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = -r15.y * L0_sS_exp.x + L0_sS_exp.x;
	}
	{
		float4 aluVectorSource = r2;
		int aluAddress = a0;
		r2.x = saturate(r0.y * L0_KdC1.x);
		ps = select(abs(abs(r15.z)) < 1.17549435e-38, FLT_MIN, clamp(log2(abs(r15.z)), FLT_MIN, FLT_MAX));
		r0.y = ps;
	}
	{
		float4 aluVectorSource = r11;
		int aluAddress = a0;
		r11.xy = gSupesKey1.ww > r15.yx;
		ps = saturate(max(r1.x, r1.x));
		r0.z = ps;
	}
	{
		float4 aluVectorSource = r11;
		int aluAddress = a0;
		r11.zw = gSupesKey2.ww > r15.yx;
		ps = L0_KsG.x * r0.z;
		r4.w = ps;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xy = r15.xy >= gSupesKey2.ww;
		ps = L0_KdE1.x * r0.z;
		r5.w = ps;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.zw = r15.xy >= gSupesKey3.ww;
		ps = L0_KsS.x * r0.z;
		r6.w = ps;
	}
	{
		float4 aluVectorSource = r12;
		int aluAddress = a0;
		r12.xy = gSupesKey3.ww > r15.yx;
		ps = r13.w != 0.0;
		r1.x = ps;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.xy = r15.xy >= gSupesMid4.ww;
		ps = max(r3.y, r3.y);
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.z = r0.x + r15.y;
		ps = -r4.z + ps;
		r2.z = ps;
	}
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.xy = -r15.xw + c251.zw;
		ps = L0_KaS.x * r1.w;
		r5.x = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = r3.y + -r15.w;
		ps = select(abs(aluVectorSource.z) < 1.17549435e-38, select((asuint(aluVectorSource.z) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(aluVectorSource.z), FLT_MIN, FLT_MAX));
		r0.z = ps;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.y = r15.y * r0.z;
		ps = r15.w * r15.w;
		r2.w = ps;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.xyzw = r12.yxzw * r7.xyzw;
		ps = L0_sG_exp.x * r0.y;
		r0.z = ps;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyzw = r11.yxwz * r9.xyzw;
		ps = select(abs(r5.y) < 1.17549435e-38, select((asuint(r5.y) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(r5.y), FLT_MIN, FLT_MAX));
		r2.y = ps;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.xyzw = r9.yxwz != c253.xxxx;
		ps = exp2(r8.x);
		r0.y = ps;
	}
	{
		float4 aluVectorSource = r17;
		int aluAddress = a0;
		r17.xyzw = r7.zyxw != c253.xxxx;
		ps = exp2(r0.z);
		r0.z = ps;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.xy = -r4.xz + r0.yz;
		ps = exp2(r3.z);
		r0.z = ps;
	}
	{
		float4 aluVectorSource = r14;
		int aluAddress = a0;
		r14.yz = r2.ww * r0.xz;
		ps = select(abs(r2.z) < 1.17549435e-38, select((asuint(r2.z) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(r2.z), FLT_MIN, FLT_MAX));
		r2.z = ps;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xyzw = r17.xxxx * gSupesFil6.xyzw;
		ps = L0_KaS.x * r1.y;
		r5.y = ps;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyzw = r17.xxxx * gSupesFil5.xyzw;
		ps = max(abs(r3.x), abs(r3.x));
	}
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.xyzw = r16.zzzz * gSupesKey2.xyzw;
		ps = L0_KdL.y * ps;
		r14.w = ps;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.xyzw = r16.zzzz * gSupesKey3.xyzw;
		ps = select(abs(abs(r14.y)) < 1.17549435e-38, FLT_MIN, clamp(log2(abs(r14.y)), FLT_MIN, FLT_MAX));
		r0.y = ps;
	}
	{
		float4 aluVectorSource = r11;
		int aluAddress = a0;
		r11.x = dot(r14.xyw, r10.yzw);
		ps = L0_KaS.x * r1.z;
		r5.z = ps;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.xyzw = r17.yyyy * gSupesMid4.xyzw + r7.xyzw;
	}
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.xyzw = r17.yyyy * gSupesKey3.xyzw + r3.xyzw;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyzw = r1.xxxx * gSupesFil6.xyzw + r9.xyzw;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xyzw = r1.xxxx * gSupesFil7.xyzw + r8.xyzw;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xyzw = r17.wwww * gSupesFil5.xyzw + r8.xyzw;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyzw = r17.wwww * gSupesMid4.xyzw + r9.xyzw;
	}
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.xyzw = r16.xxxx * gSupesKey1.xyzw + r3.xyzw;
	}
	{
		float4 aluVectorSource = r12;
		int aluAddress = a0;
		r12.xyzw = r16.xxxx * gSupesKey2.xyzw + r7.xyzw;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.x = -r12.w + r15.y;
		ps = max(r4.x, r4.x);
	}
	{
		float4 aluVectorSource = r13;
		int aluAddress = a0;
		r13.xyzw = r3.xyzw + -r12.xyzw;
		ps = saturate(r2.y * ps);
		r3.x = ps;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyzw = r17.zzzz * gSupesKey3.xyzw + r9.xyzw;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xyzw = r17.zzzz * gSupesMid4.xyzw + r8.xyzw;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xyzw = r16.wwww * gSupesKey3.xyzw + r8.xyzw;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyzw = r16.wwww * gSupesKey2.xyzw + r9.xyzw;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyzw = r16.yyyy * gSupesKey1.xyzw + r9.xyzw;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xyzw = r16.yyyy * gSupesKey2.xyzw + r8.xyzw;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyzw = r9.xyzw + -r8.xyzw;
		ps = select(abs(r13.w) < 1.17549435e-38, select((asuint(r13.w) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(r13.w), FLT_MIN, FLT_MAX));
		r3.y = ps;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.y = -r8.w + r15.x;
		ps = select(abs(r9.w) < 1.17549435e-38, select((asuint(r9.w) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(r9.w), FLT_MIN, FLT_MAX));
		r3.z = ps;
	}
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.yz = r7.xy * r3.yz;
		ps = max(r4.y, r4.y);
	}
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.yw = saturate(max(abs(r3.yz), abs(r3.yz)));
		ps = saturate(r2.z * ps);
		r3.z = ps;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.xyzw = r3.xyzw * r3.xyzw;
		ps = max(r14.z, r14.z);
	}
	{
		float4 aluVectorSource = r14;
		int aluAddress = a0;
		r14.xyzw = -r3.zxyw + c251.wwww;
		ps = r0.x * ps;
		r11.y = ps;
	}
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.xyzw = r14.xzyw + -r3.zyxw;
		ps = L0_dE2_exp.x * r0.y;
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.xyz = r7.wyz * r3.wyx;
		ps = exp2(r0.x);
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r10;
		int aluAddress = a0;
		r10.yzw = saturate(r4.yyy * r13.xyz + r12.xyz);
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.yzw = saturate(r4.xxx * r9.yzx + r8.yzx);
	}
	{
		float4 aluVectorSource = r2;
		int aluAddress = a0;
		r2.yzw = r8.yzw * r0.xxx;
		ps = L0_dE1_exp.x * r0.y;
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyzw = r11.yxxy * r8.zzyy;
		ps = exp2(r0.x);
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.xyz = r8.yzw * r0.xxx;
		ps = max(r11.y, r11.y);
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.xyz = r4.zzz * r10.yzw;
		ps = r8.w * ps;
		r8.x = ps;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.xyz = r4.xyz * L0_sG_col.xyz;
		ps = max(r11.x, r11.x);
	}
	{
		float4 aluVectorSource = r11;
		int aluAddress = a0;
		r11.yz = r9.wx * r1.yz;
		ps = r8.w * ps;
		r8.y = ps;
	}
	{
		float4 aluVectorSource = r11;
		int aluAddress = a0;
		r11.xw = r8.xy * r1.ww;
		ps = L0_dE1_col.x * r0.z;
		r8.x = ps;
	}
	{
		float4 aluVectorSource = r12;
		int aluAddress = a0;
		r12.xyzw = r6.xyyy * r10.xyzw;
		ps = L0_dE1_col.y * r0.x;
		r8.y = ps;
	}
	{
		float4 aluVectorSource = r2;
		int aluAddress = a0;
		r2.yzw = r2.zyw * L0_dE2_col.zyx;
		ps = L0_dE1_col.z * r0.y;
		r8.z = ps;
	}
	{
		float4 aluVectorSource = r10;
		int aluAddress = a0;
		r10.xyz = r12.yzw * L0_sS_col.xyz;
		ps = L0_KdE2.x * r2.w;
		r6.x = ps;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.x = r12.x * r7.x;
		ps = L0_KdE2.x * r2.z;
		r6.y = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.yz = r6.ww * r10.yx;
		ps = L0_KdE2.x * r2.y;
		r6.z = ps;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.xyw = r11.xyz * L0_KdM.xxx;
		ps = max(r6.w, r6.w);
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.z = r11.w + r7.x;
		ps = r10.z * ps;
		r2.w = ps;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.xy = r9.yz * r1.zy + r7.wy;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.xyz = r5.www * r8.yzx + r7.yxz;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.yz = r7.xz + r6.yx;
		ps = max(r7.y, r7.y);
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.yz = r1.yz * gSupesKADkmf.yy;
		ps = r6.z + ps;
		r5.w = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = dot(r5.zw, gSupesKADkmf.xy) + c253.x;
	}
	{
		float4 aluVectorSource = r2;
		int aluAddress = a0;
		r2.yz = r5.xy * gSupesKADkmf.xx + r1.zy;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.yzw = r4.www * r4.yxz + r2.zyw;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.xyz = r1.wyz + r0.xyz;
		ps = max(r1.x, r1.x);
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.xyz = -r0.zyx + gSupes_rim_col.xyz;
		ps = saturate(r3.z * ps);
		r1.w = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.xyz = r1.www * r1.xzy + r0.zxy;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.xyz = -r0.xzy + L0_dC1_col.xyz;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.xyz = r2.xxx * r1.zyx + r0.yzx;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.xyz = -r0.zyx + L0_dC2_col.xyz;
		ps = saturate(L0_KdC2.x * r0.w);
		r0.w = ps;
	}
	{
		int aluAddress = a0;
		oC0.xyz = saturate(r0.www * r1.xyz + r0.zyx);
	}
	[branch] if (g_SpecConstants() & (SPEC_CONSTANT_SOFT_PARTICLE_RGBA | SPEC_CONSTANT_SOFT_PARTICLE_ALPHA))	{		float conanSceneZ = g_Texture2DDescriptorHeap[g_SoftParticleDepth].Load(int3(iPos.xy * g_SoftParticleTexelScale, 0)).x;
		float conanFade = saturate((rcp(conanSceneZ * g_SoftParticleW.x + g_SoftParticleW.y) - rcp(iPos.z * g_SoftParticleW.x + g_SoftParticleW.y)) / g_SoftParticleDistance);
		if (g_SpecConstants() & SPEC_CONSTANT_SOFT_PARTICLE_RGBA) oC0 *= conanFade; else oC0.w *= conanFade;
	}	[branch] if (g_SpecConstants() & SPEC_CONSTANT_ALPHA_TEST)	{		clip(oC0.w - g_AlphaThreshold);
	}	else if (g_SpecConstants() & SPEC_CONSTANT_ALPHA_TO_COVERAGE)	{		oC0.w *= 1.0 + computeMipLevel(pixelCoord) * 0.25;
		oC0.w = 0.5 + (oC0.w - g_AlphaThreshold) / max(fwidth(oC0.w), 1e-6);
	}	return;
}
uint g_SpecConstants() { return g_SpecConstantsRuntime; }
