#include "test_main.h"
#include "../../port/src/graphics/pixel_coverage_shader.h"

#include "../../port/src/graphics/rt_shadow_shader.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace superman_returns::graphics;
namespace {
constexpr std::array<uint64_t, 14> kPs{0x559F422BEF5E5714ull, 0x220353EB37491B13ull, 0xE6C740EAC16B70D4ull, 0x3EF6A4A3C740ADB1ull, 0xE83A13DF3297C6CBull, 0x71CB82D791DE8E77ull,
  0xA828328313FF36B9ull,0x1A6669D4867145BCull,0xFC11F956CA7B42A2ull,
  0xADAF9F96B596F6EFull,0x3A7A1A60DE39C81Full,0xF2BB7B6551646033ull,0x04CC5C7EC9FDFD7Eull,0xC66813A81FDBD7C7ull};
constexpr std::array<uint64_t, 14> kVs{0x4F17209D364D5B51ull, 0x18B4F69400B7F0AFull, 0xC20921D0DBBD2F1Full, 0xC687EB65D635C5D7ull, 0xC687EB65D635C5D7ull, 0xA5232A3184AA617Cull,
  0x8E5232F5E5E6760Eull,0x8E5232F5E5E6760Eull,0x8E5232F5E5E6760Eull,
  0x1023A9B52C1407C1ull,0x1023A9B52C1407C1ull,0x1023A9B52C1407C1ull,0x9A62E7C65051447Aull,0x315E6843ECE68A00ull};
constexpr std::string_view kOutput = "out float4 oC0 : SV_Target0)";
constexpr std::string_view kExtra = ",\n\tout uint srCoverageId : SV_Target1";
constexpr std::string_view kAssignment = "\n\tsrCoverageId = g_SrPixelCoverageId & 0x3fffffffu;\n\tif ((g_SrPixelCoverageId & 0x80000000u) != 0u && oC0.w != 1.0) srCoverageId |= 0x40000000u;\n\t";
constexpr std::string_view kOutputScale = "oC0 *= g_ColorOutputScale[0];\n\t";

std::string Source(uint64_t hash, bool scaled = false) {
  constexpr const char* names[]{"559F422BEF5E5714.ps.hlsl","220353EB37491B13.ps.hlsl",
      "E6C740EAC16B70D4.ps.hlsl","3EF6A4A3C740ADB1.ps.hlsl","E83A13DF3297C6CB.ps.hlsl","71CB82D791DE8E77.ps.hlsl",
      "A828328313FF36B9.ps.hlsl","1A6669D4867145BC.ps.hlsl","FC11F956CA7B42A2.ps.hlsl",
      "ADAF9F96B596F6EF.ps.hlsl","3A7A1A60DE39C81F.ps.hlsl","F2BB7B6551646033.ps.hlsl","04CC5C7EC9FDFD7E.ps.hlsl","C66813A81FDBD7C7.ps.hlsl"};
  const char* name=nullptr;
  for(size_t i=0;i<kPs.size();++i) if(hash==kPs[i]) name=names[i];
  SR_CHECK(name!=nullptr);if(!name)return {};
  const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
  std::ifstream stream(root / "artifacts/shaders/hlsl" / name, std::ios::binary);
  SR_CHECK(stream.good());
  std::string result{std::istreambuf_iterator<char>(stream), {}};
  // The input corpus contains CRLF in the preamble and LF in the main body.
  std::string lf;
  for (char c : result) if (c != '\r') lf += c;
  // Keep both audited emitter generations covered while the local corpus may
  // contain either. Only normalize the exact new final output-scale statement.
  const auto final_return = lf.rfind("return;\n}");
  SR_CHECK(final_return != std::string::npos);
  if (final_return == std::string::npos) return lf;
  if (final_return >= kOutputScale.size() &&
      lf.compare(final_return - kOutputScale.size(), kOutputScale.size(), kOutputScale) == 0)
    lf.erase(final_return - kOutputScale.size(), kOutputScale.size());
  if (scaled) lf.insert(lf.rfind("return;\n}"), kOutputScale);
  return lf;
}
void Reject(uint64_t hash, std::string_view source) {
  std::string output = "stale successful shader", error;
  SR_CHECK(!SpecializePixelCoverageShader(hash, source, output, error));
  SR_CHECK(output.empty());
  SR_CHECK(!error.empty());
}
void Replace(std::string& source, std::string_view old, std::string_view value) {
  const auto at = source.find(old);
  SR_CHECK(at != std::string::npos);
  if (at != std::string::npos) source.replace(at, old.size(), value);
}
}  // namespace

