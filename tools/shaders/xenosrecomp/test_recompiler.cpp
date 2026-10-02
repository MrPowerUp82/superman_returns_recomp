#include "shader_recompiler.h"

#include <cstdio>

static bool contains(const std::string& text, const char* fragment) {
    if (text.find(fragment) != std::string::npos) return true;
    std::fprintf(stderr, "Missing emission: %s\n%s\n", fragment, text.c_str());
    return false;
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
    return ok ? 0 : 1;
}
