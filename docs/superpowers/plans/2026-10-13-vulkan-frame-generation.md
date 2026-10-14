# Vulkan Frame Generation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Execution method approved by user: native, in this session.

**Goal:** Implementar FG 2x integrado ao Vulkan, opcional e persistente no launcher, e validar quadros realmente gerados/apresentados.

**Architecture:** Emprestar o dispositivo/fila Vulkan existentes ao backend LSFG-VK e registrar os passes nos command buffers do FrameLoop. Uma sessão do presenter preserva os originais compostos, mantém histórico por serial real e apresenta intermediário/original com esperas fora de gpu_mutex. O caminho desligado continua usando o presenter atual.

**Tech Stack:** C++23, Vulkan 1.1 + VK_KHR_timeline_semaphore (ou funcionalidades core suportadas), LSFG-VK vendorizado, DLL local do usuário, C#/WPF/.NET 8, CMake/Ninja/Clang/ThinLTO.

**Spec:** `docs/superpowers/specs/2026-10-13-vulkan-frame-generation-design.md` (aprovada).

## Global Constraints

- Render e apresentação do jogo permanecem Vulkan; não usar aplicativo externo nem fallback D3D12/Xenos.
- Um intermediário por par (2x), modo performance; configuração nova desligada e alterações do launcher na próxima execução.
- FG desligado não lê a DLL nem cria sessão LSFG; não alterar simulação, limite real de 30 FPS ou qualidade.
- FPS reais, intermediários e apresentações repetidas são contados separadamente; não obter FPS apresentados por multiplicação.
- Dispositivo/fila/buffers emprestados não são destruídos pelo backend; recursos retidos até fences; não usar WaitIdle por par.
- Preservar todas as alterações de CPU existentes e código gerado. Sem commits/push, reset ou limpeza do trabalho anterior nesta execução.
- Manter avisos/licença GPL-3.0-or-later; não empacotar a Lossless.dll do usuário.
- Sem builds, hashing de fontes ou testes pesados concorrentes aos benchmarks de gameplay.

## Review Focus

- INI legado, caminho com espaços/Unicode e DLL removida depois de salvar: opção off funciona e on diagnostica sem mudar API (Task 1).
- Serial repetido/stale, pausa longa e relógio regressivo: não interpolar frames incorretos nem contar repaint como frame real (Task 2).
- Backend sem suporte timeline ou falha de DLL: off preserva device creation; on não usa funcionalidades não habilitadas (Tasks 3/4).
- Resize/desconexão com geração em voo: retirar trabalho antes de liberar imagens/histórico e reiniciar warmup (Tasks 4/5).
- Falha de acquire/submit/present ou erro depois de comandos parciais: não contar apresentação falha nem reutilizar buffer inválido (Task 5).

## Arquivos e responsabilidades

- `launcher/LauncherSettings.cs`, `MainWindow.xaml/.cs`: persistência, checkbox, seleção de DLL, argumentos.
- `port/src/graphics/vulkan/frame_generation_policy.h`: estado/cadência/contadores puros, sem SDK ou backend.
- `port/src/graphics/vulkan/context.h/.cpp`, `loader.h/.cpp`: consulta e habilitação opcionais de timeline, entry points.
- `port/third_party/lsfg-vk/lsfg-vk-common/.../command_buffer.hpp/.cpp`: vista emprestada explícita.
- `port/src/graphics/vulkan/frame_generation.h/.cpp`: sessão GPU, composição canônica, warmup, imagens e geração.
- `port/src/graphics/vulkan/platform/native_provider.h/.cpp`: configuração, serial real, pares e ciclo de vida.
- `port/src/graphics/backend_selection.h/.cpp`, `native_renderer/native_bridge.cpp`, `native_graphics_system.cpp`: aceitar apenas FG compilado e transmitir configuração.
- `port/CMakeLists.txt`, `port/src/graphics/vulkan/CMakeLists.txt`, `tests/vulkan/CMakeLists.txt`, `tests/native/CMakeLists.txt`: alvos opcionais e testes.
- `tests/launcher/Program.cs`, `tests/native/test_frame_generation_policy.cpp`, `tests/vulkan/test_context.cpp`, `test_frame_loop.cpp`, `frame_generation_integration.cpp`: verificação.
- `docs/lsfg-integration.md`: resultados medidos, opções e limites.

## Comandos de referência

