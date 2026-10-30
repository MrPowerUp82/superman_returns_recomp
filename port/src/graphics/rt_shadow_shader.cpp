#include "rt_shadow_shader.h"

#include <utility>

namespace superman_returns::graphics {
namespace {
// The family differs only in which registers hold occlusion and fade.
constexpr std::string_view kAnchor = "r0.x = saturate(-r4.x + r0.x);";
constexpr std::string_view kCall =
    "r0.x = SrRtShadowOcclusion(iTexCoord2.xyz, r0.x, r4.x, iPos.xy);";
constexpr uint64_t kAuditedShaders[] = {
    0x8A6DD1D52752FAD6ull, 0x0D62162D76A532D1ull, 0x220353EB37491B13ull, 0x22D23F60BBED8FA3ull,
    0x2E3CBDA4129FADBFull, 0x3730C6864F8CC9FEull, 0x692292B3CA5D3AB8ull, 0x847B6D3F16B81BA9ull,
    0x8C2DC655A4D70AE2ull, 0xADE73AD093F4ABBEull, 0xC59CC828D0923977ull, 0xCD5A5ADBA05B093Bull,
    0xE6C740EAC16B70D4ull, 0xF590EE61ABB72FD0ull};

struct FadeBlock {
  std::string anchor, call, block;
};
// Finds "rD.x = saturate(-rS.x + rD.x);" with the exact p0 emitter block
// around it. More or fewer than one such block rejects the shader.
bool FindFadeBlock(std::string_view audit, FadeBlock& found) {
  constexpr std::string_view kHead = "int aluAddress = a0;\n\t\t\tr";
  int matches = 0;
  for (auto at = audit.find(kHead); at != std::string_view::npos; at = audit.find(kHead, at + 1)) {
    auto cursor = at + kHead.size();
    const auto number = [&] {
      const auto begin = cursor;
      while (cursor < audit.size() && audit[cursor] >= '0' && audit[cursor] <= '9') ++cursor;
      return std::string(audit.substr(begin, cursor - begin));
    };
    const auto literal = [&](std::string_view text) {
      if (audit.substr(cursor, text.size()) != text) return false;
      cursor += text.size();
      return true;
    };
    const auto dst = number();
    if (dst.empty() || !literal(".x = saturate(-r")) continue;
    const auto src = number();
    if (src.empty() || !literal(".x + r") || number() != dst || !literal(".x);")) continue;
    FadeBlock block;
    block.anchor = "r" + dst + ".x = saturate(-r" + src + ".x + r" + dst + ".x);";
    block.call = "r" + dst + ".x = SrRtShadowOcclusion(iTexCoord2.xyz, r" + dst + ".x, r" + src + ".x, iPos.xy);";
    block.block = "if (p0)\n\t{\n\t\t{\n\t\t\tfloat4 aluVectorSource = r" + dst +
                  ";\n\t\t\tint aluAddress = a0;\n\t\t\t" + block.anchor + "\n\t\t}\n\t}";
    if (audit.find(block.block) == std::string_view::npos) continue;
    found = std::move(block);
    ++matches;
  }
  return matches == 1;
}

// Own implementation. This replaces only the atlas occlusion value, leaving
// the game's distance fade and later gShadCol lighting/color mix in control.
constexpr std::string_view kPrefix = R"hlsl(// SrRtShadow patch v2; audited fade-block family + canonical-world VS 2C45 only.
RaytracingAccelerationStructure g_SrRtShadowScene : register(t0, space6);
RWByteAddressBuffer g_SrRtExecuted : register(u0, space6);
cbuffer SrRtShadowDraw : register(b3, space4)
{
    float3 g_SrRtShadowLight;
    uint g_SrRtShadowEnabled;
    float g_SrRtShadowBias;
    float g_SrRtShadowMaxDistance;
    float g_SrRtShadowCone;
    uint g_SrRtShadowSamples;
    uint g_SrRtShadowMask;
    uint g_SrRtReflectEnabled;
    uint2 g_SrRtShadowPadding;
};

float SrRtShadowOcclusion(float3 world, float original, float fade, float2 pixel)
{
    float lightLength2 = dot(g_SrRtShadowLight, g_SrRtShadowLight);
    [branch] if (g_SrRtShadowEnabled == 0 ||
        !all(isfinite(world)) || !all(isfinite(pixel)) ||
        !isfinite(original) || !isfinite(fade) ||
        !all(isfinite(g_SrRtShadowLight)) || !isfinite(lightLength2) ||
        lightLength2 < 1e-12 ||
        !isfinite(g_SrRtShadowBias) || g_SrRtShadowBias < 0.0 ||
        !isfinite(g_SrRtShadowMaxDistance) ||
        g_SrRtShadowMaxDistance <= g_SrRtShadowBias ||
        !isfinite(g_SrRtShadowCone) || g_SrRtShadowCone < 0.0 ||
        g_SrRtShadowCone > 1.0 ||
        (g_SrRtShadowSamples != 4 && g_SrRtShadowSamples != 16) ||
        g_SrRtShadowMask == 0 || g_SrRtShadowMask > 255)
        return original;

    float3 light = g_SrRtShadowLight * rsqrt(lightLength2);
    float3 origin = world + light * g_SrRtShadowBias;
    [branch] if (!all(isfinite(origin))) return original;
    float3 axis = abs(light.z) < 0.99 ? float3(0, 0, 1) : float3(0, 1, 0);
    float3 tangent = normalize(cross(axis, light));
    float3 bitangent = cross(light, tangent);
    // Fixed spatial phase and equal-area cone disk samples: no frame seed,
    // Low = 4 rays, High = 16 rays. Cone is the disk radius at unit distance.
    float phase = 6.28318530718 * frac(dot(floor(pixel), float2(0.06711056, 0.00583715)));
    uint visible = 0;
    uint active = WaveActiveCountBits(true);
    if (WaveIsFirstLane())
    {
        uint ignored;
        g_SrRtExecuted.InterlockedAdd(0, active, ignored);
    }
    [loop] for (uint sampleIndex = 0; sampleIndex < g_SrRtShadowSamples; ++sampleIndex)
    {
        float radius = g_SrRtShadowCone * sqrt((float(sampleIndex) + 0.5) / float(g_SrRtShadowSamples));
        float angle = phase + float(sampleIndex) * 2.39996322973;
        float sine, cosine;
        sincos(angle, sine, cosine);
        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = normalize(light + radius * (cosine * tangent + sine * bitangent));
        ray.TMin = g_SrRtShadowBias;
        ray.TMax = g_SrRtShadowMaxDistance;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
                 RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(g_SrRtShadowScene, RAY_FLAG_NONE, g_SrRtShadowMask, ray);
        while (query.Proceed()) {}
        visible += query.CommittedStatus() == COMMITTED_NOTHING ? 1u : 0u;
    }
    float occlusion = 1.0 - float(visible) / float(g_SrRtShadowSamples);
    return saturate(occlusion - fade);
}

// Occlusion only: the scene has no hit materials yet, so a hit darkens the
// environment sample instead of supplying the reflected color.
float SrRtEnvVisibility(float3 world, float3 direction)
{
    float length2 = dot(direction, direction);
    [branch] if (g_SrRtReflectEnabled == 0 || !all(isfinite(world)) ||
        !all(isfinite(direction)) || !isfinite(length2) || length2 < 1e-12)
        return 1.0;
    RayDesc ray;
    ray.Origin = world;
    ray.Direction = direction * rsqrt(length2);
    ray.TMin = 0.25;
    ray.TMax = 2000.0;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
             RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(g_SrRtShadowScene, RAY_FLAG_NONE, 1, ray);
    while (query.Proceed()) {}
    return query.CommittedStatus() == COMMITTED_NOTHING ? 1.0 : 0.25;
}

)hlsl";

bool Unique(std::string_view source, std::string_view needle) {
  const auto at = source.find(needle);
  return at != std::string_view::npos &&
         source.find(needle, at + needle.size()) == std::string_view::npos;
}

bool AuditedInput(std::string_view inputs, std::string_view declaration) {
  if (!Unique(inputs, declaration)) return false;
  const auto end = inputs.find_first_not_of(" \t\n", inputs.find(declaration) + declaration.size());
  return end != std::string_view::npos && (inputs[end] == ',' || inputs[end] == ')');
}
// Multiplies the single env_map sample by the ray result, reading the world
// direction cube() saved. Shaders without exactly one such sample stay unchanged.
std::string PatchEnvironmentSample(std::string_view source) {
  constexpr std::string_view kFetch =
      " = tfetchCube(env_map_TextureCubeDescriptorIndex, env_map_SamplerDescriptorIndex, r";
  std::string result(source);
  const auto fetch = source.find(kFetch);
  if (fetch == std::string_view::npos || !Unique(source, kFetch)) return result;
  const auto line = source.rfind('\n', fetch);
  if (line == std::string_view::npos) return result;
  const auto target = source.substr(line + 1, fetch - line - 1);
  const auto first = target.find_first_not_of(" \t");
  if (first == std::string_view::npos || target[first] != 'r') return result;
  const auto lhs = target.substr(first);
  auto cursor = fetch + kFetch.size();
  const auto digits = cursor;
  while (cursor < source.size() && source[cursor] >= '0' && source[cursor] <= '9') ++cursor;
  const auto coordinate = source.substr(digits, cursor - digits);
  constexpr std::string_view kTail = ".xyz, cubeMapData).xyz;";
  if (coordinate.empty() || source.substr(cursor, kTail.size()) != kTail) return result;
  const auto end = cursor + kTail.size();
  std::string call(source.find("\r\n") != std::string_view::npos ? "\r\n\t" : "\n\t");
  call.append(lhs).append(" *= SrRtEnvVisibility(iTexCoord2.xyz, cubeMapData.cubeMapDirections[int(r");
  call.append(coordinate).append(".z)].xyz);");
  result.insert(end, call);
  return result;
}
}  // namespace

