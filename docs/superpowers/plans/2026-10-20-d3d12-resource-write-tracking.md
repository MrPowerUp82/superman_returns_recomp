# D3D12 Resource Write Tracking Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remover hashes de texturas D3D12 comprovadamente inalteradas e medir quanto isso aproxima o jogo de 60 FPS reais.

**Architecture:** Um monitor opcional no runtime mantém identidade de alocação e cobertura de todas as vistas de memória física. O renderer consulta provas de ausência de escrita e só as publica para os bytes de um snapshot realmente decodificado e enviado à GPU. A primeira versão protege páginas uma vez: qualquer escrita ou perda de proteção invalida a cobertura até outra geração de alocação, evitando depender de um protocolo de rearmamento ainda não demonstrado.

**Tech Stack:** Windows x64, C++23, testes puros C++20, Clang 23.1.2, CMake/Ninja, PowerShell 5.1, pytest, ReXGlue v0.10.0 (`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`), D3D12.

**Spec:** [Especificação aprovada](../specs/2026-10-20-d3d12-resource-write-tracking-design.md).

## Global Constraints

- Referência: 1280×720, escala 1, MSAA 1, VSync ligado e geração de quadros desligada; limite de 60 FPS nas medições, sem mudar o padrão do jogo.
- Escopo: somente texturas guest D3D12. Não modificar VB/IB, Vulkan, resolves, shaders, qualidade, ordem de draws ou rotinas gráficas Xbox.
- Estados de cobertura: desconhecida, completa e invalidada. Só completa autoriza dispensar hash; amostras sem divergência não promovem cobertura.
- Identidade: intervalo físico canônico e geração de alocação de 64 bits; cobrir todas as faixas de mips/slices e manter descritores de vistas distintos.
- Domínio físico de 512 MiB; metadados por página limitados ao domínio e registros de vistas removidos com o cache.
- Callback de falha não aloca, não registra logs, não chama D3D12 e não espera data providers. Invalidação precede qualquer liberação de proteção.
- Falha, corrida, produtor ativo, overflow ou perda de cobertura exigem fallback da política atual, sem retries ilimitados e sem espaçar validações.
- Modos: desligado por padrão, comparação preservando hashes/uploads anteriores, ativo opcional após verificação de correção.
- SDK pré-compilado atual continua compilando e usando hash. Extensão deve ser reproduzível a partir de fontes/patches versionados; não entregar mudanças manuais em `.tools` ou arquivos gerados.
- Exigir redução repetida de pelo menos 30% em rastreador + hash residual com recursos comparáveis e ausência de regressão repetida do FPS/caminho crítico para recomendar promoção.
- Só declarar 60 FPS com evidência de imagem, cadência e simulação do jogo completo. Resultado inconclusivo ou inviável é registrado explicitamente.

## Review Focus

1. Uma página compartilhada perde proteção por outro consumidor do SDK: todas as provas sobrepostas devem falhar (Task 2).
2. I/O parcialmente concluído ou com erro também pode escrever: produtor deve ficar ativo durante toda a operação e invalidar antes do primeiro byte (Task 3).
3. Cache aponta para o mesmo endereço depois de release/realloc, ou duas vistas usam formatos diferentes: não reutilizar identidade nem recurso incorretos (Tasks 1 e 5).
4. Escrita entre cópia, decode e publicação do upload: nunca certificar bytes diferentes dos efetivamente enviados (Tasks 4 e 5).
5. DLL antiga, encerramento com callback em andamento ou rodada com câmera diferente: fallback e resultado inconclusivo devem funcionar sem crash nem falsa aprovação (Tasks 3 e 6).

---

## Mapa de arquivos e decisões compartilhadas

Os caminhos abaixo são relativos à raiz. `.tools/rexglue-write-tracking-source` e `port/out/build/win-amd64-write-tracking` serão artefatos ignorados, nunca a única cópia de uma mudança.

