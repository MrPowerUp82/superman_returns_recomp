#include "pixel_coverage_shader.h"

#include <cctype>
#include <utility>
#include <vector>

namespace superman_returns::graphics {
namespace {
constexpr size_t kMaxSourceBytes = 1024 * 1024;
constexpr std::string_view kOutput = "out float4 oC0 : SV_Target0)";
constexpr std::string_view kExtra = ",\n\tout uint srCoverageId : SV_Target1";
constexpr std::string_view kAssignment = "\n\tsrCoverageId = g_SrPixelCoverageId;\n\t";
// The cape PS initializes its pixel-position register with this exact emitter
// branch. Neither arm exits main or changes the fragment coverage tail.
constexpr std::string_view kCapePixelPosition = "float4 r5 = \n" R"hlsl(#ifdef CONAN_RECOMP
		float4((iPos.xy * g_PixelPosScale - 0.5) * float2(iFace ? 1.0 : -1.0, 1.0), 0.0, 0.0);
#else
		float4((iPos.xy - 0.5) * float2(iFace ? 1.0 : -1.0, 1.0), 0.0, 0.0);
#endif
)hlsl";
constexpr std::string_view kLoadBuffers =
    "#define SR_LOAD_FLOAT4(STAGE,OFFSET) asfloat(g_ ## STAGE ## ShaderConstants.Load4(OFFSET))";
constexpr std::string_view kLoadAddress =
    "#define SR_LOAD_FLOAT4(STAGE,OFFSET) vk::RawBufferLoad<float4>(g_PushConstants.STAGE ## ShaderConstants + (OFFSET), 0x10)";
constexpr std::string_view kPrefix = R"hlsl(// SrPixelCoverage patch v1: original PS 559F/2203/E6C7/3EF6/E83A/71CB; capture only.
cbuffer SrPixelCoverage : register(b4, space4)
{
    uint g_SrPixelCoverageId;
};

)hlsl";
// Exact normalized emitter tail, shared by the audited sources. This anchors
// the insertion AFTER soft particles, clip/discard and alpha-to-coverage logic.
constexpr std::string_view kTail = R"hlsl([branch] if (g_SpecConstants() & (SPEC_CONSTANT_SOFT_PARTICLE_RGBA | SPEC_CONSTANT_SOFT_PARTICLE_ALPHA))	{		float conanSceneZ = g_Texture2DDescriptorHeap[g_SoftParticleDepth].Load(int3(iPos.xy * g_SoftParticleTexelScale, 0)).x;
		float conanFade = saturate((rcp(conanSceneZ * g_SoftParticleW.x + g_SoftParticleW.y) - rcp(iPos.z * g_SoftParticleW.x + g_SoftParticleW.y)) / g_SoftParticleDistance);
		if (g_SpecConstants() & SPEC_CONSTANT_SOFT_PARTICLE_RGBA) oC0 *= conanFade; else oC0.w *= conanFade;
	}	[branch] if (g_SpecConstants() & SPEC_CONSTANT_ALPHA_TEST)	{		clip(oC0.w - g_AlphaThreshold);
	}	else if (g_SpecConstants() & SPEC_CONSTANT_ALPHA_TO_COVERAGE)	{		oC0.w *= 1.0 + computeMipLevel(pixelCoord) * 0.25;
		oC0.w = 0.5 + (oC0.w - g_AlphaThreshold) / max(fwidth(oC0.w), 1e-6);
	}	return;
})hlsl";

