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

#define A_KdL SR_LOAD_FLOAT4(Pixel, 176)
#define A_sS_col_exp SR_LOAD_FLOAT4(Pixel, 192)
#define A_tint SR_LOAD_FLOAT4(Pixel, 208)
#define B_KdL SR_LOAD_FLOAT4(Pixel, 128)
#define B_sS_col_exp SR_LOAD_FLOAT4(Pixel, 144)
#define B_tint SR_LOAD_FLOAT4(Pixel, 160)
#define G_KdL SR_LOAD_FLOAT4(Pixel, 16)
#define G_sS_col_exp SR_LOAD_FLOAT4(Pixel, 32)
#define G_tint SR_LOAD_FLOAT4(Pixel, 48)
#define KLight SR_LOAD_FLOAT4(Pixel, 496)
#define KaS SR_LOAD_FLOAT4(Pixel, 3664)
#define Kdirt_scale_inv SR_LOAD_FLOAT4(Pixel, 288)
#define Kenv SR_LOAD_FLOAT4(Pixel, 80)
#define Kramp_lo_hi_Kao SR_LOAD_FLOAT4(Pixel, 256)
#define KsS SR_LOAD_FLOAT4(Pixel, 3680)
#define Kwarp_scale_Nscale SR_LOAD_FLOAT4(Pixel, 416)
#define R_KdL SR_LOAD_FLOAT4(Pixel, 96)
#define R_sS_col_exp SR_LOAD_FLOAT4(Pixel, 112)
#define R_tint SR_LOAD_FLOAT4(Pixel, 0)
#define UV_freq_offs SR_LOAD_FLOAT4(Pixel, 224)
#define ao_col_exp SR_LOAD_FLOAT4(Pixel, 240)
#define ao_map_Texture2DDescriptorIndex SR_SHARED_UINT(20)
#define ao_map_Texture3DDescriptorIndex SR_SHARED_UINT(84)
#define ao_map_TextureCubeDescriptorIndex SR_SHARED_UINT(148)
#define ao_map_SamplerDescriptorIndex SR_SHARED_UINT(212)
#define col_map_Texture2DDescriptorIndex SR_SHARED_UINT(0)
#define col_map_Texture3DDescriptorIndex SR_SHARED_UINT(64)
#define col_map_TextureCubeDescriptorIndex SR_SHARED_UINT(128)
#define col_map_SamplerDescriptorIndex SR_SHARED_UINT(192)
#define depthBias_FadeRange_FalloffStart_FalloffRate SR_LOAD_FLOAT4(Pixel, 752)
#define dirt_freq_sel SR_LOAD_FLOAT4(Pixel, 304)
#define dirt_tint1 SR_LOAD_FLOAT4(Pixel, 320)
#define dirt_tint2_exp SR_LOAD_FLOAT4(Pixel, 336)
#define env_map_Texture2DDescriptorIndex SR_SHARED_UINT(12)
#define env_map_Texture3DDescriptorIndex SR_SHARED_UINT(76)
#define env_map_TextureCubeDescriptorIndex SR_SHARED_UINT(140)
#define env_map_SamplerDescriptorIndex SR_SHARED_UINT(204)
#define gBuildingTint SR_LOAD_FLOAT4(Pixel, 448)
#define gEnvMapTint SR_LOAD_FLOAT4(Pixel, 464)
#define gFil5 SR_LOAD_FLOAT4(Pixel, 608)
#define gFil6 SR_LOAD_FLOAT4(Pixel, 624)
#define gFil7 SR_LOAD_FLOAT4(Pixel, 640)
#define gKADkmf SR_LOAD_FLOAT4(Pixel, 656)
#define gKADkmf_LOD SR_LOAD_FLOAT4(Pixel, 528)
#define gKSEsD SR_LOAD_FLOAT4(Pixel, 672)
#define gKdirt_scale_inv SR_LOAD_FLOAT4(Pixel, 352)
#define gKey1 SR_LOAD_FLOAT4(Pixel, 544)
#define gKey2 SR_LOAD_FLOAT4(Pixel, 560)
#define gKey3 SR_LOAD_FLOAT4(Pixel, 576)
#define gLight_freq_Klight SR_LOAD_FLOAT4(Pixel, 480)
#define gLodFade_nr_far SR_LOAD_FLOAT4(Pixel, 512)
#define gMid4 SR_LOAD_FLOAT4(Pixel, 592)
#define gNormScale SR_LOAD_FLOAT4(Pixel, 432)
#define gOverBright SR_LOAD_FLOAT4(Pixel, 688)
#define gShadCol SR_LOAD_FLOAT4(Pixel, 704)
#define gdirt_freq_sel SR_LOAD_FLOAT4(Pixel, 368)
#define gdirt_tint1 SR_LOAD_FLOAT4(Pixel, 384)
#define gdirt_tint2_exp SR_LOAD_FLOAT4(Pixel, 400)
#define mat_map_Texture2DDescriptorIndex SR_SHARED_UINT(8)
#define mat_map_Texture3DDescriptorIndex SR_SHARED_UINT(72)
#define mat_map_TextureCubeDescriptorIndex SR_SHARED_UINT(136)
#define mat_map_SamplerDescriptorIndex SR_SHARED_UINT(200)
#define noise_map_Texture2DDescriptorIndex SR_SHARED_UINT(16)
#define noise_map_Texture3DDescriptorIndex SR_SHARED_UINT(80)
#define noise_map_TextureCubeDescriptorIndex SR_SHARED_UINT(144)
#define noise_map_SamplerDescriptorIndex SR_SHARED_UINT(208)
#define nrm_map_Texture2DDescriptorIndex SR_SHARED_UINT(4)
#define nrm_map_Texture3DDescriptorIndex SR_SHARED_UINT(68)
#define nrm_map_TextureCubeDescriptorIndex SR_SHARED_UINT(132)
#define nrm_map_SamplerDescriptorIndex SR_SHARED_UINT(196)
#define ramp_col_exp SR_LOAD_FLOAT4(Pixel, 272)
#define sS_exp SR_LOAD_FLOAT4(Pixel, 3696)
#define samp_depth_Texture2DDescriptorIndex SR_SHARED_UINT(60)
#define samp_depth_Texture3DDescriptorIndex SR_SHARED_UINT(124)
#define samp_depth_TextureCubeDescriptorIndex SR_SHARED_UINT(188)
#define samp_depth_SamplerDescriptorIndex SR_SHARED_UINT(252)
#define shadowMapOffsetScales(INDEX) select((INDEX) < 211, SR_LOAD_FLOAT4(Pixel, (45 + min(INDEX, 210)) * 16), 0.0)
#define shadowMapSliceWidth SR_LOAD_FLOAT4(Pixel, 768)
#define CONST_REL(INDEX) select((uint)(INDEX) < 256, SR_LOAD_FLOAT4(Pixel, min((uint)(INDEX), 255) * 16), 0.0)