| Arquivos | Responsabilidade |
| --- | --- |
| `tools/rexglue-write-tracking/include/rex_memory_tracking_abi.h` | ABI C v1 compartilhada pelo runtime e cliente, sem STL na fronteira DLL |
| `tools/rexglue-write-tracking/src/write_tracking_state.{h,cpp}` | Identidade, épocas, cobertura e tokens; sem dependência de GPU |
| `tools/patches/rexglue/v0.10.0/0001-memory-write-tracking.patch` | Hooks no core da versão fixada e inclusão das fontes compartilhadas |
| `tools/setup_memory_tracking_sdk.ps1` | Checkout completo separado, verificação de revisão, aplicação verificável do patch e cópia das fontes compartilhadas |
| `build.cmd`, `port/CMakeLists.txt` | Seleção opcional de fontes SDK pelo hook existente `REXSDK_DIR`; targets de teste |
| `port/src/native_renderer/resource_write_tracking.{h,cpp}` | Cliente opcional, tokens RAII e diagnósticos de decisão |
| `port/src/native_renderer/texture_write_proof.h` | Comparação pura de stamps, modos e prova de upload, sem SDK/Windows |
| `port/src/native_renderer/texture_decode.{h,cpp}` | Decode de snapshot possuído usando o mesmo mapeamento de formatos |
| `port/src/native_renderer/native_renderer.{h,cpp}` | Uso no cache existente, criação e expiração de provas |
| `tests/native/test_resource_write_tracking.cpp`, `tests/native/test_texture_write_proof.cpp` | Modelo puro, política e corridas determinísticas |
| `tests/memory_tracking/{CMakeLists.txt,main.cpp,test_runtime_tracking.cpp,test_texture_upload.cpp}` | Memória real do SDK, produtores e fixture D3D12/WARP |
| `tests/tools/test_memory_tracking_setup.ps1` | Setup idempotente e rejeição de estados incompatíveis |
| `tools/bench/bench_texture_tracking.ps1`, `tools/analysis/texture_tracking_report.py` | Rodadas controladas e relatório de elegibilidade/custo/FPS |
| `tests/tools/test_texture_tracking_report.py`, `tests/tools/test_texture_tracking_bench.ps1` | Rejeição de amostras inválidas e protocolo do benchmark |
| `docs/d3d12-memory-write-coverage.md`, `docs/d3d12-texture-tracking-results.md` | Inventário verificável de writers e evidência final |

As alterações prévias de descriptor recycling e `bench_api.ps1` estão fora destes commits. Registrar o estado inicial; não descartá-las nem incluí-las por `git add .`. O plano não autoriza aumentar seu escopo.

### Contratos de interface para Tasks 1–5

O header ABI usa inteiros de largura fixa e `extern "C"`; todos os ponteiros abaixo são de função na tabela `RexMemoryTrackingApiV1`. A consulta exportada é `const RexMemoryTrackingApiV1* RexGetMemoryTrackingApi(uint32_t version, uint32_t caller_size)`. A tabela começa com `uint32_t version, size; uint64_t capabilities;`. Cliente exige versão 1, tamanho suficiente e todas as capacidades requeridas; caso contrário não inicia monitor.

Tipos definidos na Task 1:

- `RexTrackingRange { uint32_t physical_address, length; }`, intervalo não vazio e integralmente dentro de `[0, 0x20000000)`.
- `RexTrackingSession` e `RexTrackingWatch`: handles opacos `uint64_t`, com zero inválido e geração contra handles antigos.
- `RexTrackingCoverage : uint32_t { Unknown, Complete, Invalidated }`.
- `RexTrackingStamp { uint64_t allocation_generation, coverage_generation, invalidation_epoch; uint32_t active_writers; RexTrackingCoverage coverage; }`, um por intervalo pedido; spans atravessando alocações são divididos antes ou rejeitados.
- `RexTrackingStatus : uint32_t { Ok, Unsupported, InvalidRange, Unavailable, LostCoverage }`.
- `RexTrackingStats`: campos `uint64_t guest_faults, physical_faults, io_begin, io_end, lifecycle_events, protection_failures, coverage_losses, active_watches`.
- Capabilities `uint64_t`: `LifecycleAliases=1`, `VirtualPhysicalGuards=2`, `HostProducers=4`, `AllUnprotectsObserved=8`; máscara exigida `15`. Valores de enum começam em zero, na ordem declarada; `Unknown=0` e `Ok=0`. ABI fixa o alinhamento natural de 8 bytes em x64 e testes de `sizeof/offsetof` nos dois lados da DLL.