bool Unique(std::string_view source, std::string_view needle) {
  const auto at = source.find(needle);
  return at != std::string_view::npos && source.find(needle, at + needle.size()) == std::string_view::npos;
}
bool Space(char c) { return c == ' ' || c == '\t' || c == '\n'; }
bool Identifier(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}
std::string Lower(std::string_view text) {
  std::string result;
  for (unsigned char c : text) result += static_cast<char>(std::tolower(c));
  return result;
}
// Mask comments/quoted strings without changing offsets. Delimiter and keyword
// checks then cannot mistake a helper's comment/string for the main entry.
bool Mask(std::string& text) {
  for (size_t i = 0; i < text.size();) {
    const auto start = i;
    if (text.compare(i, 2, "//") == 0) {
      i = text.find('\n', i + 2);
      if (i == std::string::npos) i = text.size();
    } else if (text.compare(i, 2, "/*") == 0) {
      const auto end = text.find("*/", i + 2);
      if (end == std::string::npos) return false;
      i = end + 2;
    } else if (text[i] == '"' || text[i] == '\'') {
      const char quote = text[i++];
      bool closed = false;
      while (i < text.size()) {
        const char c = text[i++];
        if (c == '\n') return false;
        if (c == '\\') { if (i == text.size()) return false; ++i; }
        else if (c == quote) { closed = true; break; }
      }
      if (!closed) return false;
    } else { ++i; continue; }
    for (size_t p = start; p < i; ++p) if (text[p] != '\n') text[p] = ' ';
  }
  return true;
}
struct Token { size_t at, size; };
constexpr size_t kNone = std::string_view::npos;
const std::string& AuditedParameters() {
  // The original PS files have this identical complete emitter signature,
  // including inactive SPIR-V alternatives. This validates inputs as well as RT0.
  static const std::string parameters = [] {
    std::string text = "\n\tin float4 iPos : SV_Position,\n";
    for (unsigned i = 0; i < 20; ++i) {
      const auto semantic = std::string(i < 16 ? "TEXCOORD" : i < 18 ? "COLOR" : "NORMAL") +
          std::to_string(i < 16 ? i : i < 18 ? i - 16 : i - 18);
      const auto name = std::string(i < 16 ? "iTexCoord" : i < 18 ? "iColor" : "iNormal") +
          std::to_string(i < 16 ? i : i < 18 ? i - 16 : i - 18);
      text += "#if defined(__spirv__) && defined(SR_VULKAN_BUFFERS)\n[[vk::location(" +
          std::to_string(i) + ")]]\n#endif\n\tin float4 " + name + " : " + semantic + ",\n";
    }
    text += "#ifdef __spirv__\n\tin bool iFace : SV_IsFrontFace\n#else\n"
            "\tin uint iFace : SV_IsFrontFace\n#endif\n,\n\t";
    text += kOutput.substr(0, kOutput.size() - 1);
    return text;
  }();
  return parameters;
}
}  // namespace