#else

cbuffer PixelShaderConstants : register(b1, space4)
{
	float4 g_PixelShaderConstantsArr[256] : packoffset(c0);
};

#define CONST_REL(INDEX) select((uint)(INDEX) < 256, g_PixelShaderConstantsArr[min((uint)(INDEX), 255)], 0.0)
#define A_KdL g_PixelShaderConstantsArr[11]
#define A_sS_col_exp g_PixelShaderConstantsArr[12]
#define A_tint g_PixelShaderConstantsArr[13]
#define B_KdL g_PixelShaderConstantsArr[8]
#define B_sS_col_exp g_PixelShaderConstantsArr[9]
#define B_tint g_PixelShaderConstantsArr[10]
#define G_KdL g_PixelShaderConstantsArr[1]
#define G_sS_col_exp g_PixelShaderConstantsArr[2]
#define G_tint g_PixelShaderConstantsArr[3]
#define KLight g_PixelShaderConstantsArr[31]
#define KaS g_PixelShaderConstantsArr[229]
#define Kdirt_scale_inv g_PixelShaderConstantsArr[18]
#define Kenv g_PixelShaderConstantsArr[5]
#define Kramp_lo_hi_Kao g_PixelShaderConstantsArr[16]
#define KsS g_PixelShaderConstantsArr[230]
#define Kwarp_scale_Nscale g_PixelShaderConstantsArr[26]
#define R_KdL g_PixelShaderConstantsArr[6]
#define R_sS_col_exp g_PixelShaderConstantsArr[7]
#define R_tint g_PixelShaderConstantsArr[0]
#define UV_freq_offs g_PixelShaderConstantsArr[14]
#define ao_col_exp g_PixelShaderConstantsArr[15]
#define depthBias_FadeRange_FalloffStart_FalloffRate g_PixelShaderConstantsArr[47]
#define dirt_freq_sel g_PixelShaderConstantsArr[19]
#define dirt_tint1 g_PixelShaderConstantsArr[20]
#define dirt_tint2_exp g_PixelShaderConstantsArr[21]
#define gBuildingTint g_PixelShaderConstantsArr[28]
#define gEnvMapTint g_PixelShaderConstantsArr[29]
#define gFil5 g_PixelShaderConstantsArr[38]
#define gFil6 g_PixelShaderConstantsArr[39]
#define gFil7 g_PixelShaderConstantsArr[40]
#define gKADkmf g_PixelShaderConstantsArr[41]
#define gKADkmf_LOD g_PixelShaderConstantsArr[33]
#define gKSEsD g_PixelShaderConstantsArr[42]
#define gKdirt_scale_inv g_PixelShaderConstantsArr[22]
#define gKey1 g_PixelShaderConstantsArr[34]
#define gKey2 g_PixelShaderConstantsArr[35]
#define gKey3 g_PixelShaderConstantsArr[36]
#define gLight_freq_Klight g_PixelShaderConstantsArr[30]
#define gLodFade_nr_far g_PixelShaderConstantsArr[32]
#define gMid4 g_PixelShaderConstantsArr[37]
#define gNormScale g_PixelShaderConstantsArr[27]
#define gOverBright g_PixelShaderConstantsArr[43]
#define gShadCol g_PixelShaderConstantsArr[44]
#define gdirt_freq_sel g_PixelShaderConstantsArr[23]
#define gdirt_tint1 g_PixelShaderConstantsArr[24]
#define gdirt_tint2_exp g_PixelShaderConstantsArr[25]
#define ramp_col_exp g_PixelShaderConstantsArr[17]
#define sS_exp g_PixelShaderConstantsArr[231]
#define shadowMapOffsetScales(INDEX) CONST_REL(45 + (INDEX))
#define shadowMapSliceWidth g_PixelShaderConstantsArr[48]