bool IsRtShadowShader(uint64_t hash) {
  for (const auto audited : kAuditedShaders) if (hash == audited) return true;
  return false;
}

bool SpecializeRtShadowShader(uint64_t hash, std::string_view source,
                              std::string& output, std::string& error) {
  auto reject = [&](const char* why) {
    output.clear();
    error = why;
    return false;
  };
  if (!IsRtShadowShader(hash)) return reject("RT shadow PS hash is not audited");
  if (source.find("SrRtShadow") != std::string_view::npos)
    return reject("RT shadow shader already specialized or has conflicting symbols");

  // Validate the exact emitter block, accepting either newline convention.
  // Keep the original bytes for output (including all material/HUD logic).
  std::string audit;
  audit.reserve(source.size());
  for (char c : source) if (c != '\r') audit += c;
  const auto main = audit.find("void main(");
  const auto body = audit.find('{', main);
  if (!Unique(audit, "void main(") || body == std::string::npos)
    return reject("RT shadow main entry is not audited");
  const std::string_view inputs(audit.data() + main, body - main);
  if (!AuditedInput(inputs, "in float4 iTexCoord2 : TEXCOORD2") ||
      !AuditedInput(inputs, "in float4 iPos : SV_Position"))
    return reject("RT shadow requires audited world TEXCOORD2 and SV_Position inputs");
  FadeBlock fade;
  if (!FindFadeBlock(audit, fade))
    return reject("RT shadow atlas anchor must be in exactly one audited p0 block");
  if (!Unique(source, fade.anchor)) return reject("RT shadow atlas anchor count must be exactly one");
  const auto block = audit.find(fade.block);
  if (block == std::string::npos || block < body)
    return reject("RT shadow atlas anchor is outside its audited p0 block");

  const auto patched = PatchEnvironmentSample(source);
  const std::string_view patched_view(patched);
  const auto end = patched_view.find(fade.anchor) + fade.anchor.size();
  std::string result(kPrefix);
  result.append(patched_view.substr(0, end));
  result.append(source.find("\r\n") != std::string_view::npos ? "\r\n\t\t\t" : "\n\t\t\t");
  result.append(fade.call);
  result.append(patched_view.substr(end));
  output = std::move(result);
  error.clear();
  return true;
}

std::string_view RtShadowShaderCacheIdentity() {
  static const std::string identity = std::string(kPrefix) + std::string(kAnchor) + std::string(kCall);
  return identity;
}
}  // namespace superman_returns::graphics
