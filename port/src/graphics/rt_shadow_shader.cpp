#include "rt_shadow_shader.h"

#include <utility>

namespace superman_returns::graphics {
namespace {
constexpr std::string_view kAnchor = "r0.x = saturate(-r4.x + r0.x);";
constexpr std::string_view kCall =
    "r0.x = SrRtShadowOcclusion(iTexCoord2.xyz, r0.x, r4.x, iPos.xy);";
constexpr std::string_view kAuditedBlock =
    "if (p0)\n\t{\n\t\t{\n\t\t\tfloat4 aluVectorSource = r0;\n"
    "\t\t\tint aluAddress = a0;\n\t\t\tr0.x = saturate(-r4.x + r0.x);\n\t\t}\n\t}";

// Own implementation. This replaces only the atlas occlusion value, leaving
// the game's distance fade and later gShadCol lighting/color mix in control.
constexpr std::string_view kPrefix = R"hlsl(// SrRtShadow patch v1; PS 8A6D + canonical-world VS 2C45 only.
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
    uint3 g_SrRtShadowPadding;
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
}  // namespace

bool IsRtShadowShader(uint64_t hash) { return hash == 0x8A6DD1D52752FAD6ull; }

bool SpecializeRtShadowShader(uint64_t hash, std::string_view source,
                              std::string& output, std::string& error) {
  auto reject = [&](const char* why) {
    output.clear();
    error = why;
    return false;
  };
  if (!IsRtShadowShader(hash)) return reject("RT shadow PS hash is not audited");
  if (!Unique(source, kAnchor)) return reject("RT shadow atlas anchor count must be exactly one");
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
  const auto block = audit.find(kAuditedBlock);
  if (block == std::string::npos || block < body)
    return reject("RT shadow atlas anchor is outside its audited p0 block");

  const auto end = source.find(kAnchor) + kAnchor.size();
  std::string result(kPrefix);
  result.append(source.substr(0, end));
  result.append(source.find("\r\n") != std::string_view::npos ? "\r\n\t\t\t" : "\n\t\t\t");
  result.append(kCall);
  result.append(source.substr(end));
  output = std::move(result);
  error.clear();
  return true;
}

std::string_view RtShadowShaderCacheIdentity() {
  static const std::string identity = std::string(kPrefix) + std::string(kAnchor) + std::string(kCall);
  return identity;
}
}  // namespace superman_returns::graphics