Assinaturas da tabela v1:

```cpp
RexTrackingStatus (*start)(void* memory, RexTrackingSession* out);
void (*stop)(RexTrackingSession session);
RexTrackingStatus (*watch)(RexTrackingSession session, const RexTrackingRange* ranges,
                          uint32_t count, RexTrackingWatch* out);
void (*unwatch)(RexTrackingSession session, RexTrackingWatch watch);
RexTrackingStatus (*sample)(RexTrackingSession session, RexTrackingWatch watch,
                           RexTrackingStamp* out, uint32_t count);
void (*invalidate)(RexTrackingSession session, RexTrackingWatch watch);
void (*stats)(RexTrackingSession session, RexTrackingStats* out);
```

`sample` tem ponto de observação sob sincronização do registro; retorna stamps consistentes com todas as faixas. `watch` só retorna cobertura completa depois de instalar proteção em todas as vistas e no ponteiro físico, conhecer o lifecycle e excluir produtores ativos. Aliases existentes devem ser enumerados; alias novo invalida antes de ficar gravável. `invalidate` reprova as alocações envolvidas até sua próxima geração. Comparar hashes nunca é requisito para detectar perda de proteção no modo ativo.

## Task 1: Modelo limitado de identidade, cobertura e prova

**Files:** criar header ABI, `write_tracking_state.{h,cpp}` e `texture_write_proof.h` do mapa; criar os dois testes puros; modificar `tests/native/CMakeLists.txt` somente para adicionar esses arquivos.

**Interfaces:** produz os tipos ABI acima e, em `sr::tracking`, `class WriteTrackingState` com `Allocate(uint32_t address, uint32_t length) -> uint64_t`, `Release(uint32_t address, uint32_t length) -> void`, `Invalidate(uint32_t address, uint32_t length) -> void`, `BeginWrite(uint32_t address, uint32_t length) -> void`, `EndWrite(uint32_t address, uint32_t length) -> void`, `Reset() -> void`, `Sample(RexTrackingRange range) const -> RexTrackingStamp`. A operação `MarkCovered(RexTrackingRange range) -> bool` só é chamada pelo runtime após instalação comprovada das proteções; retorna false para geração já invalidada. Em `texture_write_proof.h`, namespace `superman_returns::native`, produz `bool SameCoveredContent(std::span<const RexTrackingStamp> before, std::span<const RexTrackingStamp> after)` para Tasks 4–5.

- [x] **1. Escrever testes que falham.** `tracking_reallocation_rejects_old_identity`: `SR_CHECK(new_generation != old_generation)`; `tracking_all_ranges_required`: `SR_CHECK(!SameCoveredContent(base_and_unknown_mip, base_and_unknown_mip))`; `tracking_active_writer_and_overflow_fail_closed`: exigir false com produtor ativo, contador saturado ou reset; `tracking_domain_bounds_and_shared_pages`: rejeitar overflow/endereço além de `0x20000000`, aceitar último byte e invalidar ambos os intervalos da mesma página. `tracking_missing_or_changed_stamp_rejects_proof`: `SR_CHECK(!SameCoveredContent(before, after))` para tamanho, geração, cobertura e época diferentes, inclusive spans vazios.
- [x] **2. Rodar e observar falha por interfaces ausentes.** `cmake -S tests/native -B build/tests-native` e `cmake --build build/tests-native`; após compilar, `ctest --test-dir build/tests-native --output-on-failure`. Não aceitar falha ambiental como RED.
- [x] **3. Implementar o modelo.** Metadados de páginas no limite físico e tabelas de alocações removíveis, contadores de 64 bits sem wrap silencioso. Saturação/underflow invalidam o domínio. Gerações independentes de alocação/cobertura; cada invalidação recebe uma sequência global crescente e a publica nas páginas afetadas, permitindo resumir um intervalo pelo máximo sem perder mudanças. Liberação invalida antes de remover identidade; EndWrite não restaura cobertura. Locks/atomics e ordenação publicados no header; não usar eventos assíncronos descartáveis como fonte de verdade.
- [x] **4. Verificar.** Rodar os mesmos comandos; novos testes e regressão nativa devem passar. Testar armazenamento limitado após ciclos repetidos de alloc/release/watch, sem crescimento por draw; nova tentativa de MarkCovered depois de fault deve falhar na mesma geração.
- [x] **5. Commit.** Adicionar explicitamente apenas arquivos novos desta task e os hunks correspondentes de `tests/native/CMakeLists.txt`; `git commit -m "feat: model texture memory coverage and allocation identity"`.

