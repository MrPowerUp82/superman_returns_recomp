#include "test_main.h"
#include "../../port/src/graphics/rt_shadow_shader.h"

#include <cstddef>
#include <type_traits>

using namespace superman_returns::graphics;

namespace {
constexpr uint64_t kShadowPs = 0x8A6DD1D52752FAD6ull;
constexpr std::string_view kAnchor = "r0.x = saturate(-r4.x + r0.x);";
constexpr std::string_view kCall =
    "r0.x = SrRtShadowOcclusion(iTexCoord2.xyz, r0.x, r4.x, iPos.xy);";
const std::string kAuditedSource =
    "void main(\n\tin float4 iPos : SV_Position,\n"
    "\tin float4 iTexCoord2 : TEXCOORD2)\n{\n"
    "\tif (p0)\n\t{\n\t\t{\n\t\t\tfloat4 aluVectorSource = r0;\n"
    "\t\t\tint aluAddress = a0;\n\t\t\tr0.x = saturate(-r4.x + r0.x);\n"
    "\t\t}\n\t}\n\tr9.xyz = r8.zwx * gShadCol.xyz + -r8.zwx;\n}\n";

void ExpectRejected(uint64_t hash, std::string_view source) {
  std::string output = "previous valid result", error;
  SR_CHECK(!SpecializeRtShadowShader(hash, source, output, error));
  SR_CHECK(output.empty());
  SR_CHECK(!error.empty());
}
}  // namespace

SR_TEST(rt_shadow_constants_match_three_hlsl_registers) {
  static_assert(std::is_standard_layout_v<RtShadowDrawConstants>);
  static_assert(std::is_trivial_v<RtShadowDrawConstants>);
  SR_CHECK_EQ(sizeof(RtShadowDrawConstants), 48u);
  SR_CHECK_EQ(offsetof(RtShadowDrawConstants, enabled), 12u);
  SR_CHECK_EQ(offsetof(RtShadowDrawConstants, bias), 16u);
  SR_CHECK_EQ(offsetof(RtShadowDrawConstants, samples), 28u);
  SR_CHECK_EQ(offsetof(RtShadowDrawConstants, mask), 32u);
  SR_CHECK_EQ(offsetof(RtShadowDrawConstants, padding), 36u);
}

SR_TEST(rt_shadow_only_specializes_the_audited_pixel_shader) {
  SR_CHECK(IsRtShadowShader(kShadowPs));
  SR_CHECK(!IsRtShadowShader(0));
  SR_CHECK(!IsRtShadowShader(0x2C45ull));
  ExpectRejected(0, kAuditedSource);
  ExpectRejected(kShadowPs ^ 1, kAuditedSource);
}

SR_TEST(rt_shadow_replaces_only_the_original_occlusion_inside_its_branch) {
  std::string output, error = "stale error";
  SR_CHECK(SpecializeRtShadowShader(kShadowPs, kAuditedSource, output, error));
  SR_CHECK(error.empty());
  // Removing the insertion and prefix must recover every original byte,
  // including the atlas result, its p0 guard, and the downstream color mix.
  const auto original = output.find("void main(");
  SR_CHECK(original != std::string::npos);
  if (original == std::string::npos) return;
  std::string body = output.substr(original);
  const auto call = body.find(kCall);
  SR_CHECK(call != std::string::npos);
  if (call == std::string::npos) return;
  SR_CHECK_EQ(body.find(kAnchor) + kAnchor.size(), call - 4);
  body.erase(call - 4, kCall.size() + 4);  // newline + three tabs
  SR_CHECK(body == kAuditedSource);
  ExpectRejected(kShadowPs, output);  // never double-patch
}