SR_TEST(pixel_coverage_only_audited_ps_and_exact_vs_ps_pairs_are_supported) {
  for (size_t i = 0; i < kPs.size(); ++i) {
    SR_CHECK(IsPixelCoverageShader(kPs[i]));
    SR_CHECK(IsPixelCoverageShaderPair(kVs[i], kPs[i]));
    for(size_t j=0;j<kVs.size();++j) if(kVs[j]!=kVs[i])
      SR_CHECK(!IsPixelCoverageShaderPair(kVs[j],kPs[i]));
    SR_CHECK(!IsPixelCoverageShaderPair(0, kPs[i]));
  }
  for (uint64_t hash : {0ull, 0xC20921D0DBBD2F1Full, 0x9A62E7C65051447Aull, kPs[0] ^ 1ull}) {
    SR_CHECK(!IsPixelCoverageShader(hash));
    Reject(hash, Source(kPs[0]));
  }
}

SR_TEST(pixel_coverage_actual_sources_keep_all_original_code_and_fragment_tail) {
  for (uint64_t hash : kPs) {
    for (bool scaled : {false, true}) {
      const auto source = Source(hash, scaled);
      std::string output, error = "stale error";
      const bool specialized = SpecializePixelCoverageShader(hash, source, output, error);
      SR_CHECK(specialized);
      if (!specialized) std::printf("  specialization error: %s\n", error.c_str());
      if (!specialized) continue;
      SR_CHECK(error.empty());
      SR_CHECK(output.find("cbuffer SrPixelCoverage : register(b4, space4)") != std::string::npos);
      SR_CHECK(output.find("uint g_SrPixelCoverageId;") != std::string::npos);
      const auto start = output.find(source.substr(0, 80));
      SR_CHECK(start != std::string::npos);
      if (start == std::string::npos) continue;
      auto restored = output.substr(start);
      const auto extra = restored.find(kExtra), assignment = restored.find(kAssignment);
      SR_CHECK(extra != std::string::npos && assignment != std::string::npos);
      if (extra == std::string::npos || assignment == std::string::npos) continue;
      SR_CHECK(assignment > restored.rfind("clip(oC0.w - g_AlphaThreshold);"));
      SR_CHECK(assignment > restored.rfind("fwidth(oC0.w)"));
      const auto scale = restored.find(kOutputScale);
      if (scaled) {
        SR_CHECK(scale != std::string::npos);
        SR_CHECK(scale > restored.rfind("clip(oC0.w - g_AlphaThreshold);"));
        SR_CHECK(scale > restored.rfind("fwidth(oC0.w)"));
        SR_CHECK(assignment > scale);
        SR_CHECK(restored.find(kOutputScale, scale + kOutputScale.size()) == std::string::npos);
      } else {
        SR_CHECK(scale == std::string::npos);
      }
      const auto final_return = restored.substr(assignment + kAssignment.size());
      SR_CHECK(final_return == "return;\n}" || final_return == "return;\n}\n");
      restored.erase(assignment, kAssignment.size());
      restored.erase(extra, kExtra.size());
      SR_CHECK(restored == source); // Original color, clip/discard, helpers and return unchanged.
      Reject(hash, output); // No double specialization.
    }
  }
}

