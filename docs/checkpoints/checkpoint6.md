# Backend Vulkan e Integração do Launcher — 04/10/2026

Continuação de `checkpoint5.md`: implementação e testes dos milestones Vulkan (M1, M2 e M3), integração da geração do launcher (`SupermanReturnsLauncher.exe`) na mesma pasta do executável do jogo, testes automatizados e unificação na branch `main`.

## 1. Geração do Launcher na Pasta do Executável do Jogo

- **Objetivo:** Fazer com que o executável do launcher (`SupermanReturnsLauncher.exe`) seja gerado automaticamente no mesmo diretório em que o jogo (`superman_returns.exe`) é compilado (`port\out\build\win-amd64-release` ou `%SR_BUILD_DIR%`).
- **Alterações Implementadas:**
  1. `tools/build_launcher.ps1`:
     - O parâmetro `$OutDir` agora tem como padrão a pasta do executável do jogo (`$env:SR_BUILD_DIR` se definido, ou `port\out\build\win-amd64-release`).
     - Mantém espelhamento automático para `artifacts\launcher` para compatibilidade com outros scripts.
     - Sintaxe compatível com PowerShell 5.1 e superiores.
  2. `build.cmd`:
     - Após a compilação do executável C++ via CMake/Ninja, aciona automaticamente o build do launcher para `%SR_BUILD_DIR%` (controlável via variável de ambiente `SR_BUILD_LAUNCHER`, padrão `ON`).
  3. `tools/package_release.ps1`:
     - Localiza o launcher prioritariamente no diretório de build do executável (`$BuildDir`), com fallback para `artifacts\launcher`.
  4. `launcher/README.md`:
     - Documentação atualizada refletindo a saída padrão ao lado do executável do jogo.

## 2. Visão Geral do Backend Vulkan (Milestones M1, M2 e M3)

- **M1 (Fundação e Swapchain):**
  - Implementado carregador Vulkan dinâmico, seleção de dispositivo físico com preferências explícitas, swapchain com suporte a resize e ciclo de vida de quadros.
  - Testes isolados de renderização (smoke test de triângulo) e validações de superfície.
- **M2 (Front-end Guest e Contratos de Shaders):**
  - Mapeamento e decodificação de bindings guest para layout portátil Vulkan.
  - Pipeline de compilação isolada SPIR-V e cache de shaders em disco.
  - Validação estrita de contratos de interfaces de vértice e fragmento.
- **M3 (Pipeline e Renderização do Jogo):**
  - Implementado sistema de apresentação e ponte nativa com gerenciamento seguro de filas.
  - Reutilização limitada de buffers de host para uploads sem contenção.
  - Pipeline cache persistido antes de finalização de processos.
  - Tratamento de conversões de índice em strips e preservação de resoluções de superfícies em clears com tiling.

## 3. Verificação e Suíte de Testes

- **Launcher Checks:**
  - `& .tools\dotnet-launcher\dotnet.exe run --project tests/launcher/LauncherChecks.csproj -c Release` $\to$ **18/18 checks PASS** (0 failed).
- **Vulkan Unit Tests:**
  - `.\build\tests-vulkan\sr_vulkan_tests.exe` $\to$ **66/66 checks PASS** (0 failed).
- **Shader Tests (Python):**
  - `python -m unittest discover tests/shaders` $\to$ **21/21 tests PASS** (0 failed).
- **Compilação End-to-End:**
  - `build.cmd` executado com sucesso: gera `superman_returns.exe` e `SupermanReturnsLauncher.exe` lado a lado em `port\out\build\win-amd64-release`.
