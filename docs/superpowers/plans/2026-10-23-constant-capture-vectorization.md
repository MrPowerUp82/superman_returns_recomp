# Constant capture vectorization — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Reduzir o custo comprovado da captura de constantes sem alterar seus bits ou exigir instruções além do baseline da build.

**Architecture:** O objeto Release atual executa um loop escalar mesmo em -O3. Um protótipo isolado que armazena `source.data()` antes do loop permitiu SSE no mesmo compilador e caiu de 1.439 para 385 ns/banco host. Aplicar essa mudança mínima somente depois de fixar equivalência e observar o gate de desempenho falhar no objeto atual. Manter API, seleção de bancos, versões e uploads.

**Tech Stack:** C++23, Clang 23, benchmark manual, testes nativos, D3D12.

**Spec:** `docs/superpowers/specs/2026-10-23-native-renderer-parity-design.md`, aprovado; etapa CPU.

## Global Constraints

- Dados antes da mudança; nenhuma promessa de 60 FPS pelo microbenchmark.
- Preserve NaN→zero, ±infinito, zeros com sinal, denormais, endian e truncamento sem escrita.
- Não adicionar AVX2 obrigatório, fast-math, restrict ou exceção de alias para obter vetorização.
- Mesmo checkout experimental e build normal preservada. Áudio adiado; stacked resolves permanece Off.

## Review Focus

- Inteiros não devem virar comparação FP que altere sNaN/flags ou elimine infinitos.
- Spans válidos podem exceder o banco e começar fora de alinhamento SIMD.
- Captura in-place precisa conservar comportamento.
- Gate de nanossegundos é somente local e manual, não teste de CI portável.
- Resultados de gameplay sem câmera alinhada não demonstram causalidade de FPS nem aprovam imagem.

### Task 1: Contrato e desempenho do kernel

**Files:** `tests/native/test_guest_state.cpp`, novo `tools/bench/bench_constant_capture.cpp`, `port/src/graphics/guest/constant_snapshot.cpp`.
**Interfaces:** conservar `CaptureFloatConstants(span,bool,array&,string&)`; benchmark liga o objeto real e aceita teto host opcional.
- [x] Adicionar equivalência por bits em padrões IEEE e pseudoaleatórios, endian, in-place e fonte deslocada/truncada. Executar a suíte no baseline para fixar comportamento existente.
- [x] Criar benchmark sem implementação duplicada; observar RED no objeto atual com teto local de 700 ns/banco host (menos de metade da mediana medida).
- [x] Armazenar ponteiro de entrada antes do loop; build e suíte GREEN; gate manual GREEN e assembly SIMD.

### Task 2: Integração medida

**Files:** logs novos e `docs/constant-capture-vectorization-2026-10-23.md`.
**Interfaces:** perfis existentes de captura/escrita medem o trecho modificado, sem API nova.
- [x] Compilar incremental, executar New Game aquecido com mesmos flags detalhados da fundação, colher ≥6 blocos de 120 swaps e timeline fora do trace.
- [x] Conferir custos/contabilidade, gameplay e limitações de comparação. Fazer revisão independente do diff e commit local.