bool IsPixelCoverageShader(uint64_t hash) {
  return hash == 0x559F422BEF5E5714ull || hash == 0x220353EB37491B13ull ||
         hash == 0xE6C740EAC16B70D4ull || hash == 0x3EF6A4A3C740ADB1ull || hash == 0xE83A13DF3297C6CBull ||
         hash == 0x71CB82D791DE8E77ull;
}
bool IsPixelCoverageShaderPair(uint64_t vs, uint64_t ps) {
  return (vs == 0x4F17209D364D5B51ull && ps == 0x559F422BEF5E5714ull) ||
         (vs == 0x18B4F69400B7F0AFull && ps == 0x220353EB37491B13ull) ||
         (vs == 0xC20921D0DBBD2F1Full && ps == 0xE6C740EAC16B70D4ull) ||
         (vs == 0xC687EB65D635C5D7ull && (ps == 0x3EF6A4A3C740ADB1ull || ps == 0xE83A13DF3297C6CBull)) ||
         (vs == 0xA5232A3184AA617Cull && ps == 0x71CB82D791DE8E77ull);
}
bool SpecializePixelCoverageShader(uint64_t hash, std::string_view source,
                                   std::string& output, std::string& error) {
  const auto reject = [&](const char* why) { output.clear(); error = why; return false; };
  if (!IsPixelCoverageShader(hash)) return reject("Pixel coverage PS hash is not audited");
  if (source.empty() || source.size() > kMaxSourceBytes)
    return reject("Pixel coverage source must be nonempty and at most 1 MiB");
  // Own normalized storage first: source may refer to output's current bytes.
  std::string normalized;
  normalized.reserve(source.size());
  for (size_t i = 0; i < source.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(source[i]);
    if (c == '\r') {
      if (i + 1 == source.size() || source[i + 1] != '\n') return reject("Lone CR in HLSL source");
      continue;
    }
    if ((c < 32 && c != '\n' && c != '\t') || c >= 127)
      return reject("Unsupported control/non-ASCII byte in HLSL source");
    if (c == '\\' && i && i + 2 < source.size() && source[i + 1] == '\n' &&
        Identifier(source[i - 1]) && Identifier(source[i + 2]))
      return reject("Token-splicing line continuation is not audited");
    if (c == '\\' && i && i + 3 < source.size() && source[i + 1] == '\r' && source[i + 2] == '\n' &&
        Identifier(source[i - 1]) && Identifier(source[i + 3]))
      return reject("Token-splicing line continuation is not audited");
    normalized += static_cast<char>(c);
  }
  for (std::string_view name : {"SrPixelCoverage", "srCoverageId", "g_SrPixelCoverageId"})
    if (normalized.find(name) != kNone) return reject("Pixel coverage reserved symbol collision");
  std::string code = normalized;
  if (!Mask(code)) return reject("Unterminated comment or string in HLSL source");
  std::vector<Token> tokens;
  for (size_t i = 0; i < code.size();) {
    if (Space(code[i])) { ++i; continue; }
    const size_t at = i++;
    if (Identifier(code[at])) while (i < code.size() && Identifier(code[i])) ++i;
    tokens.push_back({at, i - at});
  }
  const auto token = [&](size_t i) -> std::string_view {
    return i < tokens.size() ? std::string_view(code).substr(tokens[i].at, tokens[i].size) : std::string_view{};
  };
  // Known preprocessor branches in the preamble/signature are balanced in the
  // emitter text. Do not attempt preprocessing arbitrary new main-body control.
  std::vector<size_t> stack, match(tokens.size(), kNone), parent(tokens.size(), kNone);
  size_t main = kNone, target_count = 0;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const auto value = token(i);
    parent[i] = stack.empty() ? kNone : stack.back();
    if (value == "main") {
      if (main != kNone) return reject("Pixel coverage requires exactly one main definition");
      main = i;
    }
    const auto lower = Lower(value);
    if (lower.starts_with("sv_target")) {
      if (value != "SV_Target0" || ++target_count != 1)
        return reject("Pixel coverage requires only the audited SV_Target0 output");
    }
    if (lower.starts_with("sv_depth") || lower.starts_with("sv_coverage") || lower.starts_with("sv_innercoverage"))
      return reject("Pixel coverage rejects custom depth/coverage semantics");
    if (value == "register" && token(i + 1) == "(" && Lower(token(i + 2)) == "b4" &&
        token(i + 3) == "," && Lower(token(i + 4)) == "space4")
      return reject("Pixel coverage b4 space4 is already occupied");
    if (value == "#" && (token(i + 1) == "define" || token(i + 1) == "undef")) {
      size_t line_end = code.find('\n', tokens[i].at);
      while (line_end != kNone) {
        size_t last = line_end;
        while (last > tokens[i].at && (code[last - 1] == ' ' || code[last - 1] == '\t')) --last;
        if (last == tokens[i].at || code[last - 1] != '\\') break;
        line_end = code.find('\n', line_end + 1);
      }
      const auto directive = std::string_view(code).substr(tokens[i].at, line_end == kNone ? kNone : line_end - tokens[i].at);
      if (directive.find("return") != kNone) return reject("Return-producing macro is not audited");
      std::string logical;
      for (size_t p = 0; p < directive.size(); ++p) {
        if (directive[p] == '\\' && p + 1 < directive.size() && directive[p + 1] == '\n') ++p;
        else logical += directive[p];
      }
      if (logical.find("return") != kNone) return reject("Return-producing logical macro is not audited");
      size_t p = 1;
      while (p < logical.size() && Space(logical[p])) ++p;
      while (p < logical.size() && Identifier(logical[p])) ++p; // define / undef
      while (p < logical.size() && Space(logical[p])) ++p;
      const auto name_begin = p;
      while (p < logical.size() && Identifier(logical[p])) ++p;
      const auto name = std::string_view(logical).substr(name_begin, p - name_begin);
      for (std::string_view protected_name : {"main", "return", "out", "uint", "float4", "cbuffer", "register", "b4", "space4", "oC0"})
        if (name == protected_name) return reject("Pixel coverage reserved macro collision");
      if (logical.find("##") != kNone && logical != kLoadBuffers && logical != kLoadAddress)
        return reject("Token-pasting macro is not one of the audited loader forms");
    }
    if (value == "(" || value == "[" || value == "{") stack.push_back(i);
    if (value == ")" || value == "]" || value == "}") {
      if (stack.empty()) return reject("Unbalanced HLSL delimiter");
      const auto open = token(stack.back());
      if ((value == ")" && open != "(") || (value == "]" && open != "[") || (value == "}" && open != "{"))
        return reject("Mismatched HLSL delimiter");
      match[stack.back()] = i; stack.pop_back();
    }
  }
  if (!stack.empty() || main == kNone || main == 0 || token(main - 1) != "void" ||
      token(main + 1) != "(" || parent[main] != kNone || target_count != 1)
    return reject("Pixel coverage main definition is not audited");
  const size_t parameters_end = match[main + 1];
  if (parameters_end == kNone || token(parameters_end + 1) != "{")
    return reject("Pixel coverage main body is missing");
  const auto parameters_begin = tokens[main + 1].at + 1;
  if (std::string_view(normalized).substr(parameters_begin, tokens[parameters_end].at - parameters_begin) != AuditedParameters())
    return reject("Pixel coverage complete input/output signature is not audited");
  size_t outputs = 0;
  for (size_t i = main + 2; i < parameters_end; ++i) {
    if (token(i) == "inout") return reject("Pixel coverage main has an unsupported inout parameter");
    if (token(i) == "out") ++outputs;
  }
  if (outputs != 1) return reject("Pixel coverage main must have exactly the audited color output");
  const size_t body = parameters_end + 1, end = match[body];
  if (end == kNone || !Unique(code, kOutput)) return reject("Pixel coverage output declaration is not audited");
  const size_t declaration = code.find(kOutput), close = declaration + kOutput.size() - 1;
  if (declaration < tokens[main].at || close != tokens[parameters_end].at)
    return reject("Pixel coverage output declaration must finish main's parameter list");
  const size_t cape_branch=hash==0x71CB82D791DE8E77ull && Unique(normalized,kCapePixelPosition)
      ? normalized.find(kCapePixelPosition) : kNone;
  size_t returns = 0, final_return = kNone;
  for (size_t i = body + 1; i < end; ++i) {
    if (token(i) == "#" && (cape_branch==kNone || tokens[i].at<cape_branch ||
        tokens[i].at>=cape_branch+kCapePixelPosition.size()))
      return reject("Preprocessor control in main body is not audited");
    if (token(i) == "return") {
      ++returns; final_return = tokens[i].at;
      if (parent[i] != body || token(i + 1) != ";" || i + 2 != end)
        return reject("Pixel coverage requires a single unconditional final return");
    }
  }
  if (returns != 1 || !Unique(code, "void main(") || !Unique(normalized, kTail))
    return reject("Pixel coverage main or fragment tail is not audited");
  const size_t tail = normalized.find(kTail);
  if (tail <= tokens[body].at || tail + kTail.size() != tokens[end].at + 1)
    return reject("Pixel coverage fragment tail must finish main");

  // Descending edits keep normalized source offsets valid. All original bytes,
  // including helper returns and the entire original fragment tail, survive.
  normalized.insert(final_return, kAssignment);
  normalized.insert(close, kExtra);
  std::string result(kPrefix);
  result += normalized;
  output = std::move(result);
  error.clear();
  return true;
}
std::string_view PixelCoverageShaderCacheIdentity() {
  static const std::string identity = std::string("pixel-coverage-structural-audit-v1\n") +
      std::string(kPrefix) + std::string(kOutput) + std::string(kExtra) +
      std::string(kAssignment) + std::string(kTail) + std::string(kCapePixelPosition) + AuditedParameters() +
      std::string(kLoadBuffers) + std::string(kLoadAddress);
  return identity;
}
}  // namespace superman_returns::graphics