SR_TEST(rt_shadow_family_uses_the_registers_of_its_own_fade_block) {
  constexpr uint64_t kFamilyPs = 0x0D62162D76A532D1ull;
  SR_CHECK(IsRtShadowShader(kFamilyPs));
  std::string source = kAuditedSource;
  source.replace(source.find("aluVectorSource = r0"), 20, "aluVectorSource = r2");
  source.replace(source.find(kAnchor), kAnchor.size(), "r2.x = saturate(-r13.x + r2.x);");
  std::string output, error;
  SR_CHECK(SpecializeRtShadowShader(kFamilyPs, source, output, error));
  SR_CHECK(output.find("r2.x = SrRtShadowOcclusion(iTexCoord2.xyz, r2.x, r13.x, iPos.xy);") != std::string::npos);
  // A block whose guard names another register is not the audited emitter form.
  auto mismatched = source;
  mismatched.replace(mismatched.find("aluVectorSource = r2"), 20, "aluVectorSource = r3");
  ExpectRejected(kFamilyPs, mismatched);
  // Two fade blocks are ambiguous.
  ExpectRejected(kFamilyPs, source + "\nif (p0)\n\t{\n\t\t{\n\t\t\tfloat4 aluVectorSource = r5;\n"
                                     "\t\t\tint aluAddress = a0;\n\t\t\tr5.x = saturate(-r6.x + r5.x);\n\t\t}\n\t}");
}

SR_TEST(rt_shadow_rejects_missing_or_ambiguous_anchors_without_partial_output) {
  ExpectRejected(kShadowPs, "");
  std::string missing = kAuditedSource;
  missing.erase(missing.find(kAnchor), kAnchor.size());
  ExpectRejected(kShadowPs, missing);
  ExpectRejected(kShadowPs, kAuditedSource + std::string(kAnchor));
  auto unguarded = kAuditedSource;
  unguarded.replace(unguarded.find("if (p0)"), 7, "if (p1)");
  ExpectRejected(kShadowPs, unguarded);
}

SR_TEST(rt_shadow_rejects_changed_world_or_pixel_inputs_without_partial_output) {
  for (std::string_view input : {"in float4 iTexCoord2 : TEXCOORD2",
                                 "in float4 iPos : SV_Position"}) {
    auto changed = kAuditedSource;
    changed.erase(changed.find(input), input.size());
    ExpectRejected(kShadowPs, changed);
  }
  for (std::string_view semantic : {"TEXCOORD2", "SV_Position"}) {
    auto changed = kAuditedSource;
    changed.insert(changed.find(semantic) + semantic.size(), "0");
    ExpectRejected(kShadowPs, changed);
  }
  // A declaration outside main must not satisfy the audited input contract.
  auto outside = kAuditedSource;
  constexpr std::string_view world = "in float4 iTexCoord2 : TEXCOORD2";
  outside.erase(outside.find(world), world.size());
  outside.insert(0, world);
  ExpectRejected(kShadowPs, outside);
}

SR_TEST(rt_shadow_rejects_missing_or_ambiguous_main) {
  auto missing = kAuditedSource;
  missing.replace(missing.find("void main("), 10, "void other(");
  ExpectRejected(kShadowPs, missing);
  ExpectRejected(kShadowPs, kAuditedSource + "\nvoid main() {}\n");
}

SR_TEST(rt_shadow_cache_identity_covers_the_emitted_patch) {
  std::string output, error;
  SR_CHECK(SpecializeRtShadowShader(kShadowPs, kAuditedSource, output, error));
  const auto original = output.find("void main(");
  SR_CHECK(original != std::string::npos);
  if (original == std::string::npos) return;
  const auto identity = RtShadowShaderCacheIdentity();
  SR_CHECK(identity.find(std::string_view(output).substr(0, original)) != std::string_view::npos);
  SR_CHECK(identity.find(kCall) != std::string_view::npos);
}

SR_TEST(rt_shadow_preserves_crlf_source) {
  std::string source;
  for (char c : kAuditedSource) {
    if (c == '\n') source += '\r';
    source += c;
  }
  std::string output, error;
  SR_CHECK(SpecializeRtShadowShader(kShadowPs, source, output, error));
  SR_CHECK(output.find(std::string(kAnchor) + "\r\n\t\t\t" + std::string(kCall)) !=
           std::string::npos);
}