cbuffer SharedConstants : register(b2, space4)
{
	uint ao_map_Texture2DDescriptorIndex : packoffset(c1.y);
	uint ao_map_Texture3DDescriptorIndex : packoffset(c5.y);
	uint ao_map_TextureCubeDescriptorIndex : packoffset(c9.y);
	uint ao_map_SamplerDescriptorIndex : packoffset(c13.y);
	uint col_map_Texture2DDescriptorIndex : packoffset(c0.x);
	uint col_map_Texture3DDescriptorIndex : packoffset(c4.x);
	uint col_map_TextureCubeDescriptorIndex : packoffset(c8.x);
	uint col_map_SamplerDescriptorIndex : packoffset(c12.x);
	uint env_map_Texture2DDescriptorIndex : packoffset(c0.w);
	uint env_map_Texture3DDescriptorIndex : packoffset(c4.w);
	uint env_map_TextureCubeDescriptorIndex : packoffset(c8.w);
	uint env_map_SamplerDescriptorIndex : packoffset(c12.w);
	uint mat_map_Texture2DDescriptorIndex : packoffset(c0.z);
	uint mat_map_Texture3DDescriptorIndex : packoffset(c4.z);
	uint mat_map_TextureCubeDescriptorIndex : packoffset(c8.z);
	uint mat_map_SamplerDescriptorIndex : packoffset(c12.z);
	uint noise_map_Texture2DDescriptorIndex : packoffset(c1.x);
	uint noise_map_Texture3DDescriptorIndex : packoffset(c5.x);
	uint noise_map_TextureCubeDescriptorIndex : packoffset(c9.x);
	uint noise_map_SamplerDescriptorIndex : packoffset(c13.x);
	uint nrm_map_Texture2DDescriptorIndex : packoffset(c0.y);
	uint nrm_map_Texture3DDescriptorIndex : packoffset(c4.y);
	uint nrm_map_TextureCubeDescriptorIndex : packoffset(c8.y);
	uint nrm_map_SamplerDescriptorIndex : packoffset(c12.y);
	uint samp_depth_Texture2DDescriptorIndex : packoffset(c3.w);
	uint samp_depth_Texture3DDescriptorIndex : packoffset(c7.w);
	uint samp_depth_TextureCubeDescriptorIndex : packoffset(c11.w);
	uint samp_depth_SamplerDescriptorIndex : packoffset(c15.w);
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
	float4 c244 = asfloat(uint4(0x0, 0x0, 0x0, 0x0));
	float4 c245 = asfloat(uint4(0x40200000, 0x3F400000, 0x0, 0x0));
	float4 c246 = asfloat(uint4(0x3EE66666, 0x3ECCCCCD, 0x3F10A3D7, 0x41000000));
	float4 c247 = asfloat(uint4(0x40000000, 0x3F266666, 0x443B8000, 0x3F19999A));
	float4 c248 = asfloat(uint4(0xC0E00000, 0xC0C00000, 0xBF800000, 0x3F800000));
	float4 c249 = asfloat(uint4(0x0, 0x3F800000, 0x3E800000, 0x40800000));
	float4 c250 = asfloat(uint4(0x3EE0C49C, 0x3EDEB852, 0x3F0F9DB2, 0x3F41CAC1));
	float4 c251 = asfloat(uint4(0x3EC49BA6, 0x3F076C8B, 0x3F1A9FBE, 0x3F61CAC1));
	float4 c252 = asfloat(uint4(0x3F38D4FE, 0x3F33B646, 0x3F0978D5, 0x3D4CCCCD));
	float4 c253 = asfloat(uint4(0xC0400000, 0xC0000000, 0x3F000000, 0x3FC00000));
	float4 c254 = asfloat(uint4(0x3EFAE148, 0x3F1CAC08, 0x3F0B851F, 0x3F178D50));
	float4 c255 = asfloat(uint4(0xC0A00000, 0xC0800000, 0xC0400000, 0xC0000000));

	float4 r0 = iTexCoord0;
	float4 r1 = iTexCoord1;
	float4 r2 = iTexCoord2;
	float4 r3 = iTexCoord3;
	float4 r4 = iTexCoord4;
	float4 r5 = iTexCoord5;
	float4 r6 = iTexCoord6;
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

	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.y = r4.w > gLodFade_nr_far.x;
		ps = saturate(max(r6.x, r6.x));
		r12.x = ps;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.z = dot(r0.zxy, r0.zxy);
		ps = saturate(max(r6.y, r6.y));
		r12.y = ps;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.w = dot(r1.zxy, r1.zxy);
		ps = max(c249.y, c249.y);
		r8.y = ps;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.x = -r4.w + gLodFade_nr_far.y;
		ps = max(c249.y, c249.y);
		r14.z = ps;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.w = gLodFade_nr_far.y + -gLodFade_nr_far.x;
		ps = max(c249.y, c249.y);
		r14.y = ps;
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.x = dot(r4.zxy, r4.zxy);
		ps = select(abs(r6.w) < 1.17549435e-38, select((asuint(r6.w) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(r6.w), FLT_MIN, FLT_MAX));
		r6.w = ps;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.w = saturate(r8.x * r6.w);
		ps = select(abs(abs(r7.x)) < 1.17549435e-38, FLT_MAX, clamp(rsqrt(abs(r7.x)), FLT_MIN, FLT_MAX));
		r8.x = ps;
	}
	{
		float4 aluVectorSource = r11;
		int aluAddress = a0;
		r11.xyz = r8.xxx * r4.xyz;
		ps = select(abs(abs(r7.w)) < 1.17549435e-38, FLT_MAX, clamp(rsqrt(abs(r7.w)), FLT_MIN, FLT_MAX));
		r4.x = ps;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xzw = r4.xxx * r1.zyx;
		ps = select(abs(abs(r7.z)) < 1.17549435e-38, FLT_MAX, clamp(rsqrt(abs(r7.z)), FLT_MIN, FLT_MAX));
		r4.x = ps;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyz = r4.xxx * r0.yxz;
		ps = max(c249.y, c249.y);
		r14.x = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.y = dot(r9.zyx, r5.zxy);
		p0 = r7.y != 0.0;
		ps = p0 ? 0.0 : 1.0;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.w = saturate(max(r0.y, r0.y));
		ps = sqrt(abs(r7.x));
		r4.x = ps;
	}
	if (p0)
	{
		{
			int aluAddress = a0;
			ps = saturate(max(-r0.y, -r0.y));
			r7.x = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r7;
			int aluAddress = a0;
			r7.xyz = r7.xxx * gFil7.xyz;
			ps = max(r0.y, r0.y);
			r8.y = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r14;
			int aluAddress = a0;
			r14.xyz = r9.www * gKey1.zyx + r7.zyx;
		}
	}
	{
		float4 aluVectorSource = r7;
		int aluAddress = a0;
		r7.xyz = r2.zyx * Kwarp_scale_Nscale.yyy;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.xy = r3.xy * UV_freq_offs.xy + UV_freq_offs.zw;
	}
// conan_tfetch slot=1 mag=3 min=3 mip=3 aniso=7
	r4.yz = tfetch2D(nrm_map_Texture2DDescriptorIndex, nrm_map_SamplerDescriptorIndex, r1.xy, float2(0, 0)).xy;
// conan_tfetch slot=4 mag=3 min=3 mip=3 aniso=7
	r16.xyz = tfetch3D(noise_map_Texture3DDescriptorIndex, noise_map_SamplerDescriptorIndex, r7.xyz).xyz;
// conan_tfetch slot=2 mag=3 min=3 mip=3 aniso=7
	r7.xyw = tfetch2D(mat_map_Texture2DDescriptorIndex, mat_map_SamplerDescriptorIndex, r1.xy, float2(0, 0)).zxy;
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.y = gLodFade_nr_far.x > r4.w;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.z = Kwarp_scale_Nscale.z * gNormScale.x;
	}
	{
		float4 aluVectorSource = r10;
		int aluAddress = a0;
		r10.xy = r4.zy * c247.xx + c248.zz;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.yw = r9.zz * r8.zw;
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.xyzw = r9.xxyy * r8.wxxz;
		ps = r16.z + r16.z;
		r13.x = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = saturate(dot(r7.xyw, c249.yyy));
		ps = r16.y + r16.y;
		r13.y = ps;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.z = r4.w + -r15.z;
		ps = r16.x + r16.x;
		r13.z = ps;
	}
	{
		float4 aluVectorSource = r10;
		int aluAddress = a0;
		r10.yz = r0.zz * r10.yx;
		ps = max(r15.y, r15.y);
	}
	{
		float4 aluVectorSource = r13;
		int aluAddress = a0;
		r13.w = dot(r10.yz, r10.yz) + c249.x;
	}
	{
		float4 aluVectorSource = r13;
		int aluAddress = a0;
		r13.xyzw = r13.yxzw + c248.zzzw;
		ps = -r4.y + ps;
		r4.w = ps;
	}
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.x = dot(r13.yzx, r13.yzx);
		ps = select(abs(abs(r13.w)) < 1.17549435e-38, FLT_MAX, clamp(rsqrt(abs(r13.w)), FLT_MIN, FLT_MAX));
		r10.x = ps;
	}
	{
		float4 aluVectorSource = r12;
		int aluAddress = a0;
		r12.zw = r10.zy * r10.xx;
		ps = select(abs(r0.y) < 1.17549435e-38, select((asuint(r0.y) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(r0.y), FLT_MIN, FLT_MAX));
		r10.y = ps;
	}
	{
		float4 aluVectorSource = r10;
		int aluAddress = a0;
		r10.xyzw = r9.yxzw * r10.xxxy;
		ps = r15.w - r15.x;
		r4.y = ps;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.yzw = r12.zzz * r4.yzw;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.yzw = r12.www * r8.zxw + r4.zyw;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.yzw = r4.zwy + r10.zxy;
		ps = Kwarp_scale_Nscale.x * r7.y;
		r0.y = ps;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xzw = r0.yyy * r13.xyz + r4.wyz;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.y = dot(r8.zwx, r8.zwx);
		ps = max(c249.y, c249.y);
		r9.x = ps;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.y = -r10.w + c249.y;
		ps = select(abs(abs(r0.y)) < 1.17549435e-38, FLT_MAX, clamp(rsqrt(abs(r0.y)), FLT_MIN, FLT_MAX));
		r0.y = ps;
	}
	{
		float4 aluVectorSource = r13;
		int aluAddress = a0;
		r13.xyz = r8.wxz * r0.yyy;
		ps = max(c249.x, c249.x);
		r2.w = ps;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.x = dot(r13.zxy, r5.zxy);
		ps = c249.y - r0.x;
		r7.z = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.yz = saturate(max(r8.xy, r8.xy));
		p0 = r3.y != 0.0;
		ps = p0 ? 0.0 : 1.0;
	}
	if (!p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.xz = max(r2.ww, r2.ww);
			ps = max(aluVectorSource.w, aluVectorSource.w);
			r4.z = ps;
		}
	}
	if (!p0)
	{
		{
			float4 aluVectorSource = r0;
			int aluAddress = a0;
			r0.x = max(r5.w, r5.w);
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r15;
			int aluAddress = a0;
			r15.xyz = gKdirt_scale_inv.zxy * Kdirt_scale_inv.zxy;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.xzw = r15.zzz * r2.xyz;
		}
	}