## Task 2: Proteção real de todas as vistas e lifecycle no SDK

**Files:** criar setup, teste de setup, patch SDK e inventário de cobertura; modificar `build.cmd`, `port/CMakeLists.txt`; criar `tests/memory_tracking/CMakeLists.txt`, `main.cpp`, `test_runtime_tracking.cpp`.

**Interfaces:** consome Task 1; implementa tabela ABI para `rex::memory::Memory`. Acrescenta `SR_MEMORY_TRACKING_SDK=ON|OFF` ao build, OFF padrão; ON exige checkout preparado e passa `-DREXSDK_DIR=<checkout>` ao hook já existente, OFF passa `-DREXSDK_DIR=` para não herdar cache antigo. Usa saída separada `SR_BUILD_DIR`. Setup: `tools/setup_memory_tracking_sdk.ps1 [-VerifyOnly]`; cópia de arquivos compartilhados com SHA256 em manifesto, sem alterar o checkout esparso de GPU. Target de teste do port `sr_memory_tracking_tests` é registrado no CTest pelo mesmo nome.

- [x] **1. Escrever testes que falham.** Setup deve rejeitar SHA incorreto, patch parcialmente aplicado e fonte compartilhada divergente, sem reset/delete automático. Runtime `runtime_guest_aliases_and_raw_physical_store_invalidate`: alocar memória real, proteger, escrever via A/C/E e `TranslatePhysical`, exigir avanço de época antes de liberar o writer bloqueado e conferir o byte depois do store. `runtime_shared_page_unprotect_invalidates_every_watch`: remoção de proteção pelo caminho antigo do SDK avança épocas das duas vistas. `runtime_lifecycle_changes_generation`: alloc fixed/range, alias, Protect, Decommit, Release, Reset nunca preservam stamp. Repetir sobre páginas guest de 4/64 KiB e offset da vista E no Windows. Usar peer interno apenas do target de teste para instalar guardas e pausar faults: a capacidade pública ainda incompleta não pode ser mascarada para aprovar provas.
- [x] **2. Rodar RED.** `powershell -NoProfile -File tests/tools/test_memory_tracking_setup.ps1`; configurar/compilar target `sr_memory_tracking_tests` pelo build opcional. Antes da extensão, o teste de capacidade deve falhar explicitamente como ausente; não simular sucesso.
- [x] **3. Implementar checkout/build reproduzíveis.** Clonar fontes completas no SHA fixado, inicializar as dependências exigidas pela versão e aplicar patch com `git apply --check`. Verificar idempotência por manifesto e `git diff`; nunca sobrescrever trabalho desconhecido. Patch altera `include/rex/system/xmemory.h`, `src/system/xmemory.cpp`, `include/rex/system/mmio_handler.h`, `src/system/mmio_handler.cpp`, `src/system/CMakeLists.txt`; arquivos compartilhados são copiados para diretório SDK identificado pelo manifesto. Garantir que headers, import libs e DLLs do build fonte sejam da mesma origem, inclusive no código GPU embutido; não misturar ABI stock/fonte. Inventariar no documento todos os caminhos que tornam memória gravável, incluindo proteções antigas e aliases e o recovery de proteção obsoleta em `Memory::AccessViolationCallback`; usar enumeração sob o lock de memória, sem duplicar identidade do heap físico pai por alias.
- [x] **4. Implementar guardas sem rearmamento.** Armar todas as vistas canônicas e físicas sob protocolo explícito de sincronização; documentar garantia Windows usada para instalação. No fault, invalidar metadados fixos antes de desproteger; não chamar data provider pelo ponteiro físico. Qualquer proteção removida, falha de armar ou mudança de mapa invalida todos os watches sobrepostos. Depois do primeiro fault, a alocação permanece invalidada até nova geração; não promover por fim de frame. Guardas novas não podem esconder violações reais de acesso ou alterar semântica de MMIO. Capacidades permanecem incompletas até Task 3.
- [x] **5. Verificar GREEN.** Preparar com `powershell -NoProfile -File tools/setup_memory_tracking_sdk.ps1`; definir `$env:SR_MEMORY_TRACKING_SDK='ON'`, `$env:SR_BUILD_DIR="$PWD/port/out/build/win-amd64-write-tracking"`, `$env:SR_BUILD_LAUNCHER='OFF'` e executar `./build.cmd`. Rodar `ctest --test-dir port/out/build/win-amd64-write-tracking -R sr_memory_tracking_tests --output-on-failure`; leitura não dispara writer e provider não reentra. Rodar setup duas vezes e `-VerifyOnly`; manifesto e patch devem coincidir. Compilar também com `SR_MEMORY_TRACKING_SDK=OFF` em diretório separado, confirmando SDK antigo sem símbolos novos obrigatórios. Restaurar variáveis ao terminar.
- [x] **6. Commit.** Adicionar explicitamente setup, teste, patch, documento, fixture e hunks próprios de build/CMake; `git commit -m "feat: add reproducible runtime memory guards and lifecycle tracking"`.

