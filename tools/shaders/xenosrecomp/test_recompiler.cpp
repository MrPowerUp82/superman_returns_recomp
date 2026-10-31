#include "shader_recompiler.h"

#include <cstdio>

static bool contains(const std::string& text, const char* fragment) {
    if (text.find(fragment) != std::string::npos) return true;
    std::fprintf(stderr, "Missing emission: %s\n%s\n", fragment, text.c_str());
    return false;
}

// Minimal original-style containers exercise the complete PS exit path without
// game assets. Two exits cover both boolean-conditional and ordinary EXEC_END.
static std::string outputShader(bool vertex, uint32_t outputs, bool twoExits = false) {
    std::vector<uint8_t> data(256, 0);
    const auto put = [&](size_t offset, uint32_t word) {
        for (uint32_t lane = 0; lane < 4; ++lane)
            data[offset + lane] = uint8_t(word >> (24 - lane * 8));
    };
    constexpr uint32_t virtualSize = 128, shaderOffset = 36;
    put(0, 0x102A1100u | uint32_t(vertex));
    put(4, virtualSize);
    put(8, 12);
    put(24, shaderOffset);
    put(shaderOffset + 4, 12); // One packed pair of control-flow instructions.
    if (!vertex) put(shaderOffset + 28, outputs);
    // No ALU instructions: address 1 starts immediately after the CF pair.
    put(virtualSize, 1);
    put(virtualSize + 4, (twoExits ? 4u : 2u) << 12); // COND_EXEC_END / EXEC_END
    if (twoExits) put(virtualSize + 8, 2u << 28); // Second EXEC_END.
    ShaderRecompiler recompiler;
    recompiler.recompile(data.data(), "");
    return recompiler.out;
}

static bool outputScaleTests() {
    bool ok = true;
    for (uint32_t outputs : {1u, 2u, 4u, 8u, 15u, 17u}) {
        const auto source = outputShader(false, outputs, true);
        const auto alpha = source.find("clip(oC0.w - g_AlphaThreshold);");
        const auto coverage = source.find("oC0.w = 0.5 + (oC0.w - g_AlphaThreshold)");
        for (uint32_t target = 0; target < 4; ++target) {
            const auto scale = fmt::format("oC{} *= g_ColorOutputScale[{}];", target, target);
            const auto first = source.find(scale);
            if (outputs & (1u << target)) {
                ok &= contains(source, scale.c_str());
                const auto second = source.find(scale, first == std::string::npos ? 0 : first + scale.size());
                const auto secondCoverage = source.find("oC0.w = 0.5 + (oC0.w - g_AlphaThreshold)",
                                                        coverage == std::string::npos ? 0 : coverage + 1);
                ok &= alpha != std::string::npos && coverage != std::string::npos &&
                      first > coverage && second != std::string::npos &&
                      secondCoverage != std::string::npos && second > secondCoverage &&
                      source.find(scale, second + scale.size()) == std::string::npos;
            } else {
                ok &= first == std::string::npos;
            }
        }
        ok &= source.find("oDepth *=") == std::string::npos;
    }
    ok &= outputShader(true, 0).find("g_ColorOutputScale") == std::string::npos;
    ok &= outputShader(false, 16).find("g_ColorOutputScale") == std::string::npos;
    if (!ok) std::fprintf(stderr, "Color output scale must follow coverage at every PS exit, for declared color targets only.\n");
    return ok;
}

int main() {
    AluInstruction instruction{};
    instruction.vectorOpcode = AluVectorOpcode::Mul;
    instruction.vectorDest = 3;
    instruction.vectorWriteMask = 8;
    instruction.src1Select = instruction.src2Select = instruction.src3Select = 1;
    instruction.src1Register = 1;
    instruction.src2Register = 2;
    instruction.src3Register = 3;
    instruction.scalarOpcode = AluScalarOpcode::Maxs;
    instruction.scalarDest = 4;
    instruction.scalarWriteMask = 1;
    ShaderRecompiler alias;
    alias.recompile(instruction);
    bool ok = contains(alias.out, "float4 aluVectorSource = r3;") &&
              contains(alias.out, "r3.w = r1.w * r2.w;") &&
              contains(alias.out, "ps = max(aluVectorSource.w, aluVectorSource.x);") &&
              alias.indentation == 0;

    instruction.src3Register = 5;
    ShaderRecompiler independent;
    independent.recompile(instruction);
    ok &= contains(independent.out, "ps = max(r5.w, r5.x);");

    instruction.vectorOpcode = AluVectorOpcode::MaxA;
    instruction.src3Select = 0;
    instruction.src3Register = 12;
    instruction.const0Relative = 1;
    instruction.constAddressRegisterRelative = 1;
    ShaderRecompiler addressed;
    addressed.recompile(instruction);
    ok &= contains(addressed.out, "int aluAddress = a0;") &&
          contains(addressed.out, "CONST_REL(12 + aluAddress)");

    instruction.isPredicated = 1;
    ShaderRecompiler predicated;
    predicated.recompile(instruction);
    ok &= contains(predicated.out, "if (!p0)") && predicated.indentation == 0;

    // An instanced VS computes the buffer index in r0.x, then overwrites
    // r0.x with its first fetch. Mini-fetches must retain the original index.
    ShaderRecompiler fetching;
    fetching.out = "";
    const char names[] = "instance_data";
    ConstantInfo instance{};
    fetching.constantTableData = reinterpret_cast<const uint8_t*>(names);
    fetching.float4Constants[4] = &instance;
    fetching.vertexElements[7] = VertexElement{7, DeclUsage::Position, 0};
    VertexFetchInstruction fetch{};
    fetch.srcRegister = 0;
    fetch.srcSwizzle = 0;
    fetch.dstRegister = 0;
    fetch.dstSwizzle = 0xFF8; // x/y written, z/w kept
    fetching.recompile(fetch, 7);
    ok &= contains(fetching.out, "srVertexIndex = floor(r0.x);") &&
          contains(fetching.out, "srVertexFetch(0, srVertexIndex, false)");
    fetching.out.clear();
    fetch.isMiniFetch = 1;
    fetch.srcRegister = 9; // ignored: mini-fetch reuses the full-fetch index
    fetching.vertexElements[8] = VertexElement{8, DeclUsage::Normal, 0};
    fetching.recompile(fetch, 8);
    ok &= contains(fetching.out, "asuint(srVertexFetch(48, srVertexIndex, true))") &&
          fetching.out.find("r9.") == std::string::npos;
    fetching.out.clear();
    fetch.isMiniFetch = 0;
    fetch.isIndexRounded = 1;
    fetch.srcRegister = 2;
    fetch.srcSwizzle = 2;
    fetching.recompile(fetch, 8);
    ok &= contains(fetching.out, "srVertexIndex = floor(r2.z + 0.5f);");
    // Static IA fetches must recover unnormalized USHORT2 coordinates from
    // an UNORM host input. The declaration may change without rebinding VS.
    ShaderRecompiler static_fetch;
    static_fetch.vertexElements[7] = VertexElement{7, DeclUsage::Position, 0};
    fetch.isMiniFetch = 0;
    static_fetch.recompile(fetch, 7);
    ok &= contains(static_fetch.out, "srVertexInput(0, iPosition0)");
    ok &= outputScaleTests();
    return ok ? 0 : 1;
}