// conan_tfetch slot=4 mag=3 min=3 mip=3 aniso=7
	if (p0)
	{
		r16.xyzw = tfetch3D(noise_map_Texture3DDescriptorIndex, noise_map_SamplerDescriptorIndex, r2.xzw).xyzw;
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r17;
			int aluAddress = a0;
			r17.xyzw = gdirt_freq_sel.xyzw * dirt_freq_sel.xyzw;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.xzw = gdirt_tint1.xyz * dirt_tint1.xyz;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r10;
			int aluAddress = a0;
			r10.xyzw = gdirt_tint2_exp.wxyz * dirt_tint2_exp.wxyz;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r9;
			int aluAddress = a0;
			r9.yzw = r10.yzw + -r2.xzw;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r0;
			int aluAddress = a0;
			r0.x = saturate(dot(r17.wzxy, r16.wzxy));
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r0;
			int aluAddress = a0;
			r0.x = -r0.x + r15.x;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r4;
			int aluAddress = a0;
			r4.z = r15.y * abs(r0.x);
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r4;
			int aluAddress = a0;
			r4.w = r4.z + -c249.y;
			ps = select(abs(abs(aluVectorSource.z)) < 1.17549435e-38, FLT_MIN, clamp(log2(abs(aluVectorSource.z)), FLT_MIN, FLT_MAX));
			r0.x = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r0;
			int aluAddress = a0;
			r0.x = r10.x * r0.x;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r9;
			int aluAddress = a0;
			r9.x = max(-r4.w, -r4.w);
			ps = exp2(r0.x);
			r0.x = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.xzw = r9.wzy * r0.xxx + r2.wzx;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r0;
			int aluAddress = a0;
			r0.x = max(r5.w, r5.w);
		}
	}