## Task 3: Produtores host, contrato de capacidade e cliente opcional

**Files:** ampliar patch e `test_runtime_tracking.cpp`; criar `resource_write_tracking.{h,cpp}`; atualizar inventário e targets CMake.

**Interfaces:** consome ABI v1. Produz `native::ResourceWriteTracker`: `bool Start(rex::memory::Memory&)`, `void Stop()`, `RexTrackingWatch Watch(std::span<const RexTrackingRange>)`, `void Unwatch(RexTrackingWatch)`, `bool Sample(RexTrackingWatch, std::span<RexTrackingStamp>)`, `void Invalidate(RexTrackingWatch)`. Fallback expresso por false/handle zero; cliente não é dono da memória SDK. No runtime, RAII interno `HostWriteScope(Memory&, uint32_t physical_address, uint32_t length)` chama BeginWrite/EndWrite inclusive em erros; não expor scopes em ABI pública.

- [x] **1. Escrever testes RED.** `runtime_host_io_partial_failure_is_active_until_return`: writer bloqueado antes/durante/depois de `ReadSync`, exigir produtor ativo e prova falsa mesmo com erro/zero bytes; cobrir `ReadScatter` via `ReadInternal`. Incluir leitura de arquivo temporário real pelo backend host para memória física protegida, além da injeção determinística de erro parcial; conferir bytes e evitar depender de fault do kernel. `runtime_uncovered_writer_disqualifies_domain`: capacidade incompleta não inicia sessão completa. `runtime_stop_drains_inflight_faults`: encerrar durante callback sem uso após free nem deadlock. Cliente com export ausente, versão/tamanho errados e capability faltante retorna false sem instalar monitor.
- [x] **2. Rodar RED.** Compilar/executar `sr_memory_tracking_tests`; testes de I/O e negociação devem falhar pelo contrato ainda não implementado.
- [x] **3. Instrumentar produtores antes de anunciar cobertura.** Inspecionar árvore completa por buffers enviados a kernel/driver/I/O e por escritas físicas com proteção contornada; registrar arquivo, função, intervalo, sincronização e teste de cada classe no inventário. Envolver `src/system/xfile.cpp::ReadInternal` antes de entregar buffer físico ao host e terminar em todos os retornos; preservar callbacks antigos após leitura, inclusive sua razão de evitar corridas com páginas vizinhas. Demais transferências host só habilitam capacidade após hook e teste; caminho não comprovado deixa o domínio desconhecido. Stores comuns via ponteiro físico são capturados pela guarda, não por wrappers incompletos de memcpy.
- [x] **4. Implementar negociação e teardown.** Exportar getter v1; resolver com `GetProcAddress` na DLL já carregada, sem carregar outra cópia. Capabilities exigidas: lifecycle/aliases, guards físicos/virtuais, produtores host e invalidação de todas as remoções de proteção. `stop` desregistra, drena handlers e só então libera metadados; não segurar lock do renderer durante esse drain. Estatísticas agregadas são lidas fora dos callbacks.
- [x] **5. Verificar GREEN e viabilidade.** Rodar fixture real com stores concorrentes, callbacks duplicados, perda de notificação injetada, falha de proteção, overflow e início/fim de I/O. Cada perda de garantia deve invalidar cobertura independentemente de hash de auditoria. Teste com DLL stock confirma fallback. Se o inventário não conseguir fechar cobertura de writers relevantes, interromper Tasks 4–5 ativas, entregar evidência e não falsificar capabilities.
- [x] **6. Commit.** Adicionar somente arquivos/hunks da task; `git commit -m "feat: track host writers and negotiate optional memory coverage"`.

