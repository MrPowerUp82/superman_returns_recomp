# Stacked shadow resolves — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Preservar as camadas do shadow map observado e ligá-lo ao heap compatível com o shader D3D12, em experimento Off por padrão.

**Architecture:** Evolução do renderer atual. `docs/parity-d3d12-foundation-2026-10-23.md` comprova fetch 640×640×4 com receptor Texture3D recebendo descritor nulo; `level/slice` são capturados e descartados pelo executor. Registrar os argumentos antes de escolher as cópias. Não trocar por Texture2DArray enquanto o shader declarar Texture3D.

**Tech Stack:** C++23, D3D12, testes nativos puros, Python e captura sintética de gameplay.

**Spec:** `docs/superpowers/specs/2026-10-23-native-renderer-parity-design.md`, aprovado.

## Global Constraints

- Dados antes da mudança de comportamento; experimentar por cvar Off, sem alterar áudio/qualidade/padrões.
- Testar o contrato real, seleção de slices e cópia dentro de limites; sem pular clears pedidos pelo jogo.
- Preservar caminho Off, invalidação de slots/views e lifetimes/fences.
- Fidelidade ao console só com referência comparável; diferenciar binding corrigido de fidelidade comprovada.

## Review Focus

- `level/slice` efetivos ainda desconhecidos: registrar no frame antes de implementar.
- Fetch armazenado 2D versus instrução 3D: não escolher o heap somente por `DataDimension`.
- Recursos com quatro slices mas apenas três grupos de caster: inicialização e slices efetivamente escritos não podem ser presumidos.
- MSAA/depth/mips: não aplicar uma cópia simples para volumes a operações cuja semântica não foi validada.
- Cache de resolve/descriptor: mudar forma/geração exige invalidar views, preservando recursos em voo.

### Task 1: Argumentos reais dos resolves

**Files:** `port/src/native_renderer/native_renderer.cpp`; logs novos e relatório.
**Interfaces:** novo evento `trace resolve arguments level=N slice=N flags=HEX`, antes do evento de destino existente. Sem mudança em ExecResolve.
- [x] Compilar o diagnóstico e executar New Game com trigger somente após aquecimento.
- [x] Juntar partes do log, selecionar a pilha 1E820000 e registrar level/slice/flags de cada grupo.
- [x] Comparar com fetch, DXIL e coordenadas Z do receptor; escrever a decisão do armazenamento e os casos suportados antes do patch.

### Task 2: Plano de subrecursos e binding

**Files:** novo helper puro `port/src/native_renderer/stacked_resolve_policy.h`, teste `tests/native/test_stacked_resolve.cpp`, CMake dos testes.
**Interfaces:** `PlanStackedResolve(std::span<const uint32_t,6> fetch, uint32_t level, uint32_t slice)` retorna dimensões/camada e validade sem API GPU. O contrato de retorno e os valores de nível suportados serão fixados pelos dados da Task 1, não por endereços arbitrários.
- [x] Escrever e executar testes RED para o fetch real, seleção de todas as quatro camadas e rejeição de índices/mips não suportados; valores normais 2D não viram volumes.
- [x] Implementar o helper, executar GREEN e conferir limites/overflow.

### Task 3: Experimento de renderer e comparação

**Files:** `native_renderer.cpp/.h`, integração do helper e relatório.
**Interfaces:** cvar `sr_native_stacked_resolves=false`; armazenamento de pilha compatível com Texture3D, seleção de camada por resolve, binding somente quando a view correspondente existir.
- [x] Implementar apenas operações validadas pela Task 1; conservar fallback explícito para casos não suportados, sem SRV de dimensão incompatível.
- [x] Invalidar caches/retirar recursos como no caminho atual; não reutilizar SRV depois de reciclar heap/ring.
- [x] Build, testes puros e gameplay Off/On com log de recurso/camada/heap; capturas comparáveis e debug layer se disponível.
- [x] Revisão independente; commit local do incremento comprovado, sem promover o cvar a padrão nem alegar fidelidade ao console sem referência.