Executar CMake/Ninja dentro do ambiente VsDevCmd x64 existente. CMake:
`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.
SDK .NET local: `.tools/dotnet-launcher/dotnet.exe`.
DLL real: `C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling\Lossless.dll`.
Não usar build.cmd para reconfigurar: preservar `-O3 -DNDEBUG` e SR_THIN_LTO=ON.
Builds do jogo usam `--parallel 2`; registrar logs e informar progresso durante link.

Verificações usadas nos blocos (sempre no ambiente de build acima):
`cmake --build build/tests-native --target sr_native_tests --parallel 2`,
`build/tests-native/sr_native_tests.exe`,
`cmake --build build/tests-vulkan --parallel 2`,
`build/tests-vulkan/sr_vulkan_tests.exe`,
`build/tests-vulkan/sr_vulkan_contract_test.exe --production-bindings`.
Passa quando o processo termina em 0, sem checks falhando, e a fixture GPU
informa validation_errors=0. Warnings preexistentes não equivalem a falha de teste.

A fixture nova é o alvo `sr_vulkan_frame_generation_test`, registrado no CMake
Vulkan apenas com `SR_LSFG=ON`/backend disponível, e linka sr_vulkan_core e
sr_lsfg_backend. Compilar com
`cmake --build port/out/build/win-amd64-release --target sr_vulkan_frame_generation_test --parallel 2`;
rodar o EXE retornado pelo target com `"C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling\Lossless.dll"`.
Essa fixture executa os testes de ownership/geração/reset e imprime
`validation_errors=0`; missing DLL nunca produz um falso PASS.

### Task 1: Contrato persistente e controle no launcher

**Files:** launcher/LauncherSettings.cs, launcher/MainWindow.xaml/.cs, tests/launcher/Program.cs.

**Interfaces:** Produz `bool FrameGeneration` (default false), `string FrameGenerationDll` e `static string FindFrameGenerationDll()`. BuildArgumentList mantém API e inclui sr_lsfg false/true; modo/dll somente quando ligado. INI: `frame_generation` e `frame_generation_dll`.

- [x] Acrescentar testes red: default/INI legado desligado; round-trip on/off; true mantém `--sr_native_api=vulkan`; caminho `fixture com espaço ç/Lossless.dll` é um argumento; false não envia DLL; DLL ausente impede CreateLaunch somente quando true. DetectDefaults pode localizar a instalação local, sem distribuir seus bytes.
- [x] Rodar `.tools/dotnet-launcher/dotnet.exe run --project tests/launcher/LauncherChecks.csproj -c Release`; confirmar falha pelas propriedades/argumentos ausentes.
- [x] Implementar propriedades/persistência/validação, checkbox `Geração de quadros (2x)`, campo e botão para selecionar DLL. ReadSettings/ApplySettingsToUi e Reset preservam fluxo atual; ativar/desativar atualiza o resumo.
- [x] Rodar testes até passarem e `tools/build_launcher.ps1 -OutDir artifacts/launcher-fg-check`; confirmar publish sem erro. A disponibilidade final do executável FG será concluída nas Tasks 5/6.

### Task 2: Política de serial, histórico e contadores

**Files:** criar frame_generation_policy.h e tests/native/test_frame_generation_policy.cpp; modificar tests/native/CMakeLists.txt.

**Interfaces:** `enum class FramePresentationKind { kReal,kGenerated,kRepeat }; enum class GenerationAction { kWarmup,kInterpolate,kRepeat,kStale }; class FrameGenerationPolicy` com `GenerationAction OnSource(uint64_t serial,uint64_t now_ns)`, `uint64_t HalfIntervalNs() const`, `void OnPresented(FramePresentationKind)`, `void Reset()`, e contadores reais/gerados/repetidos somente via OnPresented.

- [x] Escrever testes red: primeiro serial warmup; serial seguinte em 66 ms interpola e usa 33 ms; mesmo serial repeat sem avançar histórico; serial inferior stale; Reset exige warmup; pausas >250 ms ou regressão do relógio exigem warmup; half interval limitado entre 1 e 50 ms; apresentações reais/geradas/repetidas contam somente depois de sucesso.
- [x] Build `sr_native_tests` em build/tests-native; confirmar red pelo novo contrato ausente.
- [x] Implementar política independente de GPU, sem relógio global/sleep ou leitura da DLL. Retornar decisão e deadline ao presenter; não gerar/capturar por repaint.
- [x] Rodar build/tests-native/sr_native_tests.exe; todas as verificações passam.

### Task 3: Funcionalidades Vulkan e vista emprestada

**Files:** context.h/.cpp, loader.h/.cpp, wrapper command_buffer.hpp/.cpp; tests/vulkan/test_context.cpp; nova fixture de ownership em tests/vulkan/frame_generation_integration.cpp e CMake opcional.

**Interfaces:** Acrescentar ao final de `Context::OpenDevice` `bool request_timeline=false`; expor `bool timeline_enabled=false`. A consulta/ativação suportada configura pNext sem alterar as features antigas. Wrapper ganha `explicit vk::CommandBuffer(VkCommandBuffer borrowed)` e `bool borrowed() const`; handle usa o buffer fornecido. Begin/end/submit de vista emprestada rejeitam uso; comandos de registro permanecem permitidos.

- [x] Testes red do dispatch: off não acrescenta feature/extensão timeline; solicitação suportada acrescenta feature e resolve funções necessárias; falta de suporte mantém device válido sem timeline e diagnóstico claro. Não chamar core 1.2 em device criado como 1.1 sem extensão/alias correto.
- [x] Teste ownership red: construir/destruir vista não aloca/libera/reset/submete buffer; handle idêntico; begin/end/submit emprestados rejeitados; wrapper proprietário mantém comportamento.
- [x] Implementar opção consultada antes de vkCreateDevice, funções de features2/entry points necessárias e vista sem deleter de handles do caller. Se timeline indisponível, registrar FG indisponível para a Task 4, preservando renderer original.
- [x] Build/testes Vulkan; fixture emprestada destrói backend e depois confirma que device do caller continua operacional. Zero erros de validação.

### Task 4: Sessão GPU no mesmo dispositivo

**Files:** criar frame_generation.h/.cpp e completar frame_generation_integration.cpp; CMake Vulkan/LSFG opcional.

**Interfaces:** `FrameGenerationConfig { bool enabled=false; std::filesystem::path dll; bool performance=true; }`; `FrameGenerationInput { uint64_t source_serial; std::shared_ptr<TextureResource> image; std::array<uint32_t,256> gamma; bool gamma_enabled; uint32_t aspect_x,aspect_y; VkExtent2D extent; }`; `FrameGenerationOutput { std::shared_ptr<TextureResource> original,generated; }`.
`FrameGenerationSession(Context&,FrameGenerationConfig)` com `bool Initialize(Error&)`, `bool Record(VkCommandBuffer,uint64_t presentation_serial,const FrameGenerationInput&,ImageState& source_state,FrameGenerationOutput&,Error&)`, `void Retire(uint64_t)`, `void ResetAfterRetire()` e `bool active() const`. Implementação privada contém backend/recursos, sem tipos LSFG nos includes do launcher/core puro.

- [x] Fixture red: renderizar entrada sintética com movimento e gamma num RGBA8 canônico; primeiro frame só original; frame seguinte intermediário distinto de repetição; cópia GPU/readback do original coincide com composição sem FG. Testar warmup/reset de extent e erro de DLL antes de registro.
- [x] Implementar BorrowedDevice, openLocalContext FP32/performance/um intermediário, wrapper emprestado da Task 3 e warmup dos dois RGB + todos os slots de features (quatro recordFrame iniciais, como caminho validado existente).
- [x] Compor frame real em target RGBA8 local usando FrontbufferCompositor/TargetStore; preservar cópia original. Transferir para imagens LSFG e copiar intermediário para TextureResource próprio da sessão, evitando wrappers que destruam imagens emprestadas. Usar GPU, estados únicos e pool limitado por fences; não ler pixels na CPU em produção.
- [x] Registrar tempo GPU por timestamp query, colher depois de fence sem esperar sincronamente. Setup falhando antes do registro deixa active=false e diagnóstico. Falha durante gravação retorna erro ao caller, sem reutilizar command buffer parcial como sucesso.
- [x] Rodar fixture com DLL real e validation layers; conferir originais, intermediários, resize, shutdown e device emprestado ainda válido. Zero validation_errors.

### Task 5: Presenter Vulkan e pares em cadência 2x

**Files:** native_provider.h/.cpp, frame_loop.cpp/h somente se necessário para hooks; backend_selection.h/.cpp, native_bridge.cpp, native_graphics_system.cpp; tests/native/test_backend_selection.cpp, tests/vulkan/test_frame_loop.cpp, fixture GPU.

**Interfaces:** `NativeProviderConfig` ganha FrameGenerationConfig; `ValidateNativeApi` recebe `bool lsfg_compiled=false` no final para permitir solicitação apenas no build correspondente. Mailbox/Publish transportam `uint64_t source_serial` vindo de `SwapPacket::guest_swap`. Composição de outputs canônicos usa gamma desativada para não aplicá-la duas vezes.

- [x] Tests red: suporte compilado aceita sr_lsfg sem liberar outras opções inválidas; sem backend diagnostica; off mantém configuração. Callback Publish/Consume preserva serial e não usa endereço da imagem como identidade.
- [x] Usar FrameGenerationPolicy da Task 2 e sessão da Task 4. Em novo serial registrar composição/FG antes do render pass de apresentação, apresentar intermediário primeiro quando disponível, esperar half interval fora de gpu_mutex e apresentar original preservado. Warmup/repaint/stale seguem política.
- [x] Manter chamada de Retire para todos os recursos e origem retida até fences. Em resize/disconnect, retirar frames antes de ResetAfterRetire. Destruição para produtor, retira GPU e destrói sessão antes do dispositivo. Não inserir WaitIdle por par.
- [ ] Tests red/green de lock probe: acquire/fence/pacing deixam gpu_mutex livre; prepare/submit/present são serializados. Falhas de acquire/submit/present não incrementam contadores; erro parcial não reapresenta saída modificada. Testar reset enquanto há dois slots em voo.
- [x] Logar a cada janela de aproximadamente 2 segundos os contadores e FPS medidos reais/gerados/repetidos/apresentados, GPU FG, resets/falhas. Tempo GPU obtido por queries, nunca pela duração de CPU do recordFrame.
- [x] Rodar suites nativa/Vulkan e fixture de apresentação com zero erros de validação.

### Task 6: Builds completos, launcher e validação de gameplay

**Files:** port/CMakeLists.txt e CMake Vulkan/LSFG; docs/lsfg-integration.md; binários locais. Testes nas Tasks anteriores.

**Interfaces:** Jogo local `port/out/build/win-amd64-release/superman_returns.exe`, launcher no mesmo diretório e artifacts/launcher; nenhum dado/DLL do usuário redistribuído.

- [x] Preservar backup do EXE atual e cache antes da reconfiguração; configurar SR_LSFG=ON sem trocar otimizações ou recodificar generated. Linkar sr_lsfg_backend ao módulo Vulkan e manter presenter D3D12 existente. Builds standalone off não dependem de ReXGlue/LSFG.
- [x] Compilar jogo com dois jobs e launcher; suites nativa/Vulkan/launcher e contratos de pixels passam. Verificar também build sem backend usando diretório apropriado preservado ou configuração off restaurável, sem sobrescrever o candidato validado.
- [x] Benchmark gameplay off/on/off no mesmo EXE, sem builds simultâneos, mesmas opções 1280x720/scale1/MSAA1/qualidade original. Para on, passar flags explicitamente e salvar logs de contadores + timestamps. Não usar apenas o FPS da simulação para anunciar resultado.
- [ ] Comparar originais da fixture e capturas de gameplay/HUD/menu/movimento; intermediários não precisam ser pixel-identical. Conferir resize, alt-tab, reexecução com checkbox off, DLL ausente e saída sem hang. Não ampliar/afrouxar gate para mascarar artefatos.
- [ ] Conferir launcher publicado on/off e INI persistente, DLL selecionada e API Vulkan no log. Se controle UI automatizado continuar indisponível, informar essa limitação e pedir somente a verificação visual indispensável; não alegar que o checkbox foi clicado.
- [x] Revisão final independente de ownership, falhas, cadência e caminhos off/on; corrigir problemas antes de afirmar conclusão. Registrar números reais, gerados/apresentados, GPU, artefatos e qualquer aceitação ainda pendente em docs/lsfg-integration.md.

## Autorrevisão e handoff

Os seis blocos cobrem launcher, política, requisitos/ownership, GPU, presenter e validação. Cada condição de Review Focus tem teste no bloco proprietário. Assinaturas de sessão/config/outputs e defaults são as mesmas nos blocos consumidores. Não integrar trabalho parcialmente validado nem marcar FG concluído por compilação ou multiplicação de FPS.

Proposta: execução nativa nesta conversa, com revisão independente no final.
Plano e execução nativa aprovados pelo usuário (sim / aprovo).

## Estado da validação — 2026-10-14
Implementação opcional publicada localmente. Suites e fixture GPU/sync passam. Rodada final FG: aproximadamente26 quadros distintos/s; 30 sustentados e 30 reais ainda não atingidos. Checkboxes abertos registram aceitação parcial: probes existentes validam queue/failure/retire, mas falta induzir reset com dois pares em voo no presenter real; capturas não substituem alt-tab/resize/avaliação temporal; checkbox WPF não foi clicado automaticamente devido ao runtime indisponível. Ruling: janela de log2s facilita diagnóstico responsivo, mantendo contagens após sucesso. Evidências e limites em docs/lsfg-integration.md e ledger .superpowers/sdd/2026-10-13-vulkan-frame-generation/progress.md.