## Task 4: Snapshot e comparação sem mudar decisões do renderer

**Files:** modificar `texture_decode.{h,cpp}`, `native_renderer.{h,cpp}`, cliente, `texture_write_proof.h`; ampliar `test_texture_write_proof.cpp`; criar `tests/memory_tracking/test_texture_upload.cpp`.

**Interfaces:** consome Task 3 e `graphics::guest::CaptureTexture(...)`. Produz overload `bool DecodeTexture(const graphics::guest::TextureCapture& capture, DecodedTexture& out, const char** reason = nullptr)`, reutilizando `DecodeTextureLayout` e o mapeamento de formatos atual. Define `enum class TextureTrackingMode { Off, Compare, On }` e `TextureTrackingMode ParseTextureTrackingMode(std::string_view)`; env `SR_D3D12_TEXTURE_WRITE_TRACKING=off|compare|on`, inválido vira Off com diagnóstico único. Nesta task, On permanece indisponível.

- [x] **1. Escrever testes RED.** `proof_matches_owned_snapshot_not_live_guest`: mudar memória depois da cópia e exigir decode/hash dos bytes possuídos; `proof_race_during_capture_rejects_publication`: invalidar entre amostras e exigir ausência de prova; `compare_never_skips_legacy_hash_or_upload`: contagens/decisões iguais ao modo Off; `compare_mismatch_disqualifies_allocation`: injetar ausência de aviso e exigir reprovação permanente da geração. Auditar contexto antes/depois do hash para distinguir corrida de violação.
- [x] **2. Rodar RED.** Suite nativa e fixture `sr_memory_tracking_tests`; exigir falha dos novos comportamentos, sem modificar imagens de referência para fazê-los passar.
- [x] **3. Implementar snapshot certificado candidato.** Em criação/atualização candidata, obter watch, sample antes, capturar todas as faixas na mesma ordem de `DescribeTextureRanges`, sample depois e decodificar o snapshot possuído. Validar novamente antes de publicar a prova. Hash e conteúdo candidato devem vir do mesmo snapshot; falha/race mantém o caminho antigo e não repete indefinidamente. Adaptar conversão de `LinearTexture` sem duplicar regras de formato. Em Compare, resultado candidato serve só à auditoria; seleção de recurso, hashes e uploads continuam exatamente na política anterior.
- [x] **4. Integrar modo e diagnóstico.** Off não inicia sessão nem auditoria por frame. Compare informa bytes/checks elegíveis, custo de consulta, origens, estados, motivo de fallback, invalidação sem mudança e mudança sem invalidação. Limitar detalhes, preservar contagens agregadas. Tokens pertencem a entradas do cache; liberar ao substituir/expulsar, e parar monitor após join do worker e antes de destruir Memory. Não tocar cache no callback.
- [x] **5. Verificar GREEN.** Testes de mips, caudas, slices, formato/descritor, duas vistas da mesma alocação, eviction e captura incompleta passam. Fixture de pixels compara decode possuído com referência. Rodar Compare em título, gameplay parado/movimento, streaming, HUD/minimapa, capa, vídeos e transições, registrar zero falso reuso e cobertura por bytes. Capacidade zero ou divergência interrompe promoção e gera relatório, não habilita On.
- [x] **6. Commit.** Selecionar hunks próprios em arquivos já alterados; `git commit -m "feat: audit texture write proofs against unchanged rendering decisions"`.