// conan_tfetch slot=0 mag=3 min=3 mip=3 aniso=7
	pixelCoord = getPixelCoord(col_map_Texture2DDescriptorIndex, r1.xy);
	r10.xyzw = tfetch2D(col_map_Texture2DDescriptorIndex, col_map_SamplerDescriptorIndex, r1.xy, float2(0, 0)).yxzw;
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.w = c247.z > r4.x;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.yzw = r7.www * G_tint.xyz;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.z = saturate(dot(r7.xywz, gBuildingTint.zxyw));
		ps = c246.w * r0.x;
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.yzw = r7.yyy * R_tint.zxy + r9.wyz;
	}
	{
		float4 aluVectorSource = r11;
		int aluAddress = a0;
		r11.w = floor(r0.x);
		ps = select(abs(r8.x) < 1.17549435e-38, select((asuint(r8.x) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(r8.x), FLT_MIN, FLT_MAX));
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = saturate(-abs(r8.x) * r0.x + c249.y);
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.zw = r11.ww + c248.xy;
	}
	{
		float4 aluVectorSource = r12;
		int aluAddress = a0;
		r12.zw = select(r11.ww == 0.0, c250.wx, c247.ww);
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.xyzw = r11.wwww + c255.xyzw;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.yzw = r7.xxx * B_tint.yzx + r9.wyz;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.yzw = r7.zzz * A_tint.xyz + r9.wyz;
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.y = select(r16.z == 0.0, c246.z, c246.y);
	}
	{
		float4 aluVectorSource = r12;
		int aluAddress = a0;
		r12.zw = select(r16.ww == 0.0, c254.wy, r12.wz);
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.xz = select(r16.zz == 0.0, c251.wy, r12.wz);
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.xyz = select(r16.yyy == 0.0, c251.xyz, r15.xyz);
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.xyz = select(r16.xxx == 0.0, c250.xyz, r15.xyz);
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.xyz = select(r8.www == 0.0, c252.xyz, r15.xyz);
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.xyz = select(r8.zzz == 0.0, c254.xyz, r15.xyz);
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.xyz = -r9.yzw + r15.xyz;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.yzw = r15.xyz * r1.zzz + r9.yzw;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.yzw = r9.yzw * r10.yxz;
		ps = c249.y - r0.x;
		r15.x = ps;
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.yzw = r9.wyz * r2.xwz + -r9.wyz;
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.xyzw = r15.xzyw * r4.yzzz;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.z = r0.x + r15.x;
		ps = -abs(r0.x) > 0.0;
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.yzw = r9.ywz + r15.yzw;
		p0 = r4.w != 0.0;
		ps = p0 ? 0.0 : 1.0;
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r16;
			int aluAddress = a0;
			r16.z = saturate(shadowMapSliceWidth.x * c245.x);
			ps = max(shadowMapSliceWidth.x, shadowMapSliceWidth.x);
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r12;
			int aluAddress = a0;
			r12.z = saturate(shadowMapSliceWidth.x * c253.z);
			ps = saturate(c253.w * ps);
			r4.w = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r15;
			int aluAddress = a0;
			r15.xzw = r6.zyx * shadowMapOffsetScales(0).www + shadowMapOffsetScales(0).zyx;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.xzw = r15.xzw * shadowMapOffsetScales(1).www;
			ps = saturate(max(r15.w, r15.w));
			r4.y = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r16;
			int aluAddress = a0;
			r16.xy = saturate(r2.wz + shadowMapOffsetScales(1).xy);
			ps = saturate(max(r15.z, r15.z));
			r4.z = ps;
		}
	}
// conan_tfetch slot=15 mag=3 min=3 mip=3 aniso=7
	if (p0)
	{
		r4.y = tfetch3D(samp_depth_Texture3DDescriptorIndex, samp_depth_SamplerDescriptorIndex, r4.yzw).x;
	}
// conan_tfetch slot=15 mag=3 min=3 mip=3 aniso=7
	if (p0)
	{
		r4.z = tfetch3D(samp_depth_Texture3DDescriptorIndex, samp_depth_SamplerDescriptorIndex, r16.xyz).x;
	}