SR_TEST(pixel_coverage_rejects_unaudited_scale_statements_and_tail_changes) {
  for (uint64_t hash : kPs) {
    for (std::string_view replacement : {
        "oC0 *= g_ColorOutputScale[1];\n\t",
        "oC1 *= g_ColorOutputScale[0];\n\t",
        "oC0.w *= g_ColorOutputScale[0];\n\t",
        "oC0 *= g_ColorOutputScale[0] / 32.0;\n\t",
        "oC0 *= g_ColorOutputScale[0];\n\toC0 *= g_ColorOutputScale[0];\n\t",
        "oC0 *= g_ColorOutputScale[0];\n\toC0 = 0;\n\t"}) {
      auto changed = Source(hash, true);
      Replace(changed, kOutputScale, replacement);
      Reject(hash, changed);
    }
    for (std::string_view anchor : {"clip(oC0.w - g_AlphaThreshold);",
                                   "oC0.w *= 1.0 + computeMipLevel(pixelCoord) * 0.25;"}) {
      auto changed = Source(hash, true);
      Replace(changed, kOutputScale, "");
      Replace(changed, anchor, std::string(kOutputScale) + std::string(anchor));
      Reject(hash, changed); // Scaling may not affect alpha-test/coverage input.
    }
    for (bool scaled : {false, true}) {
      auto changed = Source(hash, scaled);
      Replace(changed, "clip(oC0.w - g_AlphaThreshold);", "clip(oC0.w * 0.5 - g_AlphaThreshold);");
      Reject(hash, changed);
      changed = Source(hash, scaled);
      Replace(changed, "fwidth(oC0.w)", "fwidth(oC0.x)");
      Reject(hash, changed);
    }
  }
}

SR_TEST(pixel_coverage_preserves_helper_returns_and_explicit_discard_before_the_tail) {
  for (uint64_t hash : kPs) {
    auto source = Source(hash);
    const std::string helper = "float CoverageTestHelper(float x) { if (x < 0) return 0; return x; }\n";
    source.insert(0, helper);
    const auto body = source.find('{', source.find(kOutput));
    source.insert(body + 1, "\n\tif (iPos.x < 0) { discard; }\n");
    std::string output, error;
    SR_CHECK(SpecializePixelCoverageShader(hash, source, output, error));
    SR_CHECK(output.find(helper) != std::string::npos);
    SR_CHECK(output.find("if (iPos.x < 0) { discard; }") < output.find(kAssignment));
  }
}

SR_TEST(pixel_coverage_rejects_changed_output_signature_and_reserved_semantics) {
  for (std::string_view signature : {"out half4 oC0 : SV_Target0)", "out float4 oC1 : SV_Target0)",
      "out float4 oC0 : SV_Target00)", "out float4 oC0 : SV_Target1)",
      "out float4 oC0 : SV_Target0, out float4 second : SV_Target1)",
      "out float4 oC0 : SV_Target0, out float depth : SV_Depth)",
      "out float4 oC0 : SV_Target0, out uint mask : SV_Coverage)",
      "out float4 oC0 : SV_Target0, out float4 second : sv_target2)",
      "out float4 other, out float4 oC0 : SV_Target0)"}) {
    auto changed = Source(kPs[0]); Replace(changed, kOutput, signature);
    Reject(kPs[0], changed);
  }
}

SR_TEST(pixel_coverage_rejects_ambiguous_main_and_nonfinal_or_multiple_main_returns) {
  const auto source = Source(kPs[0]);
  for (std::string_view suffix : {"\nvoid main() {}\n", "\nvoid main();\n"})
    Reject(kPs[0], source + std::string(suffix));
  auto changed = source; Replace(changed, "void main(", "void other("); Reject(kPs[0], changed);
  changed = source; Replace(changed, "}\treturn;", "}\treturn; return;"); Reject(kPs[0], changed);
  changed = source; Replace(changed, "}\treturn;", "}\tif (true) return;"); Reject(kPs[0], changed);
  changed = source; Replace(changed, "}\treturn;", "}\treturn 0;"); Reject(kPs[0], changed);
  changed = source; Replace(changed, "}\treturn;", "}"); Reject(kPs[0], changed);
  changed = source; Replace(changed, "}\treturn;", "}\treturn; oC0 = 0;"); Reject(kPs[0], changed);
  changed = source; changed.insert(changed.find('{', changed.find(kOutput)) + 1, "\nif (iPos.x < 0) return;\n");
  Reject(kPs[0], changed);
}