## Task 5: Reuso ativo com prova vinculada ao recurso enviado

**Files:** modificar cliente, `texture_write_proof.h` e `native_renderer.{h,cpp}`; ampliar os dois testes de prova/fixture D3D12; atualizar `port/CMakeLists.txt` para fixture WARP e readback.

**Interfaces:** produz `struct TextureUploadProof { RexTrackingWatch watch; std::vector<RexTrackingStamp> stamps; uint64_t resource_version; }` e `bool CanReuseTexture(const TextureUploadProof&, std::span<const RexTrackingStamp> current, uint64_t live_resource_version)`. Uma entrada mantém sua chave de descritor completa; resource_version muda ao substituir o recurso e zero é inválido. Sem recurso GPU válido, nunca reutilizar.

- [x] **1. Escrever testes RED.** `active_reuses_only_same_uploaded_resource`: `SR_CHECK(CanReuseTexture(proof, unchanged, same_version))`, false para recurso ausente, outra versão, outro formato/descritor ou alocação reusada. `active_write_after_observation_invalidates_next_validation`: primeira decisão linearizada pode usar versão corrente; próxima deve fazer fallback. `active_gpu_upload_contains_certified_snapshot`: fixture WARP envia snapshot, altera guest entre decode/upload/publicação, lê pixels e exige que recurso/prova correspondam ao snapshot ou que prova esteja ausente. `active_fallback_keeps_legacy_cadence`: cobertura perdida não aumenta intervalo de checks.
- [x] **2. Rodar RED.** Suite nativa e fixture real; novo teste de On precisa falhar enquanto modo indisponível.
- [x] **3. Implementar On opcional.** Habilitar somente depois do gate de Tasks 2–4, no ponto atual de validação de `GetTextureSrvIndex`, respeitando `checked_frame`. Consultar todos os ranges; prova válida pula apenas hash. Sem prova, manter expressão/política atual de hash. Publicar prova após upload bem-sucedido dos bytes certificados, vinculada ao recurso vigente; escrita posterior invalida próxima validação. Fences e reciclagem de SRV/resource permanecem no dono atual. Não promover alocação invalidada por ter parado de mudar.
- [x] **4. Verificar GREEN.** Rodar testes e fixture D3D12 com mudanças só em mip tail/slice, producers concorrentes, falha de upload, release/realloc e alteração de descriptor. Repetir cenas da Task 4 em On e Off. Tentar debug layer e registrar explicitamente se indisponível; teste sem camada não autoriza declarar validação dela. Comparar imagens apenas com estado alinhado e limites atuais do gate.
- [x] **5. Commit.** `git commit -m "perf: skip D3D12 texture hashes with verified upload proofs"`, adicionando explicitamente apenas hunks/arquivos desta task.

## Task 6: Medição controlada e decisão documentada

**Files:** criar scripts de benchmark/relatório e seus dois testes do mapa; criar `docs/d3d12-texture-tracking-results.md`; modificar cliente e `native_renderer.{h,cpp}` para instrumentação agregada opcional. Reutilizar `benchmark_input_driver.cpp`, `tools/bench/image_gate_lib.ps1` e análise de timeline existentes, sem misturar mudanças anteriores de `bench_api.ps1`.

