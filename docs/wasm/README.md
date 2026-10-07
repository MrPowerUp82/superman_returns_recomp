# Compiladores do instalador

Estes arquivos são ferramentas, não shaders traduzidos do jogo.

O instalador usa estes emissores e o DXC para Vulkan (SPIR-V) e D3D12 (DXIL).
As opções de compilação escolhem a saída; o nome `compileToSpirv` do wrapper
também retorna DXIL quando `-spirv` não é passado. O checksum DXIL é calculado
em `docs/js/dxil-container-hash.js`, conforme o algoritmo aberto do DXC.
O bundle XXH3 em `docs/js/vendor/` reproduz as chaves das bibliotecas D3D12;
procedência, versão, hash e licença estão no README dessa pasta.

| Arquivos | Procedência | Licenças |
| --- | --- | --- |
| `hlsl.mjs`, `hlsl.wasm`, `shader_common.h` | XenosRecomp `339af41df2c23dbe3256c1c377716b81a0e0fe6b`, patches locais, `tools/shaders/build_wasm.ps1`, Emscripten 4.0.23 | XenosRecomp/fmt MIT; xxHash BSD-2-Clause |
| `dxcompiler.mjs`, `dxcompiler.wasm` | ShadowDusk `c3768aa53f54257ba5a14a7dea185227150e0499`, `.wasm-build/dxc-wasm-out`, DXC `e043f4a1286f4e1026222ab1bc94e25de8d0e959` | DXC LLVM/NCSA; glue ShadowDusk MIT; SPIRV-Tools Apache-2.0; SPIRV-Headers MIT |

Textos integrais em `licenses/`. Fontes/reprodução do DXC:
[build report](https://github.com/kaltinril/ShadowDusk/blob/c3768aa53f54257ba5a14a7dea185227150e0499/.wasm-build/DXC-WASM-BUILD.md),
[build script](https://github.com/kaltinril/ShadowDusk/blob/c3768aa53f54257ba5a14a7dea185227150e0499/.wasm-build/build-dxc-wasm.ps1),
[glue](https://github.com/kaltinril/ShadowDusk/blob/c3768aa53f54257ba5a14a7dea185227150e0499/.wasm-build/dxc-wasm-glue.cpp).

SHA-256 conferidos pelo script de obtenção:

```
E24C0D83545FCF198EEBF1DA5BA55E3BFEB790CEC2F942AB9E41692FBAEB5FCB  dxcompiler.mjs
C65696F95E5AEB9E28DE318AC74A7940143F40133F403669665C82B4F0170AB0  dxcompiler.wasm
```

Sirva com HTTP(S), não `file://`. São módulos sem pthreads: não exigem COOP/COEP
nem SharedArrayBuffer. O worker mantém a página responsiva. Os binários são
incluídos para que a página estática funcione sem compilação no servidor.