// conan_tfetch slot=15 mag=3 min=3 mip=3 aniso=7
	if (p0)
	{
		r4.w = tfetch3D(samp_depth_Texture3DDescriptorIndex, samp_depth_SamplerDescriptorIndex, r12.xyz).x;
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r4;
			int aluAddress = a0;
			r4.x = r4.x + -depthBias_FadeRange_FalloffStart_FalloffRate.z;
		}
	}
	if (p0)
	{
		{
			int aluAddress = a0;
			ps = shadowMapOffsetScales(1).z + r2.x;
			r15.y = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r12;
			int aluAddress = a0;
			r12.xy = saturate(r15.xy + depthBias_FadeRange_FalloffStart_FalloffRate.xx);
			ps = saturate(depthBias_FadeRange_FalloffStart_FalloffRate.x + r6.z);
			r12.z = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r4;
			int aluAddress = a0;
			r4.yzw = r12.xyz + -r4.yzw;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r4;
			int aluAddress = a0;
			r4.xyzw = saturate(r4.xyzw * depthBias_FadeRange_FalloffStart_FalloffRate.wyyy);
		}
	}
	if (p0)
	{
		{
			int aluAddress = a0;
			ps = max(r4.w, r4.y);
			r0.x = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r0;
			int aluAddress = a0;
			r0.x = max(r0.x, r4.z);
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r0;
			int aluAddress = a0;
			r0.x = saturate(-r4.x + r0.x);
		}
	}
	{
		float4 aluVectorSource = r17;
		int aluAddress = a0;
		r17.x = gFil6.w > r8.x;
	}
	{
		float4 aluVectorSource = r17;
		int aluAddress = a0;
		r17.w = gFil5.w > r8.x;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.x = r8.x >= gFil7.w;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.z = r8.x >= gFil6.w;
	}
	{
		float4 aluVectorSource = r19;
		int aluAddress = a0;
		r19.x = gKey2.w > r8.x;
	}
	{
		float4 aluVectorSource = r19;
		int aluAddress = a0;
		r19.y = gKey1.w > r8.x;
	}
	{
		float4 aluVectorSource = r19;
		int aluAddress = a0;
		r19.z = gMid4.w > r8.x;
	}
	{
		float4 aluVectorSource = r19;
		int aluAddress = a0;
		r19.w = gKey3.w > r8.x;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.x = r8.x >= gKey3.w;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.y = r8.x >= gKey2.w;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.z = r8.x >= gFil5.w;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.w = r8.x >= gMid4.w;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.w = gKey1.w == r8.x;
	}
	{
		float4 aluVectorSource = r10;
		int aluAddress = a0;
		r10.y = dot(-r11.zyx, r13.zyx);
		ps = saturate(max(-r8.x, -r8.x));
		r15.w = ps;
	}
	{
		float4 aluVectorSource = r10;
		int aluAddress = a0;
		r10.x = dot(r13.zyx, -r5.zyx);
		ps = G_KdL.x * r7.w;
		r6.x = ps;
	}
	{
		float4 aluVectorSource = r18;
		int aluAddress = a0;
		r18.xyzw = r4.wwww * gKey1.xyzw;
		ps = G_KdL.y * r7.w;
		r6.y = ps;
	}
	{
		float4 aluVectorSource = r12;
		int aluAddress = a0;
		r12.w = dot(r7.xywz, KaS.zxyw);
		ps = G_KdL.z * r7.w;
		r6.z = ps;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.xyz = r7.yyy * R_KdL.zxy + r6.zxy;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.xyz = r7.xxx * B_KdL.yzx + r6.zxy;
	}
	{
		float4 aluVectorSource = r20;
		int aluAddress = a0;
		r20.xyzw = r19.xyzw * r16.xyzw;
		ps = max(-abs(r8.x), -abs(r8.x));
	}
	{
		float4 aluVectorSource = r12;
		int aluAddress = a0;
		r12.xyz = r7.zzz * A_KdL.xyz + r6.zxy;
	}
	{
		float4 aluVectorSource = r17;
		int aluAddress = a0;
		r17.yz = r12.yw * gKADkmf.zx;
		ps = c249.y + ps;
		r4.y = ps;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.xyz = r17.xyw * r4.xyz;
		ps = r20.z != 0.0;
		r8.z = ps;
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.xy = r4.zx != c249.xx;
		ps = r20.w != 0.0;
		r6.z = ps;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.xyzw = r15.xxxx * gFil5.xyzw;
		ps = r20.y != 0.0;
		r6.x = ps;
	}
	{
		float4 aluVectorSource = r19;
		int aluAddress = a0;
		r19.xyzw = r15.xxxx * gFil6.xyzw;
		ps = r20.x != 0.0;
		r6.y = ps;
	}
	{
		float4 aluVectorSource = r19;
		int aluAddress = a0;
		r19.xyzw = r15.yyyy * gFil7.xyzw + r19.xyzw;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.xyzw = r15.yyyy * gFil6.xyzw + r16.xyzw;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.xyzw = r8.zzzz * gMid4.xyzw + r16.xyzw;
	}
	{
		float4 aluVectorSource = r19;
		int aluAddress = a0;
		r19.xyzw = r8.zzzz * gFil5.xyzw + r19.xyzw;
	}
	{
		float4 aluVectorSource = r19;
		int aluAddress = a0;
		r19.xyzw = r6.zzzz * gMid4.xyzw + r19.xyzw;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.xyzw = r6.zzzz * gKey3.xyzw + r16.xyzw;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.xyzw = r6.yyyy * gKey2.xyzw + r16.xyzw;
	}
	{
		float4 aluVectorSource = r19;
		int aluAddress = a0;
		r19.xyzw = r6.yyyy * gKey3.xyzw + r19.xyzw;
	}
	{
		float4 aluVectorSource = r19;
		int aluAddress = a0;
		r19.xyzw = r6.xxxx * gKey2.xyzw + r19.xyzw;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.xyzw = r6.xxxx * gKey1.xyzw + r16.xyzw;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.xyzw = r16.xyzw + r18.xyzw;
		ps = gKSEsD.z * r0.x;
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r18;
		int aluAddress = a0;
		r18.xyzw = r19.xyzw + r18.xyzw;
		ps = max(r1.z, r1.z);
		r6.x = ps;
	}
	{
		float4 aluVectorSource = r19;
		int aluAddress = a0;
		r19.xyzw = r16.xyzw + -r18.xyzw;
		ps = max(r0.x, r0.x);
		r6.y = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = r19.w + r4.w;
		ps = max(r6.x, r6.y);
		r15.x = ps;
	}
	{
		float4 aluVectorSource = r15;
		int aluAddress = a0;
		r15.y = -r15.x + c249.y;
		ps = select(abs(r0.x) < 1.17549435e-38, select((asuint(r0.x) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(r0.x), FLT_MIN, FLT_MAX));
		r15.z = ps;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.xy = r15.xy * gKADkmf.wy;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.zw = r6.yx * r12.xz;
	}
	{
		float4 aluVectorSource = r10;
		int aluAddress = a0;
		r10.z = r16.z * r0.y + r4.y;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.xy = r10.xy + r10.xy;
	}
	{
		float4 aluVectorSource = r21;
		int aluAddress = a0;
		r21.xyzw = r13.xxyy * r4.xyxy;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.zw = r13.zz * r4.xy;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.z = -r11.z + -r8.w;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.xy = -r11.xy + -r21.yw;
	}
	{
		float4 aluVectorSource = r4;
		int aluAddress = a0;
		r4.xyzw = cube(r4.xyzz, cubeMapData);
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.z = max(r4.w, r4.w);
		ps = select(abs(abs(r4.z)) < 1.17549435e-38, select((asuint(abs(r4.z)) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(abs(r4.z)), FLT_MIN, FLT_MAX));
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.xy = r4.yx * r0.xx + c253.ww;
	}
// conan_tfetch slot=3 mag=3 min=3 mip=3 aniso=7
	r12.yzw = tfetchCube(env_map_TextureCubeDescriptorIndex, env_map_SamplerDescriptorIndex, r6.xyz, cubeMapData).xyz;
// conan_tfetch slot=5 mag=3 min=3 mip=3 aniso=7
	r4.xyzw = tfetch2D(ao_map_Texture2DDescriptorIndex, ao_map_SamplerDescriptorIndex, r3.zw, float2(0, 0)).xzyw;
	{
		float4 aluVectorSource = r22;
		int aluAddress = a0;
		r22.yzw = gEnvMapTint.xyz * gKSEsD.yyy;
	}
	{
		float4 aluVectorSource = r13;
		int aluAddress = a0;
		r13.x = dot(r7.zxyw, KsS.wzxy);
		ps = G_sS_col_exp.x * r7.w;
		r6.x = ps;
	}
	{
		float4 aluVectorSource = r20;
		int aluAddress = a0;
		r20.x = dot(r7.zxyw, sS_exp.wzxy);
		ps = G_sS_col_exp.y * r7.w;
		r6.y = ps;
	}
	{
		float4 aluVectorSource = r13;
		int aluAddress = a0;
		r13.yzw = select(c249.xxy == 0.0, r11.xyy, c249.yyy);
	}
	{
		float4 aluVectorSource = r22;
		int aluAddress = a0;
		r22.x = dot(r7.zxyw, Kenv.wzxy);
		ps = G_sS_col_exp.z * r7.w;
		r6.z = ps;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.xyz = r7.yyy * R_sS_col_exp.zxy + r6.zxy;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.xyz = r7.xxx * B_sS_col_exp.yzx + r6.zxy;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.xyz = r7.zzz * A_sS_col_exp.xyz + r6.zxy;
	}
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.zw = r22.xy * r9.xx;
	}
	{
		float4 aluVectorSource = r20;
		int aluAddress = a0;
		r20.w = r3.w * r22.x;
	}
	{
		float4 aluVectorSource = r21;
		int aluAddress = a0;
		r21.xy = -r21.zx + -r5.yx;
		ps = max(r22.w, r22.w);
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = -r8.z + -r5.z;
		ps = r3.z * ps;
		r20.y = ps;
	}
	{
		float4 aluVectorSource = r21;
		int aluAddress = a0;
		r21.z = r11.z * r0.x;
		ps = max(r22.z, r22.z);
	}
	{
		float4 aluVectorSource = r12;
		int aluAddress = a0;
		r12.x = saturate(dot(r21.yxz, r13.yzw));
		ps = r3.z * ps;
		r20.z = ps;
	}
	{
		float4 aluVectorSource = r11;
		int aluAddress = a0;
		r11.xyzw = r20.xyzw * r12.xwzy;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = r20.x + -r11.x;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = r0.x + r12.x;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.y = -r18.w + r8.x;
		ps = select(abs(r0.x) < 1.17549435e-38, select((asuint(r0.x) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(r0.x), FLT_MIN, FLT_MAX));
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.x = r12.x * r0.x;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.xzw = r16.xyw * r15.yzw;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xzw = saturate(abs(r16.zzz) * r19.xzy + r18.xzy);
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.y = r10.z + r16.w;
	}
	{
		float4 aluVectorSource = r13;
		int aluAddress = a0;
		r13.yz = r16.yy * r8.xz + r17.zz;
	}
	{
		float4 aluVectorSource = r16;
		int aluAddress = a0;
		r16.xyzw = r16.xxxy * r8.xzww;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.xyz = r16.xyz * r6.xzy;
	}
	{
		float4 aluVectorSource = r13;
		int aluAddress = a0;
		r13.w = r17.z + r16.w;
		ps = max(r4.w, r4.w);
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyzw = r13.xyzw * r9.xyzw;
		ps = Kramp_lo_hi_Kao.w * r0.x;
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r1;
		int aluAddress = a0;
		r1.z = r9.x * gKSEsD.x;
		ps = max(r15.x, r15.x);
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xzw = r1.zzz * r6.xyz + r11.wyz;
	}
	{
		float4 aluVectorSource = r6;
		int aluAddress = a0;
		r6.xyz = r8.zxw * r12.xxx;
		ps = r0.w * ps;
		r0.w = ps;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xzw = r8.zxw + r9.zyw;
		ps = select(abs(abs(r1.w)) < 1.17549435e-38, FLT_MIN, clamp(log2(abs(r1.w)), FLT_MIN, FLT_MAX));
		r9.w = ps;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyz = r8.zwx * gShadCol.xyz + -r8.zwx;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xzw = r0.www * r9.zyx + r8.xwz;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyz = r8.zwx * ao_col_exp.yxz + -r8.zwx;
	}
	{
		float4 aluVectorSource = r9;
		int aluAddress = a0;
		r9.xyz = r0.xxx * r9.zyx + r8.xwz;
	}
	{
		float4 aluVectorSource = r11;
		int aluAddress = a0;
		r11.xyzw = r9.xzyw * ramp_col_exp.zyxw;
	}
	{
		float4 aluVectorSource = r8;
		int aluAddress = a0;
		r8.xzw = -r9.yzx + r11.zyx;
		ps = exp2(r11.w);
		r0.x = ps;
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = r0.x * Kramp_lo_hi_Kao.x;
		p0 = r3.y != 0.0;
		ps = p0 ? 0.0 : 1.0;
	}
	{
		float4 aluVectorSource = r3;
		int aluAddress = a0;
		r3.yzw = r0.xxx * r8.xzw + r9.yzx;
	}
	{
		float4 aluVectorSource = r10;
		int aluAddress = a0;
		r10.xyz = r6.zyx * gOverBright.yyy + r3.zyw;
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.xz = r1.xy * c252.ww;
			ps = c252.w * aluVectorSource.y;
			r2.w = ps;
		}
	}
// conan_tfetch slot=4 mag=3 min=3 mip=3 aniso=7
	if (p0)
	{
		r9.xyzw = tfetch3D(noise_map_Texture3DDescriptorIndex, noise_map_SamplerDescriptorIndex, r2.xzw).xyzw;
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r0;
			int aluAddress = a0;
			r0.w = KLight.x * gLight_freq_Klight.w;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r5;
			int aluAddress = a0;
			r5.yz = r2.yy * c249.zw;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r1;
			int aluAddress = a0;
			r1.w = r5.z >= -r5.z;
			ps = c253.z * aluVectorSource.x;
			r3.y = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.xzw = r9.yxw * c246.xxx;
			ps = c253.z * r1.y;
			r3.z = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r1;
			int aluAddress = a0;
			r1.xyz = saturate(gKey1.zxy * c247.yyy + r2.wxz);
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.w = r1.w * c246.w + c255.y;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.xz = frac(abs(r3.yz));
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r3;
			int aluAddress = a0;
			r3.yz = select(r3.yz >= 0.0, r2.xz, -r2.xz);
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r1;
			int aluAddress = a0;
			r1.w = saturate(dot(r9.zxy, gLight_freq_Klight.zxy));
			ps = select(abs(r2.w) < 1.17549435e-38, select((asuint(r2.w) >> 31) != 0, FLT_MIN, FLT_MAX), clamp(rcp(r2.w), FLT_MIN, FLT_MAX));
			r2.x = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r3;
			int aluAddress = a0;
			r3.yz = r3.zy + r3.zy;
			ps = max(r2.x, r2.x);
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.z = r3.y >= c249.y;
			ps = aluVectorSource.y * ps;
			r2.x = ps;
		}
	}
	if (p0)
	{
		{
			int aluAddress = a0;
			ps = frac(r2.x);
			r2.x = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.x = r2.w * r2.x;
		}
	}
	if (p0)
	{
		{
			int aluAddress = a0;
			ps = floor(r2.x);
			r5.x = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.x = select(r5.x == 0.0, c249.x, c249.z);
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r5;
			int aluAddress = a0;
			r5.xyz = r5.xxy + c253.xyz;
		}
	}
	if (p0)
	{
		{
			int aluAddress = a0;
			ps = floor(r5.z);
			r2.y = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.x = select(r5.y == 0.0, c245.y, r2.x);
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.x = select(r5.x == 0.0, c249.y, r2.x);
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.y = r3.z + r2.y;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.y = saturate(r2.y + r2.z);
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.y = r2.y >= c249.y;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.x = r2.y * r2.x;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.y = r2.x * r1.w;
			ps = r3.x + r3.x;
			r2.x = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r1;
			int aluAddress = a0;
			r1.xyzw = r2.xxxy * r1.zxyw;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r2;
			int aluAddress = a0;
			r2.xyz = r1.zxy * r7.yyy;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r1;
			int aluAddress = a0;
			r1.xyz = r2.zxy * r1.www;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r1;
			int aluAddress = a0;
			r1.xyz = r1.zxy * r0.xxx;
			ps = frac(r5.w);
			r0.x = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r1;
			int aluAddress = a0;
			r1.xyz = r1.zxy * r0.xxx;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r10;
			int aluAddress = a0;
			r10.xyz = r0.www * r1.yxz + r10.xyz;
		}
	}
	{
		float4 aluVectorSource = r0;
		int aluAddress = a0;
		r0.x = c249.y > r6.w;
	}
	{
		int aluAddress = a0;
		p0 = r0.x != 0.0;
		ps = p0 ? 0.0 : 1.0;
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r0;
			int aluAddress = a0;
			r0.xyw = select(c249.xxy == 0.0, gKADkmf_LOD.wzz, c249.yyy);
		}
	}
	if (p0)
	{
		{
			int aluAddress = a0;
			ps = saturate(max(-r8.y, -r8.y));
			r1.x = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r1;
			int aluAddress = a0;
			r1.y = -abs(r8.y) + c249.y;
			ps = gKADkmf_LOD.y * r0.z;
			r1.z = ps;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r0;
			int aluAddress = a0;
			r0.x = dot(r1.xyz, r0.xyw);
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r0;
			int aluAddress = a0;
			r0.xyz = r0.xxx * r14.xyz + gKADkmf_LOD.xxx;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r0;
			int aluAddress = a0;
			r0.xyz = saturate(r0.zxy * r4.xyz);
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r1;
			int aluAddress = a0;
			r1.xyz = -r0.zxy + r10.xyz;
		}
	}
	if (p0)
	{
		{
			float4 aluVectorSource = r10;
			int aluAddress = a0;
			r10.xyz = r6.www * r1.xyz + r0.zxy;
		}
	}
	{
		int aluAddress = a0;
		oC0.xyzw = max(r10.yxzw, r10.yxzw);
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