**Interfaces:** script `bench_texture_tracking.ps1 -Exe <path> -Name <label> -Mode off|compare|on -StageTiming` (switch opcional); report `python tools/analysis/texture_tracking_report.py <manifest.json> --json <output.json>`. Manifesto contém hashes EXE/DLL, argumentos, modo, ordem, cena, HUD inicial/final, imagens, amostras de FPS, volume de recursos e tempos agregados de tracker/hash. `-StageTiming` define `SR_D3D12_TEXTURE_TRACKING_TIMING=1`; implementação mede tracker/hash residual por agregação no worker e é zero custo por check quando desligada. Execuções de aceitação não usam essa opção.

- [x] **1. Escrever testes RED.** `test_report_rejects_mismatched_binary_scene_and_instrumentation`: rejeitar binários/configuração diferentes, ausência de gameplay, câmera desalinhada, amostra curta ou profiling em aceitação. `test_report_requires_repeated_thirty_percent_stage_gain`: amostras 100→70 aprovam estágio, 100→71 não; ganho só em bytes ou regressão repetida de FPS reprova promoção. `test_bench_restores_environment_and_releases_input`: falha sempre restaura env, neutraliza forward e termina só o processo lançado. Testar que imagem com câmera diferente é inconclusiva, nunca PASS nem ajuste de threshold.
- [x] **2. Rodar RED.** `python -m pytest tests/tools/test_texture_tracking_report.py -q` e `powershell -NoProfile -File tests/tools/test_texture_tracking_bench.ps1`.
- [x] **3. Implementar protocolo.** Usar arquivo `SR_BENCH_INPUT` e IDs novos para iniciar/andar, sem depender de tecla perdida entre frames. Confirmar gameplay por HUD no início/fim; saída grava amostras e rejeita falhas. Repetir blocos OFF/ON/ON/OFF no mesmo EXE/DLL, limite 60, 20 s parado + 20 s andando por rodada. Registrar aquecimento e estado inicial; nenhuma build/teste/sampler/profiling concorrente. Rodar custos por estágio em janelas separadas e comparáveis, contabilizando proteção/faults e trabalho do tracker fora do worker quando houver. Compare nunca representa FPS otimizado.
- [x] **4. Verificar GREEN e rodar experimento.** Passar testes dos scripts, suite nativa e fixture reais antes das rodadas. Aplicar `Compare-GateImages` com alinhamento conferido e thresholds existentes; sem alinhamento resultado inconclusivo. Registrar título/gameplay/streaming/HUD/capa/vídeo/transições, capacidades e motivos de fallback. Não aceitar queda de bytes como substituta do tempo total de validação. Se guardas conservadoras tornarem a maioria dos bytes inelegível, registrar limitação e encerrar promoção; rearmamento ou outra arquitetura exige nova decisão, sem ampliar este plano silenciosamente.
- [x] **5. Documentar e revisar resultado.** Relatório separa correção, custo do estágio, FPS completo e limitações. Exigir redução repetida ≥30% de tracker + hash residual e ausência de regressão repetida antes de recomendar padrão ativo; manter Off padrão nesta entrega. Se permanecer abaixo de 60, apontar o estágio medido dominante sem afirmar meta cumprida. Fazer revisão integral do diff e seguir `verification-before-completion` antes de qualquer alegação de sucesso.
- [x] **6. Commit.** Adicionar explicitamente scripts/testes/relatório e hunks de instrumentação desta task; `git commit -m "test: validate texture tracking correctness and D3D12 performance"`.

## Handoff e condições de parada

Ordem sequencial: 1 → 2 → 3 → 4 → 5 → 6. Os limites ABI/lifecycle são compartilhados; não implementar tasks concorrentes sobre o patch/renderer.

Interromper promoção se faltar garantia de writer/alias, runtime reproduzível, igualdade visual ou benefício medido. Nessa situação, preservar fallback, testes e diagnóstico úteis e entregar a evidência; não declarar concluída a meta de 60 FPS. Não lançar outro projeto de XDK/estado incremental sem nova delimitação.

Recomendação de execução: **Native**, pelo mesmo implementador nesta sessão, com revisão integral independente ao final. Há seis tasks fortemente dependentes do mesmo contrato de memória, e contexto adicional por task acrescentaria custo sem permitir paralelismo seguro. Antes de executar, o usuário revisa este plano e escolhe o método conforme a habilidade `writing-plans`.