SR_TEST(pixel_coverage_rejects_existing_symbols_binding_and_macro_collisions) {
  const auto source = Source(kPs[0]);
  for (std::string_view prefix : {"uint srCoverageId;\n", "uint g_SrPixelCoverageId;\n",
      "cbuffer SrPixelCoverage { uint id; };\n", "cbuffer Another : register(b4, space4) { uint id; };\n",
      "#define return discard\n", "#define uint float\n", "#define main other\n",
      "#define out\n", "#define SV_Target1 SV_Target0\n"})
    Reject(kPs[0], std::string(prefix) + source);
}

SR_TEST(pixel_coverage_rejects_input_signature_mutations_and_hidden_macro_returns) {
  for (uint64_t hash : kPs) {
    for (std::string_view replacement : {"in float4 iTexCoord0 : TEXCOORD1",
        "in float3 iTexCoord0 : TEXCOORD0", "in float4 renamed : TEXCOORD0"}) {
      auto changed = Source(hash);
      Replace(changed, "in float4 iTexCoord0 : TEXCOORD0", replacement);
      Reject(hash, changed);
    }
    for (std::string_view macro : {"#define PC_RET re ## turn\n", "#define PC_RET re\\\nturn\n",
                                  "#define PC_RET re\\\n\\\nturn\n"}) {
      auto changed = Source(hash);
      changed.insert(changed.find('{', changed.find(kOutput)) + 1, "\nif (iPos.x < 0) { PC_RET; }\n");
      changed.insert(0, macro);
      Reject(hash, changed);
    }
  }
}

SR_TEST(pixel_coverage_rejects_malformed_and_unbounded_text_without_partial_output) {
  const auto source = Source(kPs[0]);
  Reject(kPs[0], "");
  Reject(kPs[0], std::string(1024 * 1024 + 1, ' '));
  Reject(kPs[0], source + "/* unterminated");
  Reject(kPs[0], source + "\"unterminated");
  Reject(kPs[0], source + std::string(1, '\0'));
  Reject(kPs[0], source + "\r");
  Reject(kPs[0], source + "}");
  auto changed = source; changed.pop_back(); changed.pop_back(); Reject(kPs[0], changed);
  changed = source; Replace(changed, "}\treturn;", "}\n#if 1\nreturn;\n#endif"); Reject(kPs[0], changed);
}

SR_TEST(pixel_coverage_lf_and_crlf_normalize_identically_without_mutating_input) {
  for (uint64_t hash : kPs) {
    const auto source = Source(hash);
    std::string crlf;
    for (char c : source) { if (c == '\n') crlf += '\r'; crlf += c; }
    const auto original = crlf;
    std::string first, second, error;
    SR_CHECK(SpecializePixelCoverageShader(hash, source, first, error));
    SR_CHECK(SpecializePixelCoverageShader(hash, crlf, second, error));
    SR_CHECK(!first.empty() && first == second);
    SR_CHECK(crlf == original);
    auto alias = source;
    SR_CHECK(SpecializePixelCoverageShader(hash, alias, alias, error));
    SR_CHECK(alias == first);
  }
}

