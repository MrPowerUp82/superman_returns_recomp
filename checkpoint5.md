# Correções de cutscenes e HUD/Minimapa — 04/10/2026

Continuação de `checkpoint4.md`: resolução das telas verdes em vídeos/cutscenes e compilação do shader de tela do minimapa e indicadores de objetivo.

## Causas e correções

1. **Cutscenes com tela verde (YUV estagnado no frame 0):**
   - **Causa Raiz:** Decodificadores de vídeo (Bink / XMV) gravam planos YUV decodificados na memória virtual do guest (`0x100000000`), contornando os write-watches físicos de memória (`0x200000000`). O renderer revalidava por padrão a cada quadro apenas texturas pequenas até 256 KB (`entry.guest_size <= (256u << 10)`). Como um plano Y em 720p ocupa ~921 KB (e U/V ~230 KB), a textura principal de luminância nunca era marcada como suja após o primeiro frame, mantendo valores zerados (Y=0, U=0, V=0 decodifica para verde puro na conversão de cores YUV→RGB no shader `84AB4982B9F6D441.ps`).
   - **Correção em `port/src/native_renderer/native_renderer.cpp`:**
     - D3D12 (`Renderer::GetTextureSrvIndex`): o limite de revalidação por quadro foi ampliado para 4 MB (`4096u << 10`). Texturas maiores não vigiadas são verificadas a cada 30 quadros.
     - Vulkan (`Renderer::CaptureTextures`): `if(total <= 4096u * 1024 || !texture_watch_) dirty = true;`.
   - **Validação:** Abertura da EA testada via probe de vídeo e dump de frame (`logs/cutscene_test/dump/output_1280x720.raw`). A tela verde desapareceu completamente; o vídeo é exibido de forma nítida e com fidelidade de cores (médias de cor antes: [85.6, 162.3, 134.0], depois: [6.5, 5.2, 31.3]).

2. **Cursor do minimapa e indicadores de objetivos (Shader `978B0FF62C3693C2.vs`):**
   - **Causa Raiz:** O shader de vértices 2D de tela continha múltiplos elementos de entrada com a mesma semântica/índice (`POSITION0` e `TEXCOORD0`). O emissor HLSL do `XenosRecomp` gerava assinaturas de função repetindo os mesmos parâmetros formais (`in float4 iPosition0 : POSITION0, ... in float4 iPosition0 : POSITION0,`), gerando erro de compilação no DXC: `error: redefinition of parameter 'iPosition0'`. Por conta disso, este shader era rejeitado em runtime e os elementos 2D desenhados por ele (cursor do minimapa e marcadores de navegação) não apareciam.
   - **Correção no emissor (`0012-deduplicate-vertex-inputs.patch`):**
     - Em `shader_recompiler.cpp`, introduzido conjunto de controle `std::set<std::pair<DeclUsage, uint32_t>> declaredInputs;` para suprimir parâmetros formais redundantes com mesma semântica e índice de uso, mantendo o mapeamento de endereços no mapa `vertexElements`.
     - Atualizados `.tools/xenosrecomp` e `build/vulkan-m2/emitter-tree`, recompilando o `XenosRecompCorpus.exe`.
     - O shader `978B0FF62C3693C2.vs` agora compila para SPIR-V e DXIL (9.180 bytes) com status `ready`.
   - **Catálogo de pré-shaders (`superman_returns_shaders.srsl`):**
     - Atualizado para **241/241 shaders (99 VS, 142 PS)**, completando 100% dos shaders observados no jogo (0 falhas). Arquivo copiado para os diretórios de build (`build/vulkan-m3-game`, `build/vulkan-main` e `port/out/build/win-amd64-release`).

## Verificação e Testes

- **Suíte de Testes C++ (Vulkan):**
  - `build/tests-vulkan/sr_vulkan_tests.exe`: 63/63 testes aprovados (0 falhas).
- **Suíte de Testes Python:**
  - `python -m unittest discover tests/shaders`: 21/21 testes aprovados (incluindo o novo teste `test_hud_shader_with_duplicate_elements_compiles_ready`).
- **Validação de Gameplay:**
  - Execução de teste do HUD por 640 frames sem travamentos: 959.367 draws executados, 0 skips por shader ausente.