SR_TEST(pixel_coverage_cache_identity_contains_version_and_actual_emitted_patch) {
  std::string output, error;
  const auto source = Source(kPs[0]);
  const bool specialized = SpecializePixelCoverageShader(kPs[0], source, output, error);
  SR_CHECK(specialized);
  if (!specialized) return;
  const auto prefix_end = output.find(source.substr(0, 80));
  SR_CHECK(prefix_end != std::string::npos);
  if (prefix_end == std::string::npos) return;
  const auto identity = PixelCoverageShaderCacheIdentity();
  SR_CHECK(identity.find("pixel-coverage-structural-audit-v5-opaque-alpha") != std::string_view::npos);
  SR_CHECK(identity.find(std::string_view(output).substr(0, prefix_end)) != std::string_view::npos);
  SR_CHECK(identity.find(kExtra) != std::string_view::npos);
  SR_CHECK(identity.find(kAssignment) != std::string_view::npos);
  SR_CHECK(identity.find(kOutputScale) != std::string_view::npos);
}

SR_TEST(pixel_coverage_cape_accepts_only_the_original_pixel_position_branch) {
  const auto source=Source(0x71CB82D791DE8E77ull);
  for(std::string_view replacement : {"#ifdef OTHER_ENGINE", "#if 1", "#ifdef CONAN_RECOMP\nreturn;"}) {
    auto changed=source;
    Replace(changed,"float4 r5 = \n#ifdef CONAN_RECOMP",std::string("float4 r5 = \n")+std::string(replacement));
    Reject(0x71CB82D791DE8E77ull,changed);
  }
}

SR_TEST(pixel_coverage_vfx_preserves_depth_and_marks_only_contributing_alpha) {
  const auto root=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
  for(auto hash:{0x201C30CD5C58A1BCull,0x5C1E60F40BD1DC6Eull}) {
    const char* file=hash==0x201C30CD5C58A1BCull ? "201C30CD5C58A1BC.ps.hlsl" : "5C1E60F40BD1DC6E.ps.hlsl";
    std::ifstream stream(root/"artifacts/shaders/hlsl"/file,std::ios::binary);
    SR_CHECK(stream.good());std::string source{std::istreambuf_iterator<char>(stream),{}};
    std::string output,error;
    SR_CHECK(SpecializePixelCoverageShader(hash,source,output,error));
    SR_CHECK(output.find("oC0.w > 0.001")!=std::string::npos);
    SR_CHECK(output.find("InterlockedOr(g_SrReactiveMask[uint2(iPos.xy)], 1u, ignored)")!=std::string::npos);
    SR_CHECK(output.find("SV_Target1")!=std::string::npos);
    if(hash==0x201C30CD5C58A1BCull) SR_CHECK(output.find("out float oDepth : SV_Depth")!=std::string::npos);
    SR_CHECK(IsPixelCoverageShaderPair(0x0408D37D5D96E869ull,hash));
  }
}

SR_TEST(pixel_coverage_composes_with_rt_for_every_audited_intersection) {
  size_t count=0;
  for(uint64_t hash:kPs) {
    if(!IsRtShadowShader(hash)) continue;
    ++count;std::string coverage,combined,error;
    SR_CHECK(SpecializePixelCoverageShader(hash,Source(hash),coverage,error));
    SR_CHECK(SpecializeRtShadowShader(hash,coverage,combined,error));
    SR_CHECK(combined.find(kAssignment)!=std::string::npos);
    SR_CHECK(combined.find("g_SrRtShadowStandalone")!=std::string::npos);
  }
  SR_CHECK(count>=2);
}
SR_TEST(pixel_coverage_observed_instanced_pairs_only_add_original_pixel_ownership) {
  SR_CHECK(IsPixelCoverageShaderPair(0xDF1EB50388CC2563ull,0xADAF9F96B596F6EFull));
  SR_CHECK(IsPixelCoverageShaderPair(0xDF1EB50388CC2563ull,0xF2BB7B6551646033ull));
  SR_CHECK(IsPixelCoverageShaderPair(0xEA88A5BB6930F942ull,0xA828328313FF36B9ull));
  SR_CHECK(!IsPixelCoverageShaderPair(0xDF1EB50388CC2563ull,0xA828328313FF36B9ull));
  SR_CHECK(!IsPixelCoverageShaderPair(0xEA88A5BB6930F942ull,0xF2BB7B6551646033ull));
}
